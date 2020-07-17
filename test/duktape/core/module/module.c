#include "../../../runner.h"
#include "duktape/core/duktape.h"
#include "duktape/core/module.h"

void test()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    if (duk_peval_string(ctx, "require('commonjs/cyclic/program');") == DUK_EXEC_ERROR)
    {
        fprintf(stderr, "%s\n", duk_to_stacktrace(ctx, -1));
    }
    duk_destroy_heap(ctx);
}