/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
   Copyright (C) 2020 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */
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
#include <limits.h>
#include "rampart.h"
extern char **environ;
extern char *RP_script_path;

/* 
    defined in main program here 
    used here and in server module
*/

pthread_mutex_t loglock;
pthread_mutex_t errlock;
FILE *access_fh;
FILE *error_fh;
int duk_rp_server_logging=0;


/* utility function for rampart object:
      var buf=rampart.utils.StringToBuffer(val); //fixed if string, same type if already buffer
        or
      var buf=rampart.utils.StringToBuffer(val,"[dynamic|fixed]"); //always converted to type
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

/*  Find file searching standard directories 
    check:
    1) as given. -- if absolute, this will be found first
    2) in scriptPath/file -- NO SUBDIR
    3) in ./file --NO SUBDIR
    4) in execpath/subdir/file
    5) in ~/.rampart/subdir/file or if doesn't exist in /tmp/subdir/file (latter is for writing babel code)
    6) in $RAMPART_PATH/subdir/file
    7) in execpath/file -- questionable strategy, but allow running in github build dir from outside that dir.
*/

RPPATH rp_find_path(char *file, char *subdir)
{
    int nlocs=3;
    /* look in these locations and in ./ */
    char *locs[nlocs];
    char *home=( getenv("HOME") ? getenv("HOME") : "/tmp" );
    char *rampart_path=getenv("RAMPART_PATH");
    char homedir[strlen(home)+strlen(homesubdir)+1];
    char *loc="./";
    char *sd= (subdir)?subdir:"";
    RPPATH ret={0};
    char path[PATH_MAX];
    int i=0, skiphome=0;
    struct stat sb;

    if(!file) return ret;

    /* look for it as given before searching paths */
    if (stat(file, &sb) != -1)
    {
        ret.stat=sb;
        if(!realpath(file,ret.path))
            strcpy(ret.path,file);
        return ret;        
    }

    /* look for it in scriptPath */
    strcpy(path,RP_script_path);
    strcat(path,"/");
    strcat(path,file);

    if (stat(path, &sb) != -1)
    {
        ret.stat=sb;
        if(!realpath(path,ret.path))
            strcpy(ret.path,file);
        return ret;
    }

    //printf("looking for file %s%s\n",subdir,file);
    //if(!home || access(home, R_OK)==-1) home="/tmp";
    if ( access(home, R_OK)==-1 )
    {
        if (strcmp( home, "/tmp") != 0){
            home="/tmp";
            if ( access(home, R_OK)!=-1 )
                goto home_accessok;
        }        
        fprintf(stderr, "cannot access %s\nEither your home directory or \"/tmp\"\" should exist and be accessible.\n", home);
        skiphome=1;
        //exit(1);
    }
    
    home_accessok:
    strcpy(homedir,home);
    strcat(homedir,homesubdir); /* ~/.rampart */

    locs[0]=rampart_dir; //this is set in cmdline.c based on path of executable

    /* this should only happen if /tmp is not writable */
    if(skiphome)
    {
        locs[1]=(rampart_path)?rampart_path:RP_INST_PATH;
        nlocs=2;
    }
    else
    {
        locs[1]=homedir;
        locs[2]=(rampart_path)?rampart_path:RP_INST_PATH;
    }
    /* start with cur dir "./" */
    strcpy(path,loc);
    strcat(path,file);

//printf("first check\n");
    //look for it in ./, execpath, homedir, rampart_path
    while(1) {
//printf("checking %s\n",path);
        if (stat(path, &sb) != -1)
        {
//printf("break at i=%d\n",i);
            goto path_found;
        }

//printf ("not found\n");
        if(i==nlocs)
            break;

        strcpy(path,locs[i]);

        /* in case RAMPART_PATH doesn't have trailing '/' */
        if(locs[i][strlen(locs[i])-1] != '/')
            strcat(path,"/");
        strcat(path,sd);
        strcat(path,file);
//printf("new check(%d): %s\n",i,path);
        i++;
    }

    //look in exec path with no subdir
    //TODO: THIS ONE IS QUESTIONABLE. revisit and rethink.
    strcpy(path,rampart_dir);
    strcat(path,"/");
    strcat(path,file);
    if (stat(path, &sb) != -1)
        goto path_found;

    //not found
    ret.path[0]='\0';
    return ret;
    
    //found it above
    path_found:

    ret.stat=sb;
    if(!realpath(path,ret.path))
        strcpy(ret.path,path);
//printf("FOUND %s\n",ret.path);

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
                    return -1;
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
    duk_rp_exit(ctx, exitval);
    return 0;
}

duk_ret_t duk_process_getpid(duk_context *ctx)
{
    duk_push_int(ctx, (duk_int_t)getpid());
    return 1;
}

duk_ret_t duk_process_getppid(duk_context *ctx)
{
    duk_push_int(ctx, (duk_int_t)getppid());
    return 1;
}

duk_ret_t duk_rp_realpath(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0, "realPath requires a String as its sole parameter");
    char respath[PATH_MAX];

    errno=0;
    if(!realpath(path,respath))
        RP_THROW(ctx, "realPath: %s\n", strerror(errno));

    duk_push_string(ctx, respath);
    return 1;
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

    idx=duk_normalize_index(ctx, idx);
    duk_to_buffer(ctx,idx,&sz);

    buf=(unsigned char *)duk_get_buffer_data(ctx,idx,&sz);

    end=buf+sz;
    REMALLOC(out,sz*2);
    
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
    else RP_THROW(ctx,"hexToBuf(): invalid input");\
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



/* ********************* process.exit, process.env and others********************** *
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
    duk_push_c_function(ctx,duk_process_getpid,0);
    duk_put_prop_string(ctx,-2,"getpid");
    duk_push_c_function(ctx,duk_process_getppid,0);
    duk_put_prop_string(ctx,-2,"getppid");

    {   /* add process.argv */
        int i=0;

        duk_push_array(ctx); /* process.argv */

        for (i=0;i<rampart_argc;i++)
        {
            duk_push_string(ctx,rampart_argv[i]);
            duk_put_prop_index(ctx,-2,(duk_uarridx_t)i);
        }
        duk_put_prop_string(ctx,-2,"argv");

        duk_push_string(ctx,rampart_argv[0]);
        duk_put_prop_string(ctx,-2,"argv0");

        duk_push_string(ctx,RP_script_path);
        duk_put_prop_string(ctx,-2,"scriptPath");

    }

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

