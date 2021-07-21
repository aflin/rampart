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
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "rampart.h"
extern char **environ;
extern char *RP_script_path;
extern char *RP_script;

/* 
    defined in main program here 
    used here and in server module
*/

pthread_mutex_t loglock;
pthread_mutex_t errlock;
pthread_mutex_t pflock;
pthread_mutex_t pflock_err;
FILE *access_fh;
FILE *error_fh;
int duk_rp_server_logging=0;

#ifdef __APPLE__
int execvpe(const char *program, char **argv, char **envp)
{
    char **saved = environ;
    int rc;
    environ = envp;
    rc = execvp(program, argv);
    environ = saved;
    return rc;
}
#endif


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
    RPPATH ret={{0}};
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

duk_ret_t duk_process_setproctitle(duk_context *ctx)
{
    const char *proctitle = REQUIRE_STRING(ctx, 0, "setProcTitle: argument must be a String");
    
    if(strlen(proctitle) > 255)
        RP_THROW(ctx, "setProcTitle: String length must be less than 255 characters");
    
    setproctitle("%s", proctitle);
    return 0;
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
    duk_push_c_function(ctx,duk_process_setproctitle,1);
    duk_put_prop_string(ctx,-2,"setProcTitle");

    {   /* add process.argv */
        int i=0;
        char *s;
        char *rampart_path=getenv("RAMPART_PATH");

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

        if(RP_script)
        {
            duk_push_string(ctx,RP_script);
            duk_put_prop_string(ctx,-2,"script");

            s=strrchr(RP_script, '/');
            if(s)
            {
                duk_push_string(ctx, s+1);
                duk_put_prop_string(ctx,-2,"scriptName");
            }
            else
            {
                duk_push_string(ctx,RP_script);
                duk_put_prop_string(ctx,-2,"scriptName");
            }
        }
        else
        {
            duk_push_string(ctx,"");
            duk_put_prop_string(ctx,-2,"script");

            duk_push_string(ctx, "");
            duk_put_prop_string(ctx,-2,"scriptName");
        }

        if(rampart_path)
        {
            if(rampart_path[strlen(rampart_path)-1] != '/')
                duk_push_string(ctx, rampart_path);
            else
                duk_push_lstring(ctx, rampart_path, (duk_size_t)(strlen(rampart_path)-1) );
        }
        else
        {
            duk_push_string(ctx, rampart_dir); //set from executable path - '/bin'
        }
        duk_put_prop_string(ctx, -2, "installPath");
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

#define getfh_nonull_lock(ctx,idx,func,lock) ({\
    FILE *f=NULL;\
    lock=NULL;\
    if(duk_get_prop_string(ctx, idx, "stream")){\
        const char *s=REQUIRE_STRING(ctx,-1, "error: %s({stream:\"streamName\"},...): streamName must be stdout, stderr, stdin, accessLog or errorLog", func);\
        if (!strcmp(s,"stdout")) {f=stdout;lock=&pflock;}\
        else if (!strcmp(s,"stderr")) {f=stderr;lock=&pflock_err;}\
        else if (!strcmp(s,"stdin")) {f=stdin;lock=NULL;}\
        else if (!strcmp(s,"accessLog")) {f=access_fh;lock=&loglock;}\
        else if (!strcmp(s,"errorLog")) {f=error_fh;lock=&errlock;}\
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
    pthread_mutex_t *lock_p=NULL;
    FILE *fp=NULL;
    int64_t offset=0;
    int64_t length=0;
    duk_idx_t obj_idx=-1;
    int retstring=0;
    int close=1;
    void *buf;
    struct stat filestat;
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
/* use fread if you want to use filehandle.  This is redundant.
        if( duk_has_prop_string(ctx,obj_idx,DUK_HIDDEN_SYMBOL("filehandle")) )
        {
            fp = getfh_nonull_lock(ctx,obj_idx,"readFile",lock_p);
            close=0;
            rewind(fp);
        }
        else
*/
        {
            if(duk_get_prop_string(ctx, obj_idx, "file"))
            {
                /*
                if(duk_is_object(ctx, -1) && duk_has_prop_string(ctx,-1,DUK_HIDDEN_SYMBOL("filehandle")) )
                {
                    close=0;
                    fp = getfh_nonull_lock(ctx, -1,"readFile",lock_p);
                    rewind(fp);
                }
                else
                */
                    filename = REQUIRE_STRING(ctx, -1, "readFile() - option 'file' must be a String or filehandle");
            }
            duk_pop(ctx);
            
            if(duk_get_prop_string(ctx, obj_idx, "offset"))
                offset=(int64_t) REQUIRE_NUMBER(ctx, -1, "readFile() - option 'offset' must be a Number");
            duk_pop(ctx);

            if(duk_get_prop_string(ctx, obj_idx, "length"))
                length = (long) REQUIRE_NUMBER(ctx, -1, "readFile() - option 'length' must be a Number");
            duk_pop(ctx);
        
            if(duk_get_prop_string(ctx, obj_idx, "retString"))
                retstring=REQUIRE_BOOL(ctx,-1, "readFile() - option 'retString' must be a Boolean");
        }
    }

    if (!filename && !fp)
        RP_THROW(ctx, "readFile() - error, no file name or handle provided");

    if(fp)
    {
        if (fstat(fileno(fp), &filestat) == -1)
            RP_THROW(ctx, "readFile(\"%s\") - error accessing: %s", filename, strerror(errno));
    }
    else
    {
        if (stat(filename, &filestat) == -1)
            RP_THROW(ctx, "readFile(\"%s\") - error accessing: %s", filename, strerror(errno));
    }

    if(offset < 0)
        offset = (int64_t)filestat.st_size + offset;

    if(length < 1)
        length = ((int64_t)filestat.st_size + length) - offset;

    if( length < 1 )
        RP_THROW(ctx, "readFile(\"%s\") - negative length puts end of read before offset or start of file", filename);
    
    if(filename)
    {
        fp = fopen(filename, "r");
        if (fp == NULL)
            RP_THROW(ctx, "readFile(\"%s\") - error opening: %s", filename, strerror(errno));
    }

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

    if(!lock_p)
    {
        if (flock(fileno(fp), LOCK_SH) == -1)
            RP_THROW(ctx, "error readFile(): could not get read lock");            
    }

    while ((nbytes = fread(buf + off, 1, length - off, fp)) != 0)
    {
        off += nbytes;
    }

    if(!lock_p)
    {
        if (flock(fileno(fp), LOCK_UN) == -1)
            RP_THROW(ctx, "error readFile(): could not get read lock");
    }

    if (ferror(fp))
        RP_THROW(ctx, "readFile(\"%s\") - error reading file: %s", filename, strerror(errno));

    if(close)
        fclose(fp);

    if(retstring)
        duk_buffer_to_string(ctx,-1);    

    return 1;
}

duk_ret_t duk_rp_readln_finalizer(duk_context *ctx)
{
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer")))
    {
        FILE *fp = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        if (fp)
        {
            fclose(fp);
        }
    }
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));
    return 0;
}


