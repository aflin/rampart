/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "register.h"
#include "core/duk_console.h"
#include "core/module.h"
#include "globals/printf.h"
#include "rampart.h"

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

static duk_ret_t duk_rp_object_values(duk_context *ctx)
{
    duk_uarridx_t i=0;

    if (duk_is_array(ctx,0))
        return 1;

    duk_push_array(ctx);
    if(duk_is_string(ctx, 0))
    {
        const char *s = duk_get_string(ctx, 0);
        while(*s)
        {
            duk_push_lstring(ctx, s, 1);
            duk_put_prop_index(ctx, -2, i);
            s++;
            i++; 
        }
        return 1;
    }
    if(duk_is_object(ctx, 0))
    {
        duk_enum(ctx, 0, DUK_ENUM_OWN_PROPERTIES_ONLY|DUK_ENUM_NO_PROXY_BEHAVIOR);
        while (duk_next(ctx, -1 , 0 ))
        {
            duk_put_prop_index(ctx, -3, i);
            i++;
        }
        duk_pop(ctx);
        return 1;
    }
    if(duk_is_number(ctx, 0)|| duk_is_buffer_data(ctx,0))
        return 1;
    if( duk_is_undefined(ctx, 0) || duk_is_null(ctx, 0) )
        RP_THROW(ctx, "Object.values - Cannot convert undefined or null to object");

    RP_THROW(ctx, "Object.values - Cannot convert to object");
    return 0;
}

static void add_object_values(duk_context *ctx)
{
    duk_get_global_string(ctx, "Object");    
    duk_push_c_function(ctx, duk_rp_object_values, 1);
    duk_put_prop_string(ctx, -2, "values");
    duk_pop(ctx);
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
    fix_json_parse(ctx);
    add_object_values(ctx);
}