/* Returns a url-decoded version of str.  New length is put in *len */
/* IMPORTANT: be sure to free() the returned string after use */
char *duk_rp_url_decode(char *str, int *len)
{
    char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
    int i=*len;

    if (i < 0)
        i = strlen(str);

    *len=0;
    while (i)
    {
        if (*pstr == '%' && i > 2)
        {
            *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
            pstr += 2;
            i -= 2;
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
        (*len)++;
        i--;
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


/* *****************************************************
   serialize object to query string 
   return val needs to be freed
   ***************************************************** */
char *duk_rp_object2querystring(duk_context *ctx, duk_idx_t qsidx, int atype)
{
    int i = 0;
    char *ret = (char *)NULL, *s;

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
        int keyl=eq-s;
        int vall=l-(keyl+1);
        char *key=duk_rp_url_decode(s,&keyl);
        char *val=duk_rp_url_decode(eq+1,&vall);
        duk_size_t arrayi;

        //keyl=strlen(key);
        //vall=strlen(val);
        if( keyl > 2 && *(key+keyl-1)==']' && *(key+keyl-2)=='[')
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
    char *s;
    const char *arraytype=NULL;
    duk_idx_t obj_idx=0, str_idx;
    int atype=ARRAYREPEAT;
    
    if(duk_is_object(ctx, 0) && !duk_is_function(ctx, 0))
        obj_idx=0;
    else if (duk_is_object(ctx, 1) && !duk_is_array(ctx, -1) && !duk_is_function(ctx, 1))
        obj_idx=1;
    else
        RP_THROW(ctx, "objectToQuery - object required but not provided");

    str_idx =!obj_idx;
    
    if(duk_is_string(ctx, str_idx))
        arraytype=duk_get_string(ctx, str_idx);

    if (arraytype)
    {
        if (!strcmp("bracket", arraytype))
            atype = ARRAYBRACKETREPEAT;
        else if (!strcmp("comma", arraytype))
            atype = ARRAYCOMMA;
        else if (!strcmp("json", arraytype))
            atype = ARRAYJSON;
    }

    s = duk_rp_object2querystring(ctx, obj_idx, atype);
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
    rampart.utils.readFile("./filename",-20,50,true,{object_of_params});
    items in object_of_params override primitive parameters
*/
duk_ret_t duk_rp_read_file(duk_context *ctx)
{
    const char *filename=NULL;
    int64_t offset=0;
    int64_t length=0;
    duk_idx_t obj_idx=-1;
    int retstring=0;
    FILE *fp;
    void *buf;
    struct stat fstat;
    size_t off;
    size_t nbytes;

    /* get options in any order*/
    {
        int gotoffset=0;
        duk_idx_t i=0;

        while(i<5)
        {

            if (duk_is_string(ctx,i) )
                filename = duk_get_string(ctx, i);
            else if (duk_is_number(ctx,i) )
            {
                if (gotoffset)
                    length=(int64_t) duk_get_number(ctx, i);
                else
                {
                    offset=(int64_t) duk_get_number(ctx, i);
                    gotoffset=1;
                }
            } 
            else if (duk_is_boolean(ctx, i))
                retstring=duk_get_boolean(ctx, i);
            else if (duk_is_object(ctx, i))
                obj_idx=i;
            else
                break;
            i++;
        }
    }
    
    if ( obj_idx != -1) 
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
        RP_THROW(ctx, "readFile() - error, no filename provided");

    if (stat(filename, &fstat) == -1)
        RP_THROW(ctx, "readFile(\"%s\") - error accessing: %s", filename, strerror(errno));

    if(offset < 0)
        offset = (int64_t)fstat.st_size + offset;

    if(length < 1)
        length = ((int64_t)fstat.st_size + length) - offset;

    if( length < 1 )
        RP_THROW(ctx, "readFile(\"%s\") - negative length puts end of read before offset or start of file", filename);
    
    fp = fopen(filename, "r");
    if (fp == NULL)
        RP_THROW(ctx, "readFile(\"%s\") - error opening: %s", filename, strerror(errno));

    if(offset)
    {
        if (fseek(fp, offset, SEEK_SET))
        {
            fclose(fp);
            RP_THROW(ctx, "readFile(\"%s\") - error seeking file: %s", filename, strerror(errno));
        }
    }
    
    buf = duk_push_fixed_buffer(ctx, length);

    off = 0;
    while ((nbytes = fread(buf + off, 1, length - off, fp)) != 0)
    {
        off += nbytes;
    }

    if (ferror(fp))
        RP_THROW(ctx, "readFile(\"%s\") - error reading file: %s", filename, strerror(errno));

    fclose(fp);

    if(retstring)
        duk_buffer_to_string(ctx,-1);    

    return 1;
}

duk_ret_t duk_rp_readln_finalizer(duk_context *ctx)
{
    /* for readln */
    duk_push_this(ctx);
    if (duk_is_undefined(ctx, -1))
        /* for readLine */
        duk_push_current_function(ctx);
    if (duk_is_undefined(ctx, -1))
        return 0;
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer")))
    {
        FILE *fp = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        if (fp)
        {
            fclose(fp);
        }
    }
    return 0;
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
            RP_THROW(ctx, "readln(): error reading file '%s': %s", duk_get_string(ctx, -1), strerror(errno));
        }

        if (nread == -1)
        {
          /*  duk_push_pointer(ctx, NULL);
              duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("filepointer")); */

            /* clear finalizer */
            duk_push_null(ctx);
            duk_set_finalizer(ctx, 0);

            /* close here rather than in finalizer */
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

    if (duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "stream")){\
            const char *s=REQUIRE_STRING(ctx,-1, "error: readline({stream:\"streamName\"},...): streamName must be stdin");
            if (strcmp(s,"stdin")!=0)
                RP_THROW(ctx, "error: readline(stream) - must be stdin\n");
            
        }
        else
            RP_THROW(ctx, "readline - first argument must be a string or rampart.utils.stdin");

        filename="stdin";
        fp=stdin;

    }
    else
    {
        filename = REQUIRE_STRING(ctx, 0, "readline - first argument must be a string or rampart.utils.stdin");

        fp = fopen(filename, "r");
        if (fp == NULL)
            RP_THROW(ctx, "readLine(): error opening '%s': %s", filename, strerror(errno));
    }
    duk_push_object(ctx);

    duk_push_string(ctx,filename);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filename"));

    duk_push_pointer(ctx,fp);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));

    duk_push_c_function(ctx,readline_next,0);
    duk_put_prop_string(ctx, -2, "next");
    
    duk_push_c_function(ctx,duk_rp_readln_finalizer,0);

    duk_push_pointer(ctx,fp);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));


//    duk_put_prop_string(ctx, -2, "finish");
    duk_set_finalizer(ctx, -2);

    return 1;
}

#define DUK_PUT(ctx, type, key, value, idx) \
    {                                       \
        duk_push_##type(ctx, value);        \
        duk_put_prop_string(ctx, idx, key); \
    }

#define push_is_test(propname, test) do{\
    duk_push_boolean(ctx, test(path_stat.st_mode));\
    duk_put_prop_string(ctx, -2, propname);\
} while(0)


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
        //safestat = duk_get_boolean_default(ctx,1,0);
        safestat=1;

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
        RP_THROW(ctx, "stat(): error getting status '%s': %s", path, strerror(errno));
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

    push_is_test("isBlockDevice",S_ISBLK);
    push_is_test("isCharacterDevice",S_ISCHR);
    push_is_test("isDirectory",S_ISDIR);
    push_is_test("isFIFO",S_ISFIFO);
    push_is_test("isFile",S_ISREG);
    push_is_test("isSocket",S_ISSOCK);
    if(islstat)
        push_is_test("isSymbolicLink", S_ISLNK);

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
    RP_THROW(ctx, "trim(): string is required");
    return 0;
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
        REMALLOC(buf, size);                                                                                \
        int nbytes = 0;                                                                                     \
        nread = 0;                                                                                          \
        while ((nbytes = read(fildes, buf + nread, size - nread)) > 0)                                      \
        {                                                                                                   \
            nread += nbytes;                                                                                \
            if (size <= nread){                                                                             \
                size *= 2;                                                                                  \
                REMALLOC(buf, size);                                                                        \
            }                                                                                               \
        }                                                                                                   \
        if (nbytes < 0){                                                                                    \
            free(args);                                                                                     \
            if(env) free(env);                                                                              \
            RP_THROW(ctx, "exec(): could not read output buffer: %s", strerror(errno));                     \
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
 *    stdin: "text",
 *    args: ["ls", "-1"], 
 *    timeout: 1000
 *    kill_signal: 9, background: false });
 */