//TODO: add read lock if not stdin
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
            RP_THROW(ctx, "readln(): error reading file %s: %s", duk_get_string(ctx, -1), strerror(errno));
        }

        if (nread == -1)
            duk_push_null(ctx);
        else
            duk_push_string(ctx, line);

        free(line);
        return 1;
    }
}


#define getfh(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
        RP_THROW(ctx,"%s: argument is not a file handle",func);\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    f;\
})

#define RTYPE_STDIN 0
#define RTYPE_HANDLE 1
#define RTYPE_FILENAME 2

#define getreadfile(ctx,idx,fname,filename,type) ({\
    FILE *f=NULL;\
    if(duk_is_object(ctx, idx)){\
        if(duk_get_prop_string(ctx, idx, "stream")){\
            const char *s=REQUIRE_STRING(ctx,-1, "error: readline({stream:\"streamName\"},...): streamName must be stdin");\
            if (strcmp(s,"stdin")!=0)\
                RP_THROW(ctx, "error: %s(stream) - must be stdin\n",fname);\
            filename="stdin";\
            f=stdin;\
            type=RTYPE_STDIN;\
        } else {\
            f=getfh(ctx, idx, fname);\
            if(!f)\
                RP_THROW(ctx, "%s - first argument must be a string, filehandle or rampart.utils.stdin", fname);\
            type=RTYPE_HANDLE;\
        }\
        duk_pop(ctx);\
    } else {\
        filename = REQUIRE_STRING(ctx, idx, "%s - first argument must be a string or rampart.utils.stdin", fname);\
        f = fopen(filename, "r");\
        if (f == NULL)\
            RP_THROW(ctx, "%s: error opening '%s': %s", fname, filename, strerror(errno));\
        type=RTYPE_FILENAME;\
    }\
    f;\
})

duk_ret_t duk_rp_readline(duk_context *ctx)
{
    const char *filename="";
    FILE *f;
    int type;

    f=getreadfile(ctx, 0, "readLine", filename, type);

    duk_push_object(ctx);

    duk_push_string(ctx,filename);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filename"));

    duk_push_pointer(ctx,f);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));

    duk_push_c_function(ctx,readline_next,0);
    duk_put_prop_string(ctx, -2, "next");

    if(type==RTYPE_FILENAME)
    {
        duk_push_c_function(ctx,duk_rp_readln_finalizer,1);
        duk_set_finalizer(ctx, -2);
    }

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
    int kill_signal=SIGTERM, background=0, i=0, len=0, append=0;
    unsigned int timeout=0;
    const char *path, *stdin_txt=NULL;
    duk_size_t stdin_sz;
    char **args=NULL, **env=NULL;
    duk_size_t nargs;
    pid_t pid, pid2;
    int exit_status;
    int stdout_pipe[2];
    int stderr_pipe[2];
    int stdin_pipe[2];
    int child2par[2];

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

/*
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
*/
    
    if(duk_get_prop_string(ctx, -1, "appendEnv") && duk_get_boolean_default(ctx,-1,0) )
        append=1;
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "env"))
    {
        int start=len;
        if(!duk_is_object(ctx, -1) || duk_is_function(ctx, -1) || duk_is_array(ctx, -1))
        {
            if(env)
                free(env);
            RP_THROW(ctx, "exec(): option 'env' must be an object");
        }

        if(append)
        {
            duk_get_global_string(ctx, "Object");  // [env_arg, "Object" ]
            duk_push_string(ctx, "assign");        // [env_arg, "Object", "assign" ]
            duk_push_object(ctx);                  // [env_arg, "Object", "assign", dest_obj ]
            duk_get_global_string(ctx, "process"); // [env_arg, "Object", "assign", dest_obj, "process" ]
            duk_get_prop_string(ctx, -1, "env");   // [env_arg, "Object", "assign", dest_obj,  "process", curenv ]
            duk_remove(ctx, -2);                   // [env_arg, "Object", "assign", dest_obj, curenv ]
            duk_pull(ctx, -5);                     // ["Object", "assign" dest_obj, curenv, env_arg ]
            duk_call_prop(ctx, -5, 3);             // ["Object", retobj ]
            duk_remove(ctx, -2);                   // [ retobj ]
        }

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
    else
    //if background, only need one pipe to get the pid after the double fork
    {
        if (pipe(child2par) == -1  ||  pipe(stdin_pipe) == -1)
        {
            free(args);
            if(env)
                free(env);
            RP_THROW(ctx, "exec(): could not create pipe: %s", strerror(errno));
        }
    
    }

    if ((pid = fork()) == -1)
    {
        free(args);
        if(env)
            free(env);
        RP_THROW(ctx, "exec(): could not fork: %s", strerror(errno));
    }
    else if (pid == 0)
    {
        //child
        if (background)
        {
            //double fork and return pid to parent
            close(child2par[0]);
            if (setsid()==-1) {
                pid2=-1;
                if(-1 == write(child2par[1], &pid2, sizeof(pid_t)) )
                    fprintf(error_fh, "exec(): failed to send setsid error to parent\n");
                exit(1);
            }
            pid2=fork();
            //write pid2
            if(pid2) //if -1 or pid of child
            {
                //error or first child
                free(args);
                if(env)
                    free(env);
                if(-1 == write(child2par[1], &pid2, sizeof(pid_t)) )
                {
                    fprintf(error_fh, "exec(): failed to send pid to parent\n");
                    pid2=-1;
                }

                if(stdin_txt)
                {
                    if( -1 == write(stdin_pipe[1], stdin_txt, (size_t)stdin_sz))
                    {
                        fprintf(error_fh, "exec(): failed to write to stdin of command %s\n", strerror(errno));
                        exit(1);
                    }
                }

                close(stdin_pipe[1]);
                close(child2par[1]);
                exit((pid2<0?1:0));
            }
            //grandchild from here on
            close(child2par[1]);
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);
        }
        else
        {
            // make pipe equivalent to stdout and stderr
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            // close unused pipes
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            close(stdin_pipe[1]);
        }
        // stdin for child, or grandchild if double fork
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[1]);

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

    if (background)
    {
        // return object
        duk_push_object(ctx);
        close(child2par[1]);
        waitpid(pid,&exit_status,0);
        if(-1 == read(child2par[0], &pid2, sizeof(pid_t)) )
            RP_THROW(ctx, "exec(): failed to get pid from child");

        close(child2par[0]);
        close(stdin_pipe[1]);
        close(stdin_pipe[0]);
        DUK_PUT(ctx, int, "pid", pid2, -2);

        // set stderr and stdout to null
        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "stderr");
        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "stdout");
        duk_push_int(ctx, (int) exit_status);
        duk_put_prop_string(ctx, -2, "exitStatus");

        // set timed_out to false
        DUK_PUT(ctx, boolean, "timedOut", 0, -2);
    }
    else
    {
        int stdout_nread, stderr_nread;
        char *stdout_buf = NULL;
        char *stderr_buf = NULL;
        // create thread for timeout
        struct exec_thread_waitpid_arg arg;
        pthread_t thread;

        if (timeout > 0)
        {
            arg.signal = kill_signal;
            arg.pid = pid;
            arg.timeout = timeout;
            arg.killed = 0;
            pthread_create(&thread, NULL, duk_rp_exec_thread_waitpid, &arg);
        }

        // close unused pipes
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        close(stdin_pipe[0]);

        if(stdin_txt)
        {
            if(-1 == write(stdin_pipe[1], stdin_txt, (size_t)stdin_sz))
            {
                close(stdin_pipe[1]);
                RP_THROW(ctx, "exec(): could not write to stdin of command: %s", strerror(errno));
            }
            close(stdin_pipe[1]);
        }

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
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        close(stdin_pipe[1]);
    }
    if(env)
        free(env);
    free(args);
    return 1;
}

