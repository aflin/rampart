/* Copyright (C) 2024 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "register.h"
#include "core/module.h"
#include "globals/printf.h"
#include "rampart.h"
#include "../include/version.h"

/* allow JSON.parse to accept buffers */
duk_ret_t duk_rp_json_parse(duk_context *ctx)
{
    if(duk_is_buffer_data(ctx,0))
        duk_buffer_to_string(ctx,0);
    duk_get_global_string(ctx, "JSON");
    duk_get_prop_string(ctx, -1, "_parse_orig");
    duk_insert(ctx, 0);
    duk_pop(ctx);//"JSON"
    duk_call(ctx,2);
    return 1;
}

void fix_json_parse(duk_context *ctx)
{
    duk_get_global_string(ctx, "JSON");
    duk_get_prop_string(ctx, -1, "parse");
    duk_put_prop_string(ctx, -2, "_parse_orig");

    duk_push_c_function(ctx, duk_rp_json_parse, 2);
    duk_put_prop_string(ctx, -2, "parse");
    duk_pop(ctx);
}

duk_ret_t duk_rp_values_from_object(duk_context *ctx, duk_idx_t idx)
{
    duk_uarridx_t i=0;

    idx = duk_normalize_index(ctx, idx);
    if (duk_is_array(ctx,idx))
        return 1;

    duk_push_array(ctx);
    if(duk_is_string(ctx, idx))
    {
        const char *s = duk_get_string(ctx, idx);
        while(*s)
        {
            duk_push_lstring(ctx, s, 1);
            duk_put_prop_index(ctx, -2, i);
            s++;
            i++; 
        }
        return 1;
    }
    if(duk_is_object(ctx, idx))
    {
        duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY|DUK_ENUM_NO_PROXY_BEHAVIOR);
        while (duk_next(ctx, -1 , 1 ))
        {
            duk_put_prop_index(ctx, -4, i);
            i++;
            duk_pop(ctx);
        }
        duk_pop(ctx);
        return 1;
    }
    if(duk_is_number(ctx, idx)|| duk_is_buffer_data(ctx,idx))
        return 1;
    if( duk_is_undefined(ctx, idx) || duk_is_null(ctx, idx) )
        RP_THROW(ctx, "Object.values - Cannot convert undefined or null to object");

    RP_THROW(ctx, "Object.values - Cannot convert to object");

    return 0;
}



static duk_ret_t duk_rp_object_values(duk_context *ctx)
{
    return duk_rp_values_from_object(ctx, 0);
}

static void add_object_values(duk_context *ctx)
{
    duk_get_global_string(ctx, "Object");    
    duk_push_c_function(ctx, duk_rp_object_values, 1);
    duk_put_prop_string(ctx, -2, "values");
    duk_pop(ctx);
}

static int duk_rp_compare(duk_context *ctx, duk_idx_t idx1, duk_idx_t idx2)
{
    if(duk_get_type(ctx,idx1) != duk_get_type(ctx,idx2))
        return 0;

    switch(duk_get_type(ctx,idx1))
    {
        case DUK_TYPE_NUMBER:
        {
            if(duk_get_number(ctx,idx1) == duk_get_number(ctx,idx2))
                return 1;
            else if (duk_is_nan(ctx,idx1) && duk_is_nan(ctx,idx2))
                return 1;
            break;
        }
        case DUK_TYPE_BOOLEAN:
        {
            if(duk_get_boolean(ctx,idx1) == duk_get_boolean(ctx,idx2))
                return 1;
            break;
        }
        case DUK_TYPE_UNDEFINED:
        {
            return 1;
        }
        case DUK_TYPE_NULL:
        {
            return 1;
        }
        default:
        {
            void *v1=duk_get_heapptr(ctx,idx1), *v2=duk_get_heapptr(ctx,idx2);
            return (v1==v2);
        }
    }
    return 0;
}

static duk_ret_t duk_rp_array_includes(duk_context *ctx)
{
    int i=0, len;

    duk_push_this(ctx);
    len=duk_get_length(ctx, -1);

    if(duk_is_number(ctx, 1))
        i=duk_get_int(ctx, 1);
    if(i<0)i=0;

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        if (duk_rp_compare(ctx, 0, -1))
        {
            duk_push_true(ctx);
            return 1;
        }
        duk_pop(ctx);
    }

    duk_push_false(ctx);
    return 1;
}

static void add_array_includes(duk_context *ctx)
{
    duk_get_global_string(ctx, "Array");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_push_c_function(ctx, duk_rp_array_includes, 2);
    duk_put_prop_string(ctx, -2, "includes");
    duk_pop_2(ctx);
}