duk_ret_t duk_rp_exec_raw(duk_context *ctx)
{
    int kill_signal=SIGTERM, background=0, i=0, len=0;
    unsigned int timeout=0;
    const char *path, *stdin_txt=NULL;
    duk_size_t stdin_sz;
    char **args=NULL, **env=NULL;
    duk_size_t nargs;

    // get options
    if(duk_get_prop_string(ctx, -1, "timeout"))
    {
        if (!duk_is_number(ctx, -1))
            RP_THROW(ctx, "exec(): timeout value must be a number");

        timeout = duk_get_uint_default(ctx, -1, 0);
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "killSignal"))
    {
        kill_signal = REQUIRE_INT(ctx, -1, "exec(): killSignal value must be a Number");
    }
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, "path");
    path = REQUIRE_STRING(ctx, -1, "exec(): path must be a String");
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "background"))
    {
       background = REQUIRE_BOOL(ctx, -1, "exec(): background value must be a Boolean");
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "stdin"))
    {
       stdin_txt=REQUIRE_STR_OR_BUF(ctx, -1, &stdin_sz, "exec(): stdin must be a String or Buffer");
    }
    duk_pop(ctx);


    if(duk_get_prop_string(ctx, -1, "appendEnv") && duk_get_boolean_default(ctx,-1,0) )
    {
        char *env_s;
        i=0;
        while ( environ[i] != NULL )
            i++;
        len=i;
        REMALLOC(env, (len + 1) * sizeof(char *));

        i=0;
        while ( (env_s=environ[i]) != NULL )
        {
            env[i]=env_s;
            i++;
        }
        env[len]=NULL;
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "env"))
    {
        int start=len;
        if(!duk_is_object(ctx, -1) || duk_is_function(ctx, -1))
        {
            if(env)
                free(env);
            RP_THROW(ctx, "exec(): option 'env' must be an object or array");
        }

        if(!duk_is_array(ctx, -1))
        {
            duk_uarridx_t arr_idx=0;
            duk_push_array(ctx); //[..., envobj, array ]
            duk_enum(ctx, -2, 0); // [..., envobj, array, enum ]
            while (duk_next(ctx, -1, 1))
            {
                // [..., envobj, array, enum, key, val ]
                if(duk_is_object(ctx, -1))
                    duk_json_encode(ctx, -1);
                duk_push_sprintf(ctx, "%s=%s", duk_get_string(ctx, -2), duk_safe_to_string(ctx, -1));
                // [..., envobj, array, enum, key, val, "key=val" ]
                duk_put_prop_index(ctx, -5, arr_idx);
                // [..., envobj, array, enum, key, val ]
                arr_idx++;
                duk_pop_2(ctx);// [..., envobj, array, enum ]
            }
            duk_pop(ctx);// [..., envobj, array ]
            duk_replace(ctx, -2); //[..., array ]
            duk_dup(ctx, -1); //[opts_obj, array, array ]
            duk_insert(ctx, 0);// put copy out of the way so strings won't be freed
        }
        len += duk_get_length(ctx, -1);
        REMALLOC(env, (len + 1) * sizeof(char *));
        for (i = start; i < len; i++)
        {
            duk_get_prop_index(ctx, -1, i-start);
            if(!duk_is_string(ctx, -1))
            {
                free(env);
                RP_THROW(ctx, "exec(): option 'env' - environment array must contain only strings");
            }
            env[i] = (char *)duk_get_string(ctx, -1);
            duk_pop(ctx);
        }
        env[len]=NULL;
    }
    duk_pop(ctx);


    // get arguments into null terminated buffer
    duk_get_prop_string(ctx, -1, "args");

    if(!duk_is_array(ctx, -1))
    {
        if(env)
            free(env);
        RP_THROW(ctx, "exec(): args value must be an Array");
    }
    nargs = duk_get_length(ctx, -1);

    REMALLOC(args, (nargs + 1) * sizeof(char *));

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
    int stdin_pipe[2];
    if (!background)
    {
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1|| pipe(stdin_pipe) == -1)
        {
            free(args);
            if(env)
                free(env);
            RP_THROW(ctx, "exec(): could not create pipe: %s", strerror(errno));
        }
    }

    pid_t pid;
    if ((pid = fork()) == -1)
    {
        free(args);
        if(env)
            free(env);
        RP_THROW(ctx, "exec(): could not fork: %s", strerror(errno));
    }
    else if (pid == 0)
    {
        if (!background)
        {
            // make pipe equivalent to stdout and stderr
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            dup2(stdin_pipe[0], STDIN_FILENO);
            // close unused pipes
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            close(stdin_pipe[1]);
        }
        if(env)
            execvpe(path, args, env);
        else
            execvp(path, args);
        fprintf(stderr, "exec(): could not execute %s: %s\n", args[0], strerror(errno));
        free(args);
        if(env)
            free(env);
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

    if (!background)
    {
        // close unused pipes
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        close(stdin_pipe[0]);
    }

    if(stdin_txt)
    {
        write(stdin_pipe[1], stdin_txt, (size_t)stdin_sz);
        close(stdin_pipe[1]);
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
        int stdout_nread, stderr_nread;
        char *stdout_buf = NULL;
        char *stderr_buf = NULL;

        // read output
        DUK_UTIL_EXEC_READ_FD(ctx, stdout_buf, stdout_pipe[0], stdout_nread);
        DUK_UTIL_EXEC_READ_FD(ctx, stderr_buf, stderr_pipe[0], stderr_nread);

        waitpid(pid, &exit_status, 0);
        // cancel timeout thread in case it is still running
        if (timeout > 0)
        {
            pthread_cancel(thread);
            pthread_join(thread, NULL);
        }

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
    if(env)
        free(env);
    free(args);
    return 1;
}


duk_ret_t duk_rp_exec(duk_context *ctx)
{
    duk_idx_t i=1, obj_idx=-1, top=duk_get_top(ctx), arr_idx;
    duk_uarridx_t arrayi=0;
    duk_push_object(ctx); //object for exec_raw
    duk_push_array(ctx);  //array for args

    arr_idx=duk_get_top_index(ctx);

    (void) REQUIRE_STRING(ctx, 0, "exec(): first argument must be a String (command to execute)");

    //first argument in argument list is command name
    duk_dup(ctx, 0);
    duk_put_prop_index(ctx, arr_idx, arrayi++);

    // rest of arguments, and mark where object is, if exists
    for (i=1; i<top; i++)
    {
        //first object found is our options object.
        if(obj_idx==-1 && duk_is_object(ctx,i) && !duk_is_function(ctx,i) && !duk_is_array(ctx,i))
        {
            obj_idx=i;
            continue;
        }

        if (!duk_is_string(ctx,i))
        {
            if (duk_is_undefined(ctx, i) )
            {
                duk_push_string(ctx, "undefined");
                duk_replace(ctx,i);
            }
            else if ( !duk_is_function(ctx, i) )
                (void)duk_json_encode(ctx, i);
            else
            {
                duk_push_string(ctx,"{_func:true}");
                duk_replace(ctx,i);
            }
        }    

        duk_dup(ctx,i);
        duk_put_prop_index(ctx, arr_idx, arrayi++);
    }

    /* stack: [ ..., empty_obj, args_arr ] */
    if(obj_idx!=-1)
    {
        duk_pull(ctx, obj_idx);
        /* stack: [ ..., empty_obj, args_arr, options_object ] */
        duk_replace(ctx, -3); 
        /* stack: [ ..., options_object, args_arr ] */
    }
    duk_put_prop_string(ctx, -2, "args");
    duk_pull(ctx,0);
    duk_put_prop_string(ctx,-2,"path");
    top=duk_get_top(ctx);
    if(top>1)
    {
        duk_replace(ctx, 0);
        top--;
    }
    for (i=1;i<top;i++)
        duk_pop(ctx);

    return duk_rp_exec_raw(ctx);
}

duk_ret_t duk_rp_shell(duk_context *ctx)
{
    duk_idx_t sidx=0;
    const char *sh="/bin/bash";

    if(!duk_is_string(ctx,sidx) && !duk_is_string(ctx,++sidx) )
        RP_THROW(ctx, "shell(): error, command must be a string");

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
    int ret, x=0, signal = SIGTERM,kerrno=0;
    
    if(duk_is_number(ctx,1))
        signal=duk_get_int(ctx, 1);

    errno=0;
    ret= kill(pid, signal);
    //printf("kill (%d, %d), ret=%d err='%s'\n",(int)pid, signal, ret, strerror(errno));
    kerrno=errno;
    if(signal)
        while(waitpid(pid, NULL, WNOHANG) == 0) 
        {
            usleep(1000);
            x++;
            if(x>10)
            {
                break;
            }
        }

    if (ret || kerrno)
        duk_push_false(ctx);
    else
        duk_push_true(ctx);

    return 1;
}

/**
 * Creates a directory with the name given as a path
 * @param {path} - the directory to be created
 * @param {mode=} - the mode of the newly created directory (default: 0755)
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
        RP_THROW(ctx, "mkdir(): error creating directory: %s", strerror(errno));

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
            RP_THROW(ctx, "rmdir(): error removing directory: %s", strerror(errno));

        if (recursive)
        {
            char *p;
            for (p = _path + length - 1; p != _path; p--)
            { // Traverse the path backwards to delete nested directories

                if (*p == '/')
                {

                    *p = '\0';
                    if( strcmp(".", _path)!=0 && rmdir(_path) != 0)
                        RP_THROW(ctx, "rmdir(): error removing directories recursively: %s", strerror(errno));

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
 * @param {showhidden} list ".*" files a well
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
        RP_THROW(ctx, "readdir(): could not open directory %s: %s", path, strerror(errno));

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
        RP_THROW(ctx, "readdir(): error reading directory %s: %s", path, strerror(errno));

    closedir(dir);
    return 1;
}

#define DUK_UTIL_REMOVE_FILE(ctx, file)                                                                \
    if (remove(file))                                                                                  \
        RP_THROW(ctx, "could not remove '%s': %s", file, strerror(errno));

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
        RP_THROW(ctx, "%s: source file not specified",fname);

    if (!dest_filename)
        RP_THROW(ctx, "%s: destination file not specified",fname);

    /* test if they are the same file 
    if(! testlink(ctx, src_filename, dest_filename))
        RP_THROW(ctx, "%s: error getting status '%s': %s", fname, src_filename, strerror(errno));

    (void) testlink(ctx, dest_filename, src_filename);
    */
    
    {
        FILE *dest, *src = fopen(src_filename, "r");
        struct stat src_stat, dest_stat;
        int err;        

        if (src == NULL)
        {
            RP_THROW(ctx, "%s: could not open file '%s': %s", fname, src_filename, strerror(errno));
        }

        if (stat(src_filename, &src_stat))
        {
            fclose(src);
            RP_THROW(ctx, "%s: error getting status '%s': %s", fname, src_filename, strerror(errno));
        } 

        err = stat(dest_filename, &dest_stat);
        if(!err)
        {
            if(dest_stat.st_ino == src_stat.st_ino)
                RP_THROW(ctx, "copyFile(): same file: '%s' is the same file as or a link to '%s'", src_filename, dest_filename);
        }

        if (!err && !overwrite)
        {
            // file exists and shouldn't be overwritten
            fclose(src);
            RP_THROW(ctx, "%s: error copying '%s': %s", fname, dest_filename, "file already exists");
        }
        
        if (err && errno != ENOENT)
        {
            fclose(src);
            RP_THROW(ctx, "%s: error getting status '%s': %s", fname, dest_filename, strerror(errno));
        }

        dest = fopen(dest_filename, "w");
        if (dest == NULL)
        {
            fclose(src);
            RP_THROW(ctx, "%s: could not open file '%s': %s", fname, dest_filename, strerror(errno));
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
                    RP_THROW(ctx, "%s: could not write to file '%s': %s", fname, dest_filename, strerror(errno));
                }
            }
            if (nread < 0)
            {
                fclose(src);
                fclose(dest);
                DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
                RP_THROW(ctx, "%s: error reading file '%s': %s", fname, src_filename, strerror(errno));
            }
            if (chmod(dest_filename, src_stat.st_mode))
            {
                //DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
                fclose(src);
                fclose(dest);
                RP_THROW(ctx, "%s: error setting file mode %o for '%s': %s", fname, src_stat.st_mode, dest_filename, strerror(errno));
            }
        }
        fclose(src);
        fclose(dest);
        /* check that file stats and is the same size */
        errno=0;
        if (stat(dest_filename, &dest_stat) != 0)
            RP_THROW(ctx, "%s: error getting information for '%s' after copy: %s", fname, dest_filename, strerror(errno));

        /* if src is growing, dest might be smaller now
           but src was stat'd when copy began, so dest
           should always be equal or larger than what src
           was before copy began.
           if src is truncated during copy, dest could be smaller.
           However user should be informed, so still throw error           */
        if(dest_stat.st_size < src_stat.st_size)
            RP_THROW(ctx, "%s: error copying file (partial copy) '%d' bytes copied", (int) dest_stat.st_size);
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
        RP_THROW(ctx, "rmFile(): error deleting file: %s", strerror(errno));

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
        RP_THROW(ctx, "link(): source file not specified");

    if (!target)
        RP_THROW(ctx, "link(): target name not specified");

    if (!hard)
    {
        if (symlink(src, target))
            RP_THROW(ctx, "link(): error creating symbolic link from '%s' to '%s': %s", src, target, strerror(errno));
    }
    else
    {
        if (link(src, target))
            RP_THROW(ctx, "link(): error creating hard link from '%s' to '%s': %s", src, target, strerror(errno));
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
    mode_t mode=0, old_umask=umask(0);

    if(duk_is_string(ctx,1))
    {
        char *e;
        const char *s=duk_get_string(ctx,1);

        mode=(mode_t)strtol(s,&e,8);
        if(s==e)
            RP_THROW(ctx, "chmod(): invalid mode: %s", s);
    }
    else if (duk_is_number(ctx,1))
        mode=(mode_t)duk_get_int(ctx,1);
    else
        RP_THROW(ctx, "chmod(): invalid or no mode specified");

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
                if (!fp)
                    RP_THROW(ctx, "touch(): failed to create file");
                fclose(fp);
                if ( stat(path, &filestat) != 0)
                {
                    RP_THROW(ctx, "touch(): failed to get file information");
                }
            }
        }


        if (reference)
        {

            if (stat(reference, &refrence_stat) != 0) //reference file doesn't exist
                RP_THROW(ctx, "touch(): reference file does not exist");

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
                RP_THROW(ctx, "rename(): error deleting old file: %s", strerror(errno));

            return 0;            
        }
        RP_THROW(ctx, "error renaming '%s' to '%s': %s", old, new, strerror(errno));
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
                RP_THROW(ctx, "error changing ownership (user not found): %s", strerror(errno));

            user_id = user->pw_uid;
            uid=1;
        }

        if (group_name)
        {
            struct group *grp = getgrnam(group_name);

            if (grp == NULL)
                RP_THROW(ctx, "error changing ownership (group not found): %s", strerror(errno));

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
            RP_THROW(ctx, "error changing  ownership: %s", strerror(errno));
    }
    return 0;
}