duk_ret_t duk_rp_getcwd(duk_context *ctx)
{
    char * cwd = getcwd(NULL, 0);

    if(!cwd)
        RP_THROW(ctx, "getcwd(): error - %s", strerror(errno));
    duk_push_string(ctx, cwd);
    free(cwd);
    return 1;
}

duk_ret_t duk_rp_chdir(duk_context *ctx)
{
    const char * d = REQUIRE_STRING(ctx, 0, "chdir(): argument must be a string (directory)");

    if(chdir(d))
        RP_THROW(ctx, "chdir(): error - %s", strerror(errno));

    return 0;
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
    if (pthread_mutex_lock(&ulock_lock) != 0)\
        {fprintf(stderr,"could not obtain lock for mlock\n");exit(1);}\
} while(0)

#define UNLOCK_ULOCK  do{\
    if (pthread_mutex_unlock(&ulock_lock) != 0)\
        {fprintf(stderr,"could not release lock for mlock\n");exit(1);}\
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
    if (pthread_mutex_lock(lock) != 0)
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
    if (pthread_mutex_unlock(lock) != 0)
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

#include "cityhash.h"
#include "fast_random.h"
#include "murmurhash.h"
#include "hyperloglog.h"
#define HASH_TYPE_CITY64 0
#define HASH_TYPE_CITY   1
#define HASH_TYPE_MURMUR 2
#define HASH_TYPE_BOTH 3


static uint64_t ntoh64(const uint64_t input)
{
    uint64_t rval;
    uint8_t *data = (uint8_t *)&rval;

    data[0] = input >> 56;
    data[1] = input >> 48;
    data[2] = input >> 40;
    data[3] = input >> 32;
    data[4] = input >> 24;
    data[5] = input >> 16;
    data[6] = input >> 8;
    data[7] = input >> 0;

    return rval;
}


static inline void hash_one(duk_context *ctx, duk_idx_t idx, int type, void *map, size_t sz)
{
    duk_size_t insz;
    const char *inbuf;
    void *buf;

    if(map)
    {
        inbuf =(const char *)map;
        insz = (duk_size_t)sz;    
    }
    else
        inbuf = REQUIRE_STR_OR_BUF(ctx, idx, &insz, "rampart.utils.hash - input must be a string or buffer");

    switch(type)
    {
        case HASH_TYPE_CITY64:
        {
            uint64 h = ntoh64(CityHash64(inbuf, (size_t)insz));

            buf = duk_push_fixed_buffer(ctx, 8);

            memcpy(buf, &h, 8);
            break;
        }
        case HASH_TYPE_CITY:
        {
            uint128 h = CityHash128(inbuf, (size_t)insz);
            Uint128High64(h) = ntoh64( Uint128High64(h) );
            Uint128Low64(h) = ntoh64( Uint128Low64(h) );
            
            buf = duk_push_fixed_buffer(ctx, 16);
            memcpy(buf, &(Uint128High64(h)), 8);
            memcpy(buf+8, &(Uint128Low64(h)), 8);
            break;
        }
        case HASH_TYPE_MURMUR:
        {
            uint64_t h = ntoh64(MurmurHash64( (const void *) inbuf, (int) insz));
            
            buf = duk_push_fixed_buffer(ctx, 8);

            memcpy(buf, &h, 8);
            break;
        }
        case HASH_TYPE_BOTH:
        {
            uint128 h = CityHash128(inbuf, (size_t)insz);
            uint64_t h2 = ntoh64(MurmurHash64( (const void *) inbuf, (int) insz));
            
            Uint128High64(h) = ntoh64( Uint128High64(h) );
            Uint128Low64(h) = ntoh64( Uint128Low64(h) );
            buf = duk_push_fixed_buffer(ctx, 24);
            memcpy(buf, &(Uint128High64(h)), 8);
            memcpy(buf+8, &(Uint128Low64(h)), 8);
            memcpy(buf+16, &h2, 8);
            break;
        }
    }
}

