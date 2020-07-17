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

void module_method()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/method/program');");
    duk_destroy_heap(ctx);
}

void module_missing()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/missing/program');");
    duk_destroy_heap(ctx);
}

void module_monkeys()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/monkeys/program');");
    duk_destroy_heap(ctx);
}

void module_nested()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/nested/program');");
    duk_destroy_heap(ctx);
}

void module_relative()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/relative/program');");
    duk_destroy_heap(ctx);
}

void module_transitive()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    duk_eval_string(ctx, "require('commonjs/transitive/program');");
    duk_destroy_heap(ctx);
}

void module_popped()
{
    duk_context *ctx = duk_create_heap_default();
    duk_push_int(ctx, 100);
    duk_module_init(ctx);
    int i = duk_get_int(ctx, -1);
    assert(i == 100);
    duk_destroy_heap(ctx);
}

void test()
{
    module_cyclic();
    module_exact_exports();
    module_method();
    module_missing();
    module_monkeys();
    module_nested();
    module_relative();
    module_transitive();
    module_popped();
}