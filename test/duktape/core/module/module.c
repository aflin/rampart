#include "../../../runner.h"
#include "duktape/core/duktape.h"
#include "duktape/core/module.h"

void module_cyclic()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/cyclic/program');");
    duk_destroy_heap(ctx);
}

void module_exact_exports()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/exactExports/program');");
    duk_destroy_heap(ctx);
}

void test()
{
    module_cyclic();
    module_exact_exports();
}