static duk_ret_t _hash(duk_context *ctx, void *map, size_t sz)
{
    int type=HASH_TYPE_CITY64;
    duk_idx_t val_idx=-1, opts_idx=-1;
    int hexconv=1;

    if(duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0))
        opts_idx=0;
    else if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
        opts_idx=1;

    if(opts_idx > -1)
    {
        const char *type_name=NULL;
        if(duk_get_prop_string(ctx, opts_idx, "type"))
        {
            type_name=REQUIRE_STRING(ctx, -1, "hash() - option 'type' must be a String");
        }
        duk_pop(ctx);
        if(duk_get_prop_string(ctx, opts_idx, "function"))
        {
            type_name=REQUIRE_STRING(ctx, -1, "hash() - option 'function' must be a String");
        }
        duk_pop(ctx);

        if(type_name)
        {
            if(!strcmp(type_name,"city128"))
                type=HASH_TYPE_CITY;
            else if(!strcmp(type_name,"murmur"))
                type=HASH_TYPE_MURMUR;
            else if(!strcmp(type_name,"both"))
                type=HASH_TYPE_BOTH;
            else if(strcmp(type_name,"city"))
                RP_THROW(ctx, "hash() - unknown value for option 'type' ('%s')", type_name);
            //else is city64
        }

        if(duk_get_prop_string(ctx, opts_idx, "returnBuffer") && duk_get_boolean_default(ctx, -1, 0))
        {
            hexconv=0;
        }
        duk_pop(ctx);
    }

    val_idx = !opts_idx; //0 or 1

    if(map)
        hash_one(ctx, val_idx, type, map, sz);
    else if(duk_is_array(ctx, val_idx))
    {
        duk_uarridx_t i=0, len=duk_get_length(ctx, val_idx);
        
        duk_push_array(ctx);
        while(i<len)
        {
            duk_get_prop_index(ctx, val_idx, i);
            hash_one(ctx, -1, type, NULL, 0);
            if(hexconv)
                duk_rp_toHex(ctx, -1, 0);
            duk_put_prop_index(ctx, -3, i);
            duk_pop(ctx);
            i++;
        }
        hexconv=0;
    }
    else
        hash_one(ctx, val_idx, type, NULL, 0);
    
    if(hexconv)
    {
        duk_rp_toHex(ctx, -1, 0);
    }
    return 1;
}

duk_ret_t duk_rp_hash(duk_context *ctx)
{
    return _hash(ctx, NULL, 0);
}

duk_ret_t duk_rp_hash_file(duk_context *ctx)
{
    void *map;
    duk_idx_t val_idx=-1, opts_idx=-1;
    const char *fn;
    struct stat sb;
    int fd;


    if(duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0))
        opts_idx=0;
    else if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
        opts_idx=1;
    
    val_idx = !opts_idx;
    fn = REQUIRE_STRING(ctx, val_idx, "hashFile() - argument (filename) must be a string");
    fd = open(fn, O_RDONLY);
    if (fd == -1)
        RP_THROW(ctx, "hashFile() - could not open file '%s' - %s", fn, strerror(errno));

    if (fstat(fd, &sb) == -1)
        RP_THROW(ctx, "hashFile() - could not get stats of file '%s' - %s", fn, strerror(errno));    
    
    map = mmap(NULL, sb.st_size,  PROT_READ, MAP_PRIVATE, fd, 0);

    duk_ret_t ret = _hash(ctx, map, sb.st_size);

    munmap(map, sb.st_size);

    return ret;
}

/* make sure when we use RAND_ functions, we've seeded at least once */
static int seeded=0;
void checkseed(duk_context *ctx)
{
    if(!seeded)
    {
        int rd;

        errno=0;
        rd = open("/dev/urandom", O_RDONLY);
        if (rd < 0)
            RP_THROW(ctx, "error opening data from /dev/urandom - '%s'",strerror(errno));
        else
        {
            uint64_t rc;
            ssize_t result = read(rd, &rc, sizeof(uint64_t));
            if (result != sizeof(uint64_t))
                RP_THROW(ctx, "error reading data from /dev/urandom - '%s'",strerror(errno));
            xorRand64Seed(rc);
        }
        seeded=1;
    }
}

duk_ret_t duk_rp_srand(duk_context *ctx)
{
    if(!duk_is_undefined(ctx,0))
    {
        uint64_t t = (uint64_t) fabs(REQUIRE_NUMBER(ctx, 0, "srand() - first argument must be a number (seed)"));
        xorRand64Seed(t);        
        seeded=1;
        return 0;
    }
    seeded=0;
    checkseed(ctx);
    return 0;
}

duk_ret_t duk_rp_rand(duk_context *ctx)
{
    uint64_t r;
    double max=1.0, min=0.0;

    if(!duk_is_undefined(ctx,0))
    {
        double t = REQUIRE_NUMBER(ctx, 0, "rand() - first argument must be a number");

        if(duk_is_undefined(ctx,1))
            max=t;
        else
            min=t;
    }

    if(!duk_is_undefined(ctx,1))
        max = REQUIRE_NUMBER(ctx, 1, "rand() - second argument must be a number (max)");

    checkseed(ctx);
    r=xorRand64();
    duk_push_number(ctx, (((double)r/(double)UINT64_MAX) * (max - min) + min) );

    return 1;
}

#define HLL struct utils_hll_s

HLL {
    unsigned char * buf;
    char *name;
    int refcount;
    pthread_mutex_t lock;
    duk_context *ctx;
    HLL *next;
};

HLL *hll_list = NULL;

pthread_mutex_t hll_lock;

#define HLL_MAIN_LOCK RP_MLOCK(&hll_lock)
#define HLL_MAIN_UNLOCK RP_MUNLOCK(&hll_lock)


#define HLL_LOCK(h) RP_MLOCK(&((h)->lock))
#define HLL_UNLOCK(h) RP_MUNLOCK(&((h)->lock))


static inline HLL *newhll(duk_context *ctx, const char *name)
{
    HLL *hll=NULL;
    REMALLOC(hll, sizeof(HLL));
    CALLOC(hll->buf,16384);
    hll->name=strdup(name);
    hll->refcount=1;
    hll->ctx=ctx;

    /* throw or die?? */
    if (pthread_mutex_init(&hll->lock, NULL) == EINVAL)
    {
        RP_THROW(ctx, "rampart.utils - error initializing hll lock");
    }

    hll->next=NULL;
    return hll;
}

// updateref +1 or -1 for existing hll only
static HLL *gethll(duk_context *ctx, const char *name, int updateref)
{
    HLL *hll=NULL, *lasthll=NULL;

    HLL_MAIN_LOCK;
    if(!hll_list)
    {
        if(!updateref) //this should not happen ever
            RP_THROW(ctx, "hll() - internal error getting hll list bufer");
        hll=hll_list=newhll(ctx, name);
    }
    else
    {
        // find our named hll, if exists
        hll=hll_list;
        while(hll)
        {
            if(!strcmp(hll->name, name))
                break;
            lasthll=hll;
            hll = hll->next;
        }
        //doesn't exist. Create it.
        if(!hll)
        {
            if(!updateref) //this should not happen ever
                RP_THROW(ctx, "hll() - internal error getting hll bufer");
            hll=newhll(ctx, name);
            lasthll->next=hll;
        }
        else if(updateref)
        {
            // if using a copy from another context, add one to ref count
            if(!updateref && hll->ctx != ctx)
            {
                hll->ctx = ctx;
                updateref=1;
            }
            // if new, or destroy, or copied from another context
            if(updateref)
            {
                hll->refcount += updateref;
                if(!hll->refcount)
                {
                    free(hll->buf);
                    free(hll->name);
                    if(lasthll)
                        lasthll->next = hll->next;
                    else //hll==hll_list
                        hll_list=NULL;
                    free(hll);
                    hll=NULL;
                }
            }
        }
    }        
    HLL_MAIN_UNLOCK;
    return hll;
}

