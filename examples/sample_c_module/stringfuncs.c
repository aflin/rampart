#include <ctype.h>
#include "rampart.h"

/* 
  reverse a string 
  return must be freed
*/
static char * reverse_single_str(const char *str)
{
    int i=0, len = strlen(str);
    char *ret = malloc(len+1);
    
    if (!ret)
    {
        fprintf(stderr, "malloc err\n");
        exit(1);
    }

    ret[len]='\0';
    while (len)
    {
        len--;
        ret[len] = str[i];
        i++;
    }
    return ret;
}

/* 
  lower a string 
  return must be freed
*/
static char * lower_single_str(const char *str)
{
    int i=0, len = strlen(str);
    char *ret = malloc(len+1);
    
    if (!ret)
    {
        fprintf(stderr, "malloc err\n");
        exit(1);
    }

    ret[len]='\0';
    while (i<len)
    {
        ret[i] = tolower(str[i]);
        i++;
    }
    return ret;
}

/* javascript c function to reverse a string 
   or array of strings                         */
static duk_ret_t reverse_string(duk_context *ctx)
{
    const char *str;
    char *rev;

    /* check for an array of strings */
    if (duk_is_array(ctx, 0))
    {
        duk_uarridx_t i=0, len = duk_get_length(ctx, 0);   // Array.length

        // var ret = [];
        duk_push_array(ctx); //the return array;
        for (;i<len;i++)
        {
            // var str = arr[i];
            duk_get_prop_index(ctx, 0, i);
            // Get string. Throw error if not a string. See rampart.h for REQUIRE_* macros
            str = REQUIRE_STRING(ctx, -1, "reverse(): array member must be a String");
            duk_pop(ctx);

            // var rev = reverse_single_str(str);
            rev = reverse_single_str(str);

            // ret[i]=rev;
            duk_push_string(ctx, rev);
            free(rev);
            duk_put_prop_index(ctx, -2, i);
        }
    }
    else
    {
        // if not an array of strings, it must be a string.
        str = REQUIRE_STRING(ctx, 0, "reverse(): parameter must be a String or Array of Strings");

        // var ret = reverse_single_str(str);
        rev = reverse_single_str(str);
        duk_push_string(ctx, rev);
        free(rev);
    }

    return 1; // return ret;
}

/* javascript c function to convert a string 
   or array of strings to lowercase            */
static duk_ret_t lower_string(duk_context *ctx)
{
    const char *str;
    char *low;

    /* check for an array of strings */
    if (duk_is_array(ctx, 0))
    {
        duk_uarridx_t i=0, len = duk_get_length(ctx, 0);   // Array.length

        // var ret = [];
        duk_push_array(ctx); //the return array;
        for (;i<len;i++)
        {
            // var str = arr[i];
            duk_get_prop_index(ctx, 0, i);
            // Get string. Throw error if not a string. See rampart.h for REQUIRE_* macros
            str = REQUIRE_STRING(ctx, -1, "lower(): array member must be a String");
            duk_pop(ctx);

            // var low = lower_single_str(str);
            low = lower_single_str(str);

            // ret[i]=low;
            duk_push_string(ctx, low);
            free(low);
            duk_put_prop_index(ctx, -2, i);
        }
    }
    else
    {
        // if not an array of strings, it must be a string
        str = REQUIRE_STRING(ctx, 0, "lower(): parameter must be a String or Array of Strings");

        // var ret = lower_single_str(str);
        low = lower_single_str(str);
        duk_push_string(ctx, low);
        free(low);
    }

    return 1; // return ret;
}

/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);                         // the return object.           Stack: [ {} ]
                                                  // same as: var ret = {};

    duk_push_c_function(ctx, reverse_string, 1);  // the reverse_string function. Stack: [ {}, func ]
    duk_put_prop_string (ctx, -2, "reverse");     // add property to ret.         Stack: [ {reverse:func} ]  
                                                  // same as: ret.reverse = function reverse_string(){...}

    duk_push_c_function(ctx, lower_string, 1);    // the lower_string function.   Stack: [ {reverse:func}, func ]
    duk_put_prop_string (ctx, -2, "lower");       // add property to ret.         Stack: [ {reverse:func, lower:func} ]  
                                                  // same as: ret.lower = function lower_string(){...}

    return 1;                                     // return the item on top of stack.
                                                  // same as: return ret;
}