duk_ret_t duk_rp_nsleep(duk_context *ctx)
{
    double secs = REQUIRE_NUMBER(ctx, -1,  "rampart.sleep requires a number (float)");
    struct timespec stime;
    
    stime.tv_sec=(time_t)secs;
    stime.tv_nsec=(long)( 1000000000.0 * (secs - (double)stime.tv_sec) );
    nanosleep(&stime,NULL);
    return 0;
}

#define ULOCK struct utils_mlock_s

ULOCK {
    pthread_mutex_t lock;
    char *name;
};

static int nulocks=0;

static ULOCK **ulocks=NULL;

pthread_mutex_t ulock_lock;

#define LOCK_ULOCK do {\
    if (pthread_mutex_lock(&ulock_lock) == EINVAL)\
        {fprintf(stderr,"could not obtain lock for main context\n");exit(1);}\
} while(0)

#define UNLOCK_ULOCK  do{\
    pthread_mutex_unlock(&ulock_lock);\
} while(0)


#define CREATE_LOCK 0
#define NO_CREATE_LOCK 1
#define DEL_LOCK 2

//mode=0 - create lock if not found
//mode=1 - return existing or NULL
//mode=2 - mark unused if exists
pthread_mutex_t * get_lock(const char *name, int mode)
{
    int i=0, first_unused=-1;
    ULOCK *l=NULL;

    LOCK_ULOCK;
    //find existing
    while(i<nulocks)
    {
        l=ulocks[i];
        if(l->name!=NULL && !strcmp(l->name,name))
        {
            if(mode != DEL_LOCK) // if CREATE_LOCK or NO_CREATE_LOCK, return found lock
            {
                UNLOCK_ULOCK;
                return(&(l->lock));
            }
            else //if DEL_LOCK, free name, mark as unused
            {
                free(l->name);
                l->name=NULL;
                UNLOCK_ULOCK;
                return NULL;
            }
        }
        if(first_unused<0 && l->name==NULL)
            first_unused=i;
        i++;
    }
    //doesn't exist, if NO_CREATE_LOCK or DEL_LOCK, return NULL
    if(mode!=CREATE_LOCK)
    {
        UNLOCK_ULOCK;
        return NULL;
    }

    // if CREATE_LOCK:

    // there's an empty slot, use it.
    if(first_unused>-1)
    {
        l=ulocks[first_unused];
        l->name=strdup(name);
        UNLOCK_ULOCK;
        return(&(l->lock));
    }

    //mode=0 and doesn't exist, create the struct, init the lock
    nulocks++;
    REMALLOC(ulocks, nulocks * sizeof(ULOCK *) );
    l=NULL;
    REMALLOC(l,sizeof(ULOCK));
    ulocks[nulocks-1]=l;

    if (pthread_mutex_init(&(l->lock), NULL) == EINVAL)
    {
        UNLOCK_ULOCK;
        return NULL; //with CREATE_LOCK NULL=error
    }
    l->name=strdup(name);
    UNLOCK_ULOCK;
    return(&(l->lock));
}