static duk_ret_t _hll_destroy(duk_context *ctx)
{
    const char *hllname;

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllname"));
    hllname=duk_get_string(ctx, -1);
    (void)gethll(ctx, hllname, -1);
    return 0;
}

duk_ret_t duk_rp_hll_addfile(duk_context *ctx)
{
    HLL *hll=NULL;
    char delim='\n';
    FILE *f;
    const char *filename="";
    int type;
    char *line = NULL;
    size_t len = 0;
    int nread;

    f=getreadfile(ctx, 0, "addFile", filename, type);

    if(!duk_is_undefined(ctx, 1))
    {
        const char *d = REQUIRE_STRING(ctx, 1, "addfile - second argument must be a string (first char is delimiter)");
        delim=*d;
    }

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllbuf"));
    hll = (HLL *) duk_get_pointer(ctx, -1);

    if(!hll)
        RP_THROW(ctx, "hll - could not retrieve buffer");

    while(1)
    {
        errno = 0;
        nread = getdelim(&line, &len, delim, f);
        if (errno)
        {
            free(line);
            duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filename"));
            RP_THROW(ctx, "addFile(): error reading file %s: %s", duk_get_string(ctx, -1), strerror(errno));
        }

        if (nread == -1)
            break;
        else
        {
            if(line[nread-1]==delim)
                nread--;
            HLL_LOCK(hll);
            addHLL16K(hll->buf, CityHash64(line,(size_t)nread) );
            HLL_UNLOCK(hll);
            // just looking:
            //addHLL16K(hll->buf, MurmurHash64( (const void *) line, (int) nread) );
        }
    }

    free(line);

    if(type==RTYPE_FILENAME)
        fclose(f);

    duk_push_this(ctx);
    return 1;

}
duk_ret_t duk_rp_hll_getbuffer(duk_context *ctx)
{
    HLL *hll=NULL;
    void *buf;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllbuf"));
    hll = (HLL *) duk_get_pointer(ctx, -1);

    buf=duk_push_fixed_buffer(ctx, 16384);
    memcpy(buf, hll->buf, 16384);
    return 1;
}

duk_ret_t duk_rp_hll_add(duk_context *ctx)
{
    duk_size_t sz;
    const char *in;
    HLL *hll=NULL;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllbuf"));
    hll = (HLL *) duk_get_pointer(ctx, -1);

    if(duk_is_array(ctx, 0))
    {
        duk_uarridx_t i=0, len=duk_get_length(ctx, 0);
        
        while(i<len)
        {
            duk_get_prop_index(ctx, 0, i);
            in = REQUIRE_STR_OR_BUF(ctx, -1, &sz, "hll.add() - input must be a buffer, string or an array of strings/buffers");
            HLL_LOCK(hll);
            addHLL16K(hll->buf, CityHash64(in,(size_t)sz) );
            HLL_UNLOCK(hll);
            duk_pop(ctx);
            i++;
        }
    }
    else
    {
        in = REQUIRE_STR_OR_BUF(ctx, 0, &sz, "hll.add() - input must be a buffer, string or an array of strings/buffers");
        HLL_LOCK(hll);
        addHLL16K(hll->buf, CityHash64(in,(size_t)sz) );
        HLL_UNLOCK(hll);
    }

    duk_push_this(ctx);
    return 1;
}

duk_ret_t duk_rp_hll_count(duk_context *ctx)
{
    HLL *hll=NULL;
    double c;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllbuf"));
    hll = (HLL *) duk_get_pointer(ctx, -1);
    HLL_LOCK(hll);
    c=(double)countHLL16k(hll->buf);
    HLL_UNLOCK(hll);
    duk_push_int(ctx, (int)c);
    return 1;
}

//assumes stack starts with the hlls to merge, up to top
static void _merge(duk_context *ctx, duk_idx_t cur, duk_idx_t top, HLL *hll)
{
    HLL *hll2=NULL;

    while (cur<top)
    {
        if(!duk_is_object(ctx, cur) || !duk_get_prop_string(ctx, cur, DUK_HIDDEN_SYMBOL("hllbuf")) )
            RP_THROW(ctx, "hll.merge() - argument must be another hll object");

        hll2 = (HLL *)duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        HLL_LOCK(hll);
        if(hll!=hll2)
            HLL_LOCK(hll2);

        mergeHLL16K(hll->buf, hll->buf, hll2->buf);
        if(hll!=hll2)
            HLL_UNLOCK(hll2);
        HLL_UNLOCK(hll);
        cur++;
    }
}

duk_ret_t duk_rp_hll_merge(duk_context *ctx)
{
    HLL *hll=NULL;
    duk_idx_t top;

    top=duk_get_top(ctx);
    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("hllbuf"));
    hll = (HLL *) duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    _merge(ctx, 0, top, hll);

//    duk_pull(ctx, top);//this
    return 1;
}

