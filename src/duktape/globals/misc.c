#include "misc.h"
#include "../core/duktape.h"
#include "../../rp.h"
#include <ctype.h>
#include <unistd.h>
extern char **environ;

/* utility function for global object:
      var buf=toBuffer(val); //fixed if string, same type if already buffer
        or
      var buf=toBUffer(val,"[dynamic|fixed]"); //always converted to type
*/

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


void duk_rampart_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    duk_push_c_function(ctx, duk_rp_strToBuf, 2);
    duk_put_prop_string(ctx, -2, "stringToBuffer");
    duk_push_c_function(ctx, duk_rp_bufToStr, 1);
    duk_put_prop_string(ctx, -2, "bufferToString");
    duk_push_c_function(ctx, duk_rp_object2q,2);
    duk_put_prop_string(ctx, -2, "objectToQuery");
    duk_push_c_function(ctx, duk_rp_query2o,1);
    duk_put_prop_string(ctx, -2, "queryToObject");
    
    duk_put_global_string(ctx, "rampart");
}




void duk_misc_init(duk_context *ctx)
{
    duk_rampart_init(ctx);
    duk_process_init(ctx);
}