duk_ret_t duk_rp_mlock_fin(duk_context *ctx)
{
    const char *name;
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("mlock_name"));
    name=duk_get_string(ctx, -1);
    get_lock(name, DEL_LOCK);
    return 0;
}

duk_ret_t duk_rp_mlock_destroy (duk_context *ctx)
{
    duk_push_this(ctx);

    return duk_rp_mlock_fin(ctx);
}

duk_ret_t duk_rp_mlock_constructor(duk_context *ctx)
{
    pthread_mutex_t *newlock;
    const char *lockname;

    if (!duk_is_constructor_call(ctx))
        RP_THROW(ctx, "rampart.utils.mlock is a constructor (must be called with 'new rampart.utils.mlock()')");

    lockname=REQUIRE_STRING(ctx, 0, "rampart.utils.mlock - String required as first/only parameter (lock_name)");

    duk_push_this(ctx);
    duk_push_string(ctx, lockname);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("mlock_name"));

    newlock=get_lock(lockname, CREATE_LOCK);
    
    if(!newlock)
        RP_THROW(ctx, "new rampart.utils.mlock - internal error creating lock");
    return 0;
}

duk_ret_t duk_rp_mlock_lock (duk_context *ctx)
{
    pthread_mutex_t *lock;
    const char *name;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("mlock_name"));
    name=duk_get_string(ctx, -1);
    lock = get_lock(name, NO_CREATE_LOCK);

    if(!lock)
        RP_THROW(ctx, "mlock(): error - lock already destroyed");
    if (pthread_mutex_lock(lock) == EINVAL)
        RP_THROW(ctx, "mlock(): error - could not obtain lock");
    return 0;
}

duk_ret_t duk_rp_mlock_unlock (duk_context *ctx)
{
    pthread_mutex_t *lock;
    const char *name;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("mlock_name"));
    name=duk_get_string(ctx, -1);
    lock = get_lock(name, NO_CREATE_LOCK);

    if(!lock)
        RP_THROW(ctx, "munlock(): error - lock already destroyed");
    if (pthread_mutex_unlock(lock) == EINVAL)
        RP_THROW(ctx, "munlock(): error - could not obtain lock");
    return 0;
}

/* todo put tickify in its own .c and .h file */
#define ST_NONE 0
#define ST_DQ   1
#define ST_SQ   2
#define ST_BT   3
#define ST_BS   4

char * tickify(char *src, size_t sz, int *err, int *ln);