duk_ret_t duk_rp_hll_constructor(duk_context *ctx)
{
    const char *hllname;
    HLL *hll=NULL;
    duk_idx_t top;

    if (!duk_is_constructor_call(ctx))
        RP_THROW(ctx, "rampart.utils.hll is a constructor (must be called with 'new rampart.utils.hll()')");

    hllname=REQUIRE_STRING(ctx, 0, "hll() - first argument must be a string (name of the hll)");    

    hll=gethll(ctx, hllname, 1);
    
    duk_push_this(ctx);
    duk_push_pointer(ctx, (void*)hll);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("hllbuf"));
    duk_push_string(ctx, hllname);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("hllname"));
    duk_push_c_function(ctx, _hll_destroy, 1);
    duk_set_finalizer(ctx, -2);
    duk_pop(ctx);

    if(duk_is_buffer_data(ctx, 1))
    {
        duk_size_t sz;
        void *buf = duk_get_buffer_data(ctx, 1, &sz);
        if(sz !=16384)
            RP_THROW(ctx, "new hll(): error - buffer must be 16384 bytes in length");
        memcpy(hll->buf, buf, (size_t)sz);
        duk_remove(ctx, 1); 
    }

    top=duk_get_top(ctx);

    if(top>1)
        _merge(ctx, 1, top, hll);

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

    RP_MINIT(&ulock_lock);

    //end mlock

    // hll
    duk_push_c_function(ctx, duk_rp_hll_constructor, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, duk_rp_hll_add, 1);
    duk_put_prop_string(ctx, -2, "add");
    duk_push_c_function(ctx, duk_rp_hll_addfile, 2);
    duk_put_prop_string(ctx, -2, "addFile");
    duk_push_c_function(ctx, duk_rp_hll_getbuffer, 0);
    duk_put_prop_string(ctx, -2, "getBuffer");
    duk_push_c_function(ctx, duk_rp_hll_count, 0);
    duk_put_prop_string(ctx, -2, "count");
    duk_push_c_function(ctx, duk_rp_hll_merge, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "merge");
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "hll");

    RP_MINIT(&hll_lock);
    // end hll


    duk_push_c_function(ctx, duk_rp_srand, 1);
    duk_put_prop_string(ctx, -2, "srand");
    duk_push_c_function(ctx, duk_rp_rand, 2);
    duk_put_prop_string(ctx, -2, "rand");
    duk_push_c_function(ctx, duk_rp_hash, 2);
    duk_put_prop_string(ctx, -2, "hash");
    duk_push_c_function(ctx, duk_rp_hash_file, 2);
    duk_put_prop_string(ctx, -2, "hashFile");
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
    duk_push_c_function(ctx, duk_rp_mkdir, 2);
    duk_put_prop_string(ctx, -2, "mkDir");
    duk_push_c_function(ctx, duk_rp_rmdir, 2);
    duk_put_prop_string(ctx, -2, "rmdir");
    duk_push_c_function(ctx, duk_rp_rmdir, 2);
    duk_put_prop_string(ctx, -2, "rmDir");
    duk_push_c_function(ctx, duk_rp_chdir, 1);
    duk_put_prop_string(ctx, -2, "chdir");
    duk_push_c_function(ctx, duk_rp_chdir, 1);
    duk_put_prop_string(ctx, -2, "chDir");
    duk_push_c_function(ctx, duk_rp_getcwd, 0);
    duk_put_prop_string(ctx, -2, "getcwd");
    duk_push_c_function(ctx, duk_rp_getcwd, 0);
    duk_put_prop_string(ctx, -2, "getCwd");
    duk_push_c_function(ctx, duk_rp_readdir, 2);
    duk_put_prop_string(ctx, -2, "readdir");
    duk_push_c_function(ctx, duk_rp_readdir, 2);
    duk_put_prop_string(ctx, -2, "readDir");
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
        if(lock_p) RP_MUNLOCK(lock_p);\
        RP_THROW(ctx, "string required in format string argument %d",i);\
    }\
    const char *r=duk_get_string((ctx),i);\
    r=TO_UTF8(r);\
    r;\
})

#define PF_REQUIRE_LSTRING(ctx,idx,len) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        if(lock_p) RP_MUNLOCK(lock_p);\
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
        if(lock_p) RP_MUNLOCK(lock_p);\
        RP_THROW(ctx, "string or buffer required in format string argument %d",i);\
    }\
    if(r != s) *(len) = strlen(r);\
    r;\
})

#define PF_REQUIRE_INT(ctx,idx) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        if(lock_p) RP_MUNLOCK(lock_p);\
        RP_THROW(ctx, "number required in format string argument %d",i);\
    }\
    int r=duk_get_int((ctx),i);\
    r;\
})


#define PF_REQUIRE_NUMBER(ctx,idx) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        if(lock_p) RP_MUNLOCK(lock_p);\
        RP_THROW(ctx, "number required in format string argument %d",i);\
    }\
    double r=duk_get_number((ctx),i);\
    r;\
})

#define PF_REQUIRE_BUFFER_DATA(ctx,idx,sz) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_buffer_data((ctx),i)) {\
        if(lock_p) RP_MUNLOCK(lock_p);\
        RP_THROW(ctx, "buffer required in format string argument %d",i);\
    }\
    void *r=duk_get_buffer_data((ctx),i,(sz));\
    r;\
})


#include "printf.c"

/* TODO: make locking per file.  Add locking to fwrite */

duk_ret_t duk_rp_printf(duk_context *ctx)
{
    char buffer[1];
    int ret;

    if (pthread_mutex_lock(&pflock) != 0)
        RP_THROW(ctx, "printf(): error - could not obtain lock\n");

    ret = rp_printf(_out_char, buffer, (size_t)-1, ctx,0,&pflock);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;
}


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

duk_ret_t duk_rp_fseek(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fseek()");
    long offset=(long)REQUIRE_NUMBER(ctx, 1, "fseek(): second argument must be a number (seek position)");
    int whence=SEEK_SET;

    if(!duk_is_undefined(ctx,2))
    {
        const char *wstr=REQUIRE_STRING(ctx,2, "fseek(): third argument must be a string (whence)");

        if(!strcasecmp(wstr,"SEEK_SET"))
            whence=SEEK_SET;
        else if(!strcasecmp(wstr,"SEEK_END"))
            whence=SEEK_END;
        else if(!strcasecmp(wstr,"SEEK_CUR"))
            whence=SEEK_CUR;
        else
            RP_THROW(ctx,"error fseek(): invalid argument '%s'",wstr);
    }
    if(fseek(f, offset, whence))
        RP_THROW(ctx, "error fseek():'%s'", strerror(errno));

    return 0;
}

duk_ret_t duk_rp_rewind(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"rewind()");
    rewind(f);
    return 0;
}

duk_ret_t duk_rp_ftell(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"ftell()");
    long pos;

    pos=ftell(f);
    if(pos==-1)
        RP_THROW(ctx, "error ftell():'%s'", strerror(errno));

    duk_push_number(ctx,(double)pos);
    return 1;
}