duk_ret_t duk_rp_buffer_from(duk_context *ctx)
{

    if(duk_is_buffer_data(ctx, 0) || duk_is_string(ctx, 0) )
    {
        duk_size_t from_sz;
        void *from_buf = (void *) REQUIRE_STR_OR_BUF(ctx, 0, &from_sz, "");
        void *to_buf = duk_get_buffer_data(ctx, -1, NULL);

        duk_get_global_string(ctx, "Buffer");
        duk_push_number(ctx, (double) from_sz);
        duk_new(ctx, 1);

        to_buf = duk_get_buffer_data(ctx, -1, NULL);
        memcpy(to_buf, from_buf, from_sz);
    }
    else // TODO:do array case
        RP_THROW(ctx, "Buffer.from: Argument must be a Buffer or String");

    return 1;
}

duk_ret_t duk_rp_buffer_alloc(duk_context *ctx)
{
    int size = 0;

    size = REQUIRE_INT(ctx, 0, "Buffer.alloc: size must be an integer");
    if(size < 0)
        RP_THROW(ctx, "Buffer.alloc: size must be a positive number");

    duk_get_global_string(ctx, "Buffer");
    duk_dup(ctx, 0);
    duk_new(ctx, 1);

    if(duk_is_buffer_data(ctx, 1) || duk_is_string(ctx, 1) )
    {
        duk_size_t from_sz;
        int i=0;
        const char *from_buf = REQUIRE_STR_OR_BUF(ctx, 1, &from_sz, "");
        char *to_buf = (char *) duk_get_buffer_data(ctx, -1, NULL);

        if (from_sz > 0)
        {
            while(i<size)
            {
                int idx = i%from_sz;
                to_buf[i]=from_buf[idx];
                i++;
            }
        }
    }
    else if(!duk_is_undefined(ctx, 1))
        RP_THROW(ctx, "Buffer.alloc: Second argument must be a Buffer or String");

    return 1;
}

static void add_buffer_func(duk_context *ctx)
{
    duk_get_global_string(ctx, "Buffer");
    duk_push_c_function(ctx, duk_rp_buffer_alloc, 3);
    duk_put_prop_string(ctx, -2, "alloc");
    duk_push_c_function(ctx, duk_rp_buffer_from, 1);
    duk_put_prop_string(ctx, -2, "from");
    /* TODO: add all node methods */
    duk_pop(ctx);
}

static duk_ret_t rp_eval_js(duk_context *ctx)
{
    char *source=NULL;
    const char *bfn=NULL;

    if(!duk_is_string(ctx,0))
    {
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));
        duk_insert(ctx, 0);
        duk_call(ctx, duk_get_top_index(ctx));
        return 1;
    }
    
    source=(char*)duk_get_string(ctx, 0);

    struct timespec tsnow;
    clock_gettime(CLOCK_REALTIME, &tsnow);

    // get orig eval from DUK_HIDDEN_SYMBOL("buildin_eval")
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));

    // main_babel_opt is non null if this script was previously babelized.
    if ( !main_babel_opt || ! (bfn=duk_rp_babelize(ctx, "eval_code", source, tsnow.tv_sec, 1, main_babel_opt)) )
    {
        int err=0, lineno=0;
        char *tickified = tickify(source, strlen(source), &err, &lineno);
        if (err)
        {
            RP_THROW(ctx, "SyntaxError: %s (line %d of eval code)\n", tickify_err(err), lineno);
        }

        duk_push_string(ctx, tickified);
        free(tickified);
    }
    if(bfn)
        free((char*)bfn);
    duk_call(ctx,1);
    return 1;
}

static void fix_eval(duk_context *ctx)
{
    /* new eval code */
    // save orig eval to DUK_HIDDEN_SYMBOL("buildin_eval") if not already there
    if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval")))
    {
        duk_get_global_string(ctx, "eval");
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));
    }
    duk_pop(ctx);
    duk_push_c_function(ctx, rp_eval_js, 1);
    duk_put_global_string(ctx, "eval");
}

void duk_init_context(duk_context *ctx)
{
    /* https://wiki.duktape.org/howtoglobalobjectreference */
    /*char globdef[]="if (typeof global === 'undefined') {(function () {var global = new Function('return this;')();Object.defineProperty(global, 'global', {value: global,writable: true,enumerable: false,configurable: true});})()}";
    
    if (duk_pcompile_string(ctx, 0, globdef) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
    }
    if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
    }
    duk_pop(ctx);
    */

    //maybe just do this?
    duk_push_global_object(ctx);
    duk_put_global_string(ctx,"global");


    duk_console_init(ctx, DUK_CONSOLE_FLUSH); /* register console.log, console.error etc. */
    duk_module_init(ctx);                     /* register require() function */
    duk_printf_init(ctx);                     /* register the printf and sprintf functions from printf.c */
    duk_misc_init(ctx);                       /* register functions in rampart-utils.c */
    duk_import_init(ctx);                     /* register functions in rampart-import.c */
    duk_process_init(ctx);                    /* register process.* vars */
    duk_event_init(ctx);                      /* register functions in rampart-event.c */
    duk_thread_init(ctx);                     /* register functions in rampart-thread.c */
    duk_rp_push_rampart_version(ctx);         /* rampart version info */
    fix_json_parse(ctx);
    fix_eval(ctx);
    add_object_values(ctx);
    add_array_includes(ctx);
    add_buffer_func(ctx);

}