static duk_ret_t include_js(duk_context *ctx)
{
    const char *script= REQUIRE_STRING(ctx, -1, "rampart.include: - parameter must be a String (path of script to include)" );
    const char *bfn=NULL;
    size_t slen = strlen(script);
    RPPATH rp;
    char *buffer = NULL;
    rp=rp_find_path((char*)script, "includes/");

    if(!strlen(rp.path) && strcmp(&script[slen-3], ".js") )
    {
        char jsscript[slen + 4];
        strcpy(jsscript, script);
        strcat(jsscript, ".js");
        rp=rp_find_path(jsscript, "includes/");
    }

    if(!strlen(rp.path))
    {
        RP_THROW(ctx, "could not include file %s: %s", script,  strerror(errno));
    }

    FILE *f = fopen(rp.path, "r");
    if (!f)
        RP_THROW(ctx, "Could not open %s: %s\n", rp.path, strerror(errno));

    REMALLOC(buffer, rp.stat.st_size + 1);
    slen = fread(buffer, 1, rp.stat.st_size, f);
    if (rp.stat.st_size != slen)
        RP_THROW(ctx, "Error loading file %s: %s\n", rp.path, strerror(errno));

    buffer[rp.stat.st_size]='\0';

    if (! (bfn=duk_rp_babelize(ctx, rp.path, buffer, rp.stat.st_mtime, 1)) )
    {
        /* No babel, normal compile */
        int err, lineno;
        char *isbabel = strstr(rp.path, "/babel.js");
        /* don't tickify actual babel.js source */
        if ( !(isbabel && isbabel == rp.path + strlen(rp.path) - 9) )
        {
            char *tickified = tickify(buffer, rp.stat.st_size, &err, &lineno);
            free(buffer);
            buffer = tickified;
            if (err)
            {
                char *msg="";
                switch (err) {
                    case ST_BT:
                        msg="unterminated or illegal template literal"; break;
                    case ST_SQ:
                        msg="unterminated string"; break;
                    case ST_DQ:
                        msg="unterminated string"; break;
                    case ST_BS:
                        msg="invalid escape"; break;
                }
                RP_THROW(ctx, "SyntaxError: %s (line %d)\n    at %s:%d", msg, lineno, rp.path, lineno);
            }
        }

        duk_push_string(ctx, buffer);
    }

    fclose(f);
    free(buffer);

    if(bfn)
    {
        duk_push_string(ctx, bfn);
        free((char*)bfn);
    }
    else
        duk_push_string(ctx, rp.path);
    duk_compile(ctx, DUK_COMPILE_EVAL);
    duk_call(ctx,0);
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

    //mlock
    duk_push_c_function(ctx, duk_rp_mlock_constructor, 1);
    duk_push_object(ctx);
    duk_push_c_function(ctx, duk_rp_mlock_lock, 0);
    duk_put_prop_string(ctx, -2, "lock");
    duk_push_c_function(ctx, duk_rp_mlock_unlock, 0);
    duk_put_prop_string(ctx, -2, "unlock");
    duk_push_c_function(ctx, duk_rp_mlock_destroy, 0);
    duk_put_prop_string(ctx, -2, "destroy");
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "mlock");

    if (pthread_mutex_init(&ulock_lock, NULL) == EINVAL)
    {
        RP_THROW(ctx, "rampart.utils - error initializing mlock lock");
    }

    //end mlock

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
//    duk_push_c_function(ctx, duk_rp_readln, 1);
//    duk_put_prop_string(ctx, -2, "readln");
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
    duk_push_c_function(ctx, duk_rp_realpath, 1);
    duk_put_prop_string(ctx, -2, "realPath");
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
    duk_push_c_function(ctx, duk_rp_nsleep, 1);
    duk_put_prop_string(ctx, -2, "sleep");

    /* all above are rampart.utils.xyz() functions*/
    duk_put_prop_string(ctx, -2, "utils");

    /* globalize is rampart.globalize() */
    duk_push_c_function(ctx, duk_rp_globalize,2);
    duk_put_prop_string(ctx, -2, "globalize");
    
    duk_push_c_function(ctx, include_js, 1);
    duk_put_prop_string(ctx, -2, "include");

    duk_put_global_string(ctx, "rampart");
}




void duk_misc_init(duk_context *ctx)
{
    duk_rampart_init(ctx);
    //duk_process_init(ctx);
}

/************  PRINT/READ/WRITE FUNCTIONS ***************/

char *to_utf8(const char *in_str)
{
    unsigned char *out, *buf = NULL;
    size_t len = strlen(in_str) + 1;
    unsigned const char 
            *in = (unsigned const char*) in_str,
            *five_before_end = in+len-5;

    REMALLOC(buf,len);
    out=buf;
    /* https://github.com/svaarala/duktape-wiki/pull/137/commits/3e653e3e45be930924cd4167788b1f65b414a2ac */    
    while (*in) {
        // next six bytes represent a codepoint encoded as UTF-16 surrogate pair
        if ( in < five_before_end
            && (in[0] == 0xED) 
            && (in[1] & 0xF0) == 0xA0
            && (in[2] & 0xC0) == 0x80
            && (in[3] == 0xED)
            && (in[4] & 0xF0) == 0xB0
            && (in[5] & 0xC0) == 0x80) 
        {
          // push coding parts of 6 bytes of UTF-16 surrogate pair into a 4 byte UTF-8 codepoint
          // adding 1 to in[1] adds 0x10000 to code-point that was subtracted for UTF-16 encoding
          out[0] = 0xF0 | ((in[1]+1) & 0x1C) >> 2;
          out[1] = 0x80 | ((in[1]+1) & 0x03) << 4 | (in[2] & 0x3C) >> 2;
          out[2] = 0x80 | (in[2] & 0x03) << 4 | (in[4] & 0x0F);
          out[3] = in[5];
          in += 6; out += 4; 
        } else {
          // copy anything else as is
          *out++ = *in++;
      }
    }
    *out = '\0';    
    return (char *)buf;
}

#define TO_UTF8(s) ({\
    char *ret=NULL;\
    if(s){\
        if(strchr(s,0xED)) {\
            ret=to_utf8(s);\
            REMALLOC(free_ptr, ++nfree * sizeof(char *) );\
            free_ptr[nfree-1]=ret;\
        } else ret=(char*)s;\
    }\
    ret;\
})

#define FREE_PTRS do{\
    int i=0;\
    for(;i<nfree;i++) free(free_ptr[i]);\
    free(free_ptr);\
} while (0)

#define PF_REQUIRE_STRING(ctx,idx) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "string required in format string argument %d",i);\
    }\
    const char *r=duk_get_string((ctx),i);\
    r=TO_UTF8(r);\
    r;\
})

#define PF_REQUIRE_LSTRING(ctx,idx,len) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "string required in format string argument %d",i);\
    }\
    const char *s=duk_get_lstring((ctx),i,(len));\
    const char *r=TO_UTF8(s);\
    if(r != s) *(len) = strlen(r);\
    r;\
})

#define PF_REQUIRE_BUF_OR_STRING(ctx,idx,len) ({\
    duk_idx_t i=(idx);\
    const char *s=NULL, *r=NULL;\
    if(duk_is_buffer_data((ctx), i)){\
        s=(const char *)duk_get_buffer_data((ctx),i,(len));\
        r=s;\
    } else if(duk_is_string((ctx),i)){\
        s=duk_get_lstring((ctx),i,(len));\
        r=TO_UTF8(s);\
    } else {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "string or buffer required in format string argument %d",i);\
    }\
    if(r != s) *(len) = strlen(r);\
    r;\
})

#define PF_REQUIRE_INT(ctx,idx) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "number required in format string argument %d",i);\
    }\
    int r=duk_get_int((ctx),i);\
    r;\
})


#define PF_REQUIRE_NUMBER(ctx,idx) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "number required in format string argument %d",i);\
    }\
    double r=duk_get_number((ctx),i);\
    r;\
})

#define PF_REQUIRE_BUFFER_DATA(ctx,idx,sz) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_buffer_data((ctx),i)) {\
        if(lock_p) pthread_mutex_unlock(lock_p);\
        RP_THROW(ctx, "buffer required in format string argument %d",i);\
    }\
    void *r=duk_get_buffer_data((ctx),i,(sz));\
    r;\
})

pthread_mutex_t pflock;
pthread_mutex_t pflock_err;

#include "printf.c"

/* TODO: make locking per file.  Add locking to fwrite */