duk_ret_t duk_rp_fread(duk_context *ctx)
{
    FILE *f = NULL;
    void *buf;
    size_t r, read=0, sz=4096, max=SIZE_MAX;
    int isz=-1;
    const char *filename="";
    duk_idx_t idx=1;
    int type, retstr=0;;

    f=getreadfile(ctx, 0, "fread", filename, type);

    /* check for boolean in idx 1,2 or 3 */
    if(duk_is_boolean(ctx, idx) || duk_is_boolean(ctx, ++idx) || duk_is_boolean(ctx, ++idx))
    {
        retstr=duk_get_boolean(ctx, idx);
        duk_remove(ctx, idx);
    }

    if (!duk_is_undefined(ctx,1))
    {
        int imax = REQUIRE_INT(ctx, 1, "fread(): argument max_bytes must be a Number (positive integer)");
        if(imax>0)
            max=(size_t)imax;
    }

    if (!duk_is_undefined(ctx,2))
    {
        int isz=REQUIRE_INT(ctx, 2, "fread(): argument chunk_size must be a Number (positive integer)");
        if(isz > 0)
            sz=(size_t)isz;
    }

    if(isz > 0)
        sz=(size_t)isz;

    buf=duk_push_dynamic_buffer(ctx, (duk_size_t)sz);

    if(type!=RTYPE_STDIN)
    {
        if (flock(fileno(f), LOCK_SH) == -1)
            RP_THROW(ctx, "error fread(): could not get read lock");            
    }

    while (1)
    {
        r=fread(buf+read,1,sz,f);
        if(ferror(f))
            RP_THROW(ctx, "error fread(): error reading file");
        read+=r;
        if (r != sz || r > max ) break;
        buf = duk_resize_buffer(ctx, -1, read+sz);
    }

    if(type!=RTYPE_STDIN)
    {
        if (flock(fileno(f), LOCK_UN) == -1)
            RP_THROW(ctx, "error fread(): could not get read lock");
    }

    if(read > max) read=max;
    duk_resize_buffer(ctx, -1, read);

    if(retstr)
        duk_buffer_to_string(ctx, -1);    

    if(type==RTYPE_FILENAME)
        fclose(f);

    return (1);
}

duk_ret_t duk_rp_fwrite(duk_context *ctx)
{
    pthread_mutex_t *lock_p=NULL;
    FILE *f;
    void *buf;
    size_t wrote, sz;
    duk_size_t bsz;
    int closefh=1;
    int append=0;

    if(duk_is_object(ctx,0))
    {
        f = getfh_nonull_lock(ctx,0,"fprintf()",lock_p);
        closefh=0;
    }
    else
    {
        const char *fn=REQUIRE_STRING(ctx, 0, "fwrite(): output must be a filehandle opened with fopen() or a String (filename)");
        duk_idx_t idx=2;
        /* get boolean at stack pos 2 or 3 */
        if( duk_is_boolean(ctx, idx) || duk_is_boolean(ctx, ++idx) )
        {
            append=duk_get_boolean(ctx, idx);
            duk_remove(ctx, idx);
        }

        if(append)
        {
            if( (f=fopen(fn,"a")) == NULL )
                RP_THROW(ctx, "fwrite(): error opening file '%s': %s", fn, strerror(errno));
        }
        else
        {
            if( (f=fopen(fn,"w")) == NULL )
                RP_THROW(ctx, "fwrite(): error opening file '%s': %s", fn, strerror(errno));
        }
    }

    buf = (void *) REQUIRE_STR_OR_BUF(ctx, 1, &bsz, "fwrite(): error - data must be a String or Buffer" );

    sz=(size_t)duk_get_number_default(ctx,2,-1);

    if(sz > 0)
    {
        if((size_t)bsz < sz)
            sz=(size_t)bsz;
    }
    else sz=(size_t)bsz;

    if(!lock_p)
    {
        if (flock(fileno(f), LOCK_EX) == -1)
            RP_THROW(ctx, "fwrite(): error - could not obtain lock");
    }
    else
    {
        if (pthread_mutex_lock(lock_p) != 0)
            RP_THROW(ctx, "fwrite(): error - could not obtain lock");
    }

    wrote=fwrite(buf,1,sz,f);

    if(!lock_p)
    {
        if (flock(fileno(f), LOCK_UN) == -1)
            RP_THROW(ctx, "fwrite(): error - could not release lock");
    }
    else
    {
        if (pthread_mutex_unlock(lock_p) != 0)
          RP_THROW(ctx, "fwrite(): error - could not release lock");
    }

    if(closefh)
        fclose(f);

    if(wrote != sz)
        RP_THROW(ctx, "fwrite(): error writing file (wrote %d of %d bytes)", wrote, sz);

    duk_push_number(ctx,(double)wrote);
    return(1);
}

duk_ret_t duk_rp_fclose(duk_context *ctx)
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

            duk_pop(ctx);
            f = getfh(ctx,0,"fclose()");
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

duk_ret_t duk_rp_fflush(duk_context *ctx)
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


duk_ret_t duk_rp_fprintf(duk_context *ctx)
{
    int ret;
    const char *fn;
    FILE *f=NULL;
    int append=0;
    int closefh=1;
    pthread_mutex_t *lock_p=NULL;

    if(duk_is_object(ctx,0))
    {
        f = getfh_nonull_lock(ctx,0,"fprintf()",lock_p);
        closefh=0;
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
            if( (f=fopen(fn,"a")) == NULL )
            {
                    goto err;
            }
        }
        else
        {
            if( (f=fopen(fn,"w")) == NULL )
                goto err;
        }
    }

    if(!lock_p)
    {
        if (flock(fileno(f), LOCK_EX) == -1)
            RP_THROW(ctx, "fprintf(): error - could not obtain lock");
    }
    else
    {
        if (pthread_mutex_lock(lock_p) != 0)
            RP_THROW(ctx, "fprintf(): error - could not obtain lock");
    }

    errno=0;
    ret = rp_printf(_fout_char, (void*)f, (size_t)-1, ctx, 1, lock_p);
    fflush(f);
    if(errno)
        RP_THROW(ctx, "fprintf(): error - %s", strerror(errno));

    if(!lock_p)
    {
        if (flock(fileno(f), LOCK_UN) == -1)
            RP_THROW(ctx, "fprintf(): error - could not release lock");
    }
    else
    {
        if (pthread_mutex_unlock(lock_p) != 0)
          RP_THROW(ctx, "fprintf(): error - could not release lock");
    }

    if(closefh)
        fclose(f);

    duk_push_int(ctx, ret);
    return 1;

    err:
    RP_THROW(ctx, "error opening file '%s': %s", fn, strerror(errno));
    return 0;
}

duk_ret_t duk_rp_sprintf(duk_context *ctx)
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

