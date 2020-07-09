#include "../../../runner.h"
#include "duktape/core/duktape.h"
#include "duktape/core/module.h"
#define UTILS_MODULE_PATH "../../../../src/rputils"
void read_file()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "file = utils.readFile({ file: 'helloworld.txt' });";

    duk_eval_string(ctx, js);

    // basic case
    assert(duk_get_length(ctx, -1) == 11);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello world"));

    // length
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', length: 10 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello worl"));

    // negative length (bytes from end)
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', length: -1 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello worl"));

    // offset
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', offset: 1 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "ello world"));

    duk_destroy_heap(ctx);
}

void read_ln()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "var iter = utils.readln('loremipsum.txt')[Symbol.iterator]();"
        "var next = iter.next();"
        "var i = 1;"
        "var value;"
        "while(!next.done) {"
        "   if(i == 12) {"
        "       value = next.value;"
        "       break;"
        "   }"
        "   i++;"
        "   next = iter.next();"
        "}"
        "value";

    // test an arbitrary line
    duk_eval_string(ctx, js);
    assert(!strcmp(duk_get_string(ctx, -1), "Quisque consequat elit non dapibus mattis.\n"));
    duk_destroy_heap(ctx);
}

void test()
{
    read_file();
    read_ln();
}