duk_ret_t duk_printf(duk_context *ctx)
{
    char buffer[1];
    int ret;
    if (pthread_mutex_lock(&pflock) == EINVAL)
        RP_THROW(ctx, "printf(): error - could not obtain lock\n");

    ret = rp_printf(_out_char, buffer, (size_t)-1, ctx,0,&pflock);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;
}
#define getfh(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
        RP_THROW(ctx,"%s: argument is not a file handle",func);\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    f;\
})
#define getfh_nonull(ctx,idx,func) ({\
    FILE *f=NULL;\
    if(duk_get_prop_string(ctx, idx, "stream")){\
        const char *s=REQUIRE_STRING(ctx,-1, "error: %s({stream:\"streamName\"},...): streamName must be stdout, stderr, stdin, accessLog or errorLog", func);\
        if (!strcmp(s,"stdout")) f=stdout;\
        else if (!strcmp(s,"stderr")) f=stderr;\
        else if (!strcmp(s,"stdin")) f=stdin;\
        else if (!strcmp(s,"accessLog")) f=access_fh;\
        else if (!strcmp(s,"errorLog")) f=error_fh;\
        else RP_THROW(ctx,"error: %s({stream:\"streamName\"},...): streamName must be stdout, stderr, stdin, accessLog or errorLog", func);\
        duk_pop(ctx);\
    } else {\
        duk_pop(ctx);\
        if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
            RP_THROW(ctx,"error %s(): argument is not a file handle",func);\
        f=duk_get_pointer(ctx,-1);\
        duk_pop(ctx);\
        if(f==NULL)\
            RP_THROW(ctx,"error %s(): file handle was previously closed",func);\
    }\
    f;\
})

duk_ret_t duk_fseek(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fseek()");
    long offset=(long)REQUIRE_NUMBER(ctx, 1, "fseek(): second argument must be a number (seek position)");
    int whence=SEEK_SET;
    const char *wstr=REQUIRE_STRING(ctx,2, "fseek(): third argument must be a string (whence)");

    if(!strcasecmp(wstr,"SEEK_SET"))
        whence=SEEK_SET;
    else if(!strcasecmp(wstr,"SEEK_END"))
        whence=SEEK_END;
    else if(!strcasecmp(wstr,"SEEK_CUR"))
        whence=SEEK_CUR;
    else
        RP_THROW(ctx,"error fseek(): invalid argument '%s'",wstr);

    if(fseek(f, offset, whence))
        RP_THROW(ctx, "error fseek():'%s'", strerror(errno));

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
        RP_THROW(ctx, "error ftell():'%s'", strerror(errno));

    duk_push_number(ctx,(double)pos);
    return 1;
}

duk_ret_t duk_fread(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fread()");
    void *buf;
    size_t r, read=0, sz=4096, max=SIZE_MAX;
    int isz=-1;

    if (!duk_is_undefined(ctx,1))
    {
        int isz=REQUIRE_INT(ctx, 1, "fread(): second argument (chunk_size) must be a Number (integer)");
        if(isz > 0)
            sz=(size_t)isz;
    }

    if (!duk_is_undefined(ctx,2))
    {
        int imax = REQUIRE_INT(ctx, 2, "fread(): third argument (max_bytes) must be a Number (integer)");
        if(imax>0)
            max=(size_t)imax;
    }

    if(isz > 0)
        sz=(size_t)isz;

    buf=duk_push_dynamic_buffer(ctx, (duk_size_t)sz);

    while (1)
    {
        r=fread(buf+read,1,sz,f);
        if(ferror(f))
            RP_THROW(ctx, "error fread(): error reading file");
        read+=r;
        if (r != sz || r > max ) break;
        duk_resize_buffer(ctx, -1, read+sz);
    }

    if(read > max) read=max;
    duk_resize_buffer(ctx, -1, read);

    return (1);
}

duk_ret_t duk_fwrite(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fwrite()");
    void *buf;
    size_t wrote, 
        sz=(size_t)duk_get_number_default(ctx,2,-1);
    duk_size_t bsz;
    pthread_mutex_t *lock_p=NULL;

    duk_to_buffer(ctx,1,&bsz);
    buf=duk_get_buffer_data(ctx, 1, &bsz);
    if(sz > 0)
    {
        if((size_t)bsz < sz)
            sz=(size_t)bsz;
    }
    else sz=(size_t)bsz;

    if ( duk_get_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filelock")) )
        lock_p=duk_get_pointer(ctx,-1);
    else
        RP_THROW(ctx, "error fwrite(): no mutex lock found (this should never happen)");

    if (pthread_mutex_lock(lock_p) == EINVAL)
        RP_THROW(ctx, "fwrite(): error - could not obtain lock\n");
    wrote=fwrite(buf,1,sz,f);
    pthread_mutex_unlock(lock_p);
    
    if(wrote != sz)
        RP_THROW(ctx, "error fwrite(): error writing file");

    duk_push_number(ctx,(double)wrote);
    return(1);
}

duk_ret_t duk_fclose(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0))
    {
        RP_THROW(ctx, "error fclose(): parameter is not a filehandle object");
    }
    else
    {
        FILE *f=NULL;

        if(duk_get_prop_string(ctx, 0, "stream"))
        {
            const char *s=duk_require_string(ctx,-1);
            FILE *f=NULL;
            if (!strcmp(s,"stdout"))
                f=stdout;
            else if (!strcmp(s,"stderr"))
                f=stderr;
            else if (!strcmp(s,"stdin"))
                f=stdin;
            else if (!strcmp(s,"accessLog"))
                f=access_fh;
            else if (!strcmp(s,"errorLog"))
                f=error_fh;
            else
                RP_THROW(ctx,"error: fclose({stream:\"streamName\"},...): streamName must be stdout, stderr, stdin, accessLog or errorLog");

            fclose(f);
            return 0;
        }
        else
        {
            pthread_mutex_t *lock;

            duk_pop(ctx);
            f = getfh(ctx,0,"fclose()");
            
            duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("filelock") );
            lock=duk_get_pointer(ctx, -1);
            if (lock)
            {
                free(lock);
                duk_pop(ctx);
                duk_push_pointer(ctx,NULL);
                duk_put_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filelock") );
            } 

            if(f)
            {
                fclose(f);
                duk_push_pointer(ctx,NULL);
                duk_put_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle") );
            }
        }
    }
    return 0;
}

duk_ret_t duk_fflush(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0))
    {
        RP_THROW(ctx, "error fclose(): parameter is not a filehandle object");
    }
    else
    {
        FILE *f=NULL;

        if(duk_get_prop_string(ctx, 0, "stream"))
        {
            const char *s=duk_require_string(ctx,-1);
            FILE *f=NULL;
            if (!strcmp(s,"stdout"))
                f=stdout;
            else if (!strcmp(s,"stderr"))
                f=stderr;
            else if (!strcmp(s,"accessLog"))
                f=access_fh;
            else if (!strcmp(s,"errorLog"))
                f=error_fh;
            else
                RP_THROW(ctx,"error: fflush({stream:\"streamName\"},...): streamName must be stdout, stderr, accessLog or errorLog");

            fflush(f);
            return 0;
        }
        duk_pop(ctx);
        f = getfh_nonull(ctx,0,"fflush()");
        fflush(f);
    }
    return 0;
}