duk_ret_t duk_rp_bprintf(duk_context *ctx)
{
    char *buffer;
    int size = rp_printf(_out_null, NULL, (size_t)-1, ctx, 0, NULL);
    buffer = (char *) duk_push_fixed_buffer(ctx, (duk_size_t)size);
    (void)rp_printf(_out_buffer, buffer, (size_t)-1, ctx, 0, NULL);
    return 1;
}

duk_ret_t duk_rp_getType(duk_context *ctx)
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

#define func_fprintf 0
#define func_fseek 1
#define func_rewind 2
#define func_ftell 3
#define func_fflush 4
#define func_fread 5
#define func_fwrite 6
#define func_readline 7
#define func_fclose 8

static duk_ret_t (*funcmap[9])(duk_context *ctx) = {
  duk_rp_fprintf, duk_rp_fseek,    duk_rp_rewind,
  duk_rp_ftell,   duk_rp_fflush,   duk_rp_fread,
  duk_rp_fwrite,  duk_rp_readline, duk_rp_fclose
};

static int f_return_this[9] = {
  0, 1, 1,
  0, 1, 0,
  0, 0, 0
};

/* for var h=fopen(); h.fprintf, h.fseek, ... */

static duk_ret_t f_func(duk_context *ctx)
{
    int fno=-1;

    duk_push_current_function(ctx);
    if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("f_no")))
        RP_THROW(ctx, "Internal error getting function from filehandle");
    fno = REQUIRE_INT(ctx, -1, "Internal error getting function from filehandle");
    duk_pop_2(ctx);
    duk_push_this(ctx);
    duk_insert(ctx, 0);
    if(!f_return_this[fno])
        return (funcmap[fno])(ctx);
    else
    {
        (void) (funcmap[fno])(ctx);
        duk_push_this(ctx);
        return 1;
    }
}

#define pushffunc(fname, fn,n) do {\
    duk_push_c_function(ctx, f_func, (n));\
    duk_push_int(ctx, (fn));\
    duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("f_no"));\
    duk_put_prop_string(ctx,-2,(fname));\
} while(0) 


duk_ret_t duk_rp_fopen(duk_context *ctx)
{
    FILE *f;
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

    duk_push_c_function(ctx, duk_rp_fclose, 2);
    duk_set_finalizer(ctx, -2);

    pushffunc("fprintf",    func_fprintf,   DUK_VARARGS );
    pushffunc("fseek",      func_fseek,     2           );
    pushffunc("rewind",     func_rewind,    0           );
    pushffunc("ftell",      func_ftell,     0           );
    pushffunc("fflush",     func_fflush,    0           );
    pushffunc("fwrite",     func_fwrite,    2           );
    pushffunc("fread",      func_fread,     3           );
    pushffunc("readLine",   func_readline,  0           );
    pushffunc("fclose",     func_fclose,    0           );

    return 1;

    err:
    RP_THROW(ctx, "error opening file '%s': %s", fn, strerror(errno));
    return 0;
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
    duk_push_c_function(ctx, duk_rp_printf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "printf");

    duk_push_c_function(ctx, duk_rp_sprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "sprintf");

    duk_push_c_function(ctx, duk_rp_fprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "fprintf");

    duk_push_c_function(ctx, duk_rp_bprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "bprintf");

    duk_push_c_function(ctx, duk_rp_fopen, 2);
    duk_put_prop_string(ctx, -2, "fopen");

    duk_push_c_function(ctx, duk_rp_fclose, 1);
    duk_put_prop_string(ctx, -2, "fclose");

    duk_push_c_function(ctx, duk_rp_fflush, 1);
    duk_put_prop_string(ctx, -2, "fflush");

    duk_push_c_function(ctx, duk_rp_fseek, 3);
    duk_put_prop_string(ctx, -2, "fseek");

    duk_push_c_function(ctx, duk_rp_ftell, 1);
    duk_put_prop_string(ctx, -2, "ftell");

    duk_push_c_function(ctx, duk_rp_rewind, 1);
    duk_put_prop_string(ctx, -2, "rewind");

    duk_push_c_function(ctx, duk_rp_fread, 4);
    duk_put_prop_string(ctx, -2, "fread");

    duk_push_c_function(ctx, duk_rp_fwrite, 4);
    duk_put_prop_string(ctx, -2, "fwrite");

    duk_push_c_function(ctx, duk_rp_getType, 1);
    duk_put_prop_string(ctx, -2, "getType");

    duk_push_object(ctx);
    duk_push_string(ctx,"accessLog");
    duk_put_prop_string(ctx,-2,"stream");
    pushffunc("fprintf",    func_fprintf,   DUK_VARARGS );
    pushffunc("fflush",     func_fflush,    0           );
    pushffunc("fwrite",     func_fwrite,    2           );
    duk_put_prop_string(ctx, -2,"accessLog");

    duk_push_object(ctx);
    duk_push_string(ctx,"errorLog");
    duk_put_prop_string(ctx,-2,"stream");
    pushffunc("fprintf",    func_fprintf,   DUK_VARARGS );
    pushffunc("fflush",     func_fflush,    0           );
    pushffunc("fwrite",     func_fwrite,    2           );
    duk_put_prop_string(ctx, -2,"errorLog");

    duk_push_object(ctx);
    duk_push_string(ctx,"stdout");
    duk_put_prop_string(ctx,-2,"stream");
    pushffunc("fprintf",    func_fprintf,   DUK_VARARGS );
    pushffunc("fflush",     func_fflush,    0           );
    pushffunc("fwrite",     func_fwrite,    2           );
    duk_put_prop_string(ctx, -2,"stdout");

    duk_push_object(ctx);
    duk_push_string(ctx,"stderr");
    duk_put_prop_string(ctx,-2,"stream");
    pushffunc("fprintf",    func_fprintf,   DUK_VARARGS );
    pushffunc("fflush",     func_fflush,    0           );
    pushffunc("fwrite",     func_fwrite,    2           );
    duk_put_prop_string(ctx, -2,"stderr");

    duk_push_object(ctx);
    duk_push_string(ctx,"stdin");
    duk_put_prop_string(ctx,-2,"stream");
    pushffunc("fread",      func_fread,     2           );
    pushffunc("readLine",   func_readline,  0           );
    duk_put_prop_string(ctx, -2,"stdin");

    duk_put_prop_string(ctx, -2,"utils");
    duk_put_global_string(ctx,"rampart");

    RP_MINIT(&pflock);
    RP_MINIT(&pflock_err);
    RP_MINIT(&loglock);
    RP_MINIT(&errlock);

}
