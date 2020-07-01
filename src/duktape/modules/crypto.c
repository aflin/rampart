#include "../core/duktape.h"
#include <openssl/evp.h>

static duk_ret_t duk_aes(duk_context *ctx)
{
    return 0;
}

const duk_function_list_entry crypto_funcs[] = {
    {"aes", duk_aes, 1},
    {NULL, NULL, 0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, crypto_funcs);
    return 1;
}