duk_ret_t duk_fopen(duk_context *ctx)
{
    FILE *f;
    pthread_mutex_t *newlock=NULL;
    const char *fn=REQUIRE_STRING(ctx,0, "fopen(): filename (String) required as first parameter");
    const char *mode=REQUIRE_STRING(ctx, 1, "fopen(): mode (String) required as second parameter");
    int mlen=strlen(mode);

    if (
        mlen > 2 || 
        (  mlen > 1 && mode[1] != '+') ||
        (*mode != 'r' && *mode != 'w' && *mode != 'a')
    )
        RP_THROW(ctx, "error opening file '%s': invalid mode '%s'", fn, mode);

    f=fopen(fn,mode);
    if(f==NULL) goto err;

    duk_push_object(ctx);
    duk_push_pointer(ctx,(void *)f);
    duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("filehandle") );

    REMALLOC(newlock, sizeof(pthread_mutex_t) );

    if (pthread_mutex_init(newlock, NULL) == EINVAL)
        RP_THROW(ctx,"could not initialize file handle lock");

    duk_push_pointer(ctx,(void *)newlock);
    duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("filelock") );

    duk_push_c_function(ctx, duk_fclose, 2);
    duk_set_finalizer(ctx, -2);

    return 1;

    err:
    RP_THROW(ctx, "error opening file '%s': %s", fn, strerror(errno));
    return 0;
}

duk_ret_t duk_fprintf(duk_context *ctx)
{
    int ret;
    const char *fn;
    FILE *out=NULL;
    int append=0;
    int closefh=1;
    pthread_mutex_t *lock_p=&pflock;

    if(duk_is_object(ctx,0))
    {
        /* special named file handles */
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
                lock_p=&pflock_err;
            }
            else if (!strcmp(s,"accessLog"))
            {
                out=access_fh;
                lock_p=&loglock;
            }
            else if (!strcmp(s,"errorLog"))
            {
                out=error_fh;
                lock_p=&errlock;
            }
            else
                RP_THROW(ctx,"error: fprintf({stream:\"streamName\"},...): streamName must be stdout, stderr, accessLog or errorLog");

            closefh=0;
            duk_pop(ctx);
            goto startprint;
        }
        duk_pop(ctx);
        /* file handles opened with javascript:fopen() */
        if ( duk_get_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle")) )
        {
            out=duk_get_pointer(ctx,-1);
            duk_pop(ctx);
            closefh=0;
            if(out==NULL)
                RP_THROW(ctx,"error: fprintf(handle,...): handle was previously closed");

            if ( duk_get_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filelock")) )
            {
                lock_p=duk_get_pointer(ctx,-1);
            }
            duk_pop(ctx);
            goto startprint;
        }
        duk_pop(ctx);

        RP_THROW(ctx,"error: fprintf(): invalid option");
    }
    else
    {
        fn=REQUIRE_STRING(ctx, 0, "fprintf(output): output must be a filehandle opened with fopen() or a String (filename)");
        if( duk_is_boolean(ctx,1) )
        {
            append=duk_get_boolean(ctx,1);
            duk_remove(ctx,1);
        }

        if(append)
        {
            if( (out=fopen(fn,"a")) == NULL )
            {
                //if( (out=fopen(fn,"w")) == NULL )
                //why would I do such a thing?
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
    if (pthread_mutex_lock(lock_p) == EINVAL)
        RP_THROW(ctx, "fprintf(): error - could not obtain lock\n");

    ret = rp_printf(_fout_char, (void*)out, (size_t)-1, ctx, 1, lock_p);
    fflush(out);
    if(closefh)
        fclose(out);
    pthread_mutex_unlock(lock_p);
    duk_push_int(ctx, ret);
    return 1;

    err:
    RP_THROW(ctx, "error opening file '%s': %s", fn, strerror(errno));
    return 0;
}

duk_ret_t duk_sprintf(duk_context *ctx)
{
    char *buffer;
    int size = rp_printf(_out_null, NULL, (size_t)-1, ctx, 0, NULL);
    buffer = malloc((size_t)size + 1);
    if (!buffer)
        RP_THROW(ctx, "malloc error in sprintf");

    (void)rp_printf(_out_buffer, buffer, (size_t)-1, ctx, 0, NULL);
    duk_push_lstring(ctx, buffer,(duk_size_t)size);
    free(buffer);
    return 1;
}

duk_ret_t duk_bprintf(duk_context *ctx)
{
    char *buffer;
    int size = rp_printf(_out_null, NULL, (size_t)-1, ctx, 0, NULL);
    buffer = (char *) duk_push_fixed_buffer(ctx, (duk_size_t)size);
    (void)rp_printf(_out_buffer, buffer, (size_t)-1, ctx, 0, NULL);
    return 1;
}

duk_ret_t duk_getType(duk_context *ctx)
{
    if (duk_is_string(ctx, 0))
        duk_push_string(ctx, "String");
    else if (duk_is_array(ctx, 0))
        duk_push_string(ctx, "Array");
    else if (duk_is_nan(ctx, 0))
        duk_push_string(ctx, "Nan");
    else if (duk_is_number(ctx, 0))
        duk_push_string(ctx, "Number");
    else if (duk_is_function(ctx, 0))
        duk_push_string(ctx, "Function");
    else if (duk_is_boolean(ctx, 0))
        duk_push_string(ctx, "Boolean");
    else if (duk_is_buffer_data(ctx, 0))
        duk_push_string(ctx, "Buffer");
    else if (duk_is_null(ctx, 0))
        duk_push_string(ctx, "Null");
    else if (duk_is_undefined(ctx, 0))
        duk_push_string(ctx, "Undefined");
    else if (duk_is_symbol(ctx, 0))
        duk_push_string(ctx, "Symbol");
    else if (duk_is_object(ctx, 0))
    {
        if(duk_has_prop_string(ctx, 0, "getMilliseconds") && duk_has_prop_string(ctx, 0, "getUTCDay") )
            duk_push_string(ctx, "Date");
        else
            duk_push_string(ctx, "Object");
    }
    else
        duk_push_string(ctx, "Unknown");

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

    duk_push_c_function(ctx, duk_fflush, 1);
    duk_put_prop_string(ctx, -2, "fflush");

    duk_push_c_function(ctx, duk_fseek, 3);
    duk_put_prop_string(ctx, -2, "fseek");

    duk_push_c_function(ctx, duk_ftell, 1);
    duk_put_prop_string(ctx, -2, "ftell");

    duk_push_c_function(ctx, duk_rewind, 1);
    duk_put_prop_string(ctx, -2, "rewind");

    duk_push_c_function(ctx, duk_fread, 3);
    duk_put_prop_string(ctx, -2, "fread");

    duk_push_c_function(ctx, duk_fwrite, 3);
    duk_put_prop_string(ctx, -2, "fwrite");

    duk_push_c_function(ctx, duk_getType, 1);
    duk_put_prop_string(ctx, -2, "getType");

    duk_push_object(ctx);
    duk_push_string(ctx,"accessLog");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"accessLog");

    duk_push_object(ctx);
    duk_push_string(ctx,"errorLog");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"errorLog");

    duk_push_object(ctx);
    duk_push_string(ctx,"stdout");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stdout");

    duk_push_object(ctx);
    duk_push_string(ctx,"stderr");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stderr");

    duk_push_object(ctx);
    duk_push_string(ctx,"stdin");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stdin");

    duk_put_prop_string(ctx, -2,"utils");
    duk_put_global_string(ctx,"rampart");

    if (pthread_mutex_init(&pflock, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize stdout print lock\n");
        exit(1);
    }
    if (pthread_mutex_init(&pflock_err, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize stderr print lock\n");
        exit(1);
    }
    if (pthread_mutex_init(&loglock, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize accessLog lock\n");
        exit(1);
    }
    if (pthread_mutex_init(&errlock, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize errorLog lock\n");
        exit(1);
    }
}
