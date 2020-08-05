#include "register.h"
#include "core/duk_console.h"
#include "core/module.h"
#include "globals/misc.h"
#include "globals/printf.h"
#include "globals/ramis.h"

void duk_init_context(duk_context *ctx)
{
    /* https://wiki.duktape.org/howtoglobalobjectreference */
    char globdef[]="if (typeof global === 'undefined') {(function () {var global = new Function('return this;')();Object.defineProperty(global, 'global', {value: global,writable: true,enumerable: false,configurable: true});})()}";

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

    duk_console_init(ctx, DUK_CONSOLE_FLUSH); /* register console.log, console.error etc. */
    duk_module_init(ctx);                     /* register require() function */
    duk_ra_init(ctx);                         /* register ramis functions */
    duk_printf_init(ctx);                     /* register the printf and sprintf functions from printf.c */
    duk_misc_init(ctx);                       /* register functions in misc.c */
}
