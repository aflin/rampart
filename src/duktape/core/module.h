/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
   Copyright (C) 2020 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#if !defined(MODULE_H_INCLUDED)
#define MODULE_H_INCLUDED

#if defined(__cplusplus)
extern "C"
{
#endif

#include "duktape.h"
    extern void duk_module_init(duk_context *ctx);
    extern duk_ret_t duk_resolve(duk_context *ctx);
    extern duk_ret_t duk_require(duk_context *ctx);
#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */

#endif /* MODULE_H_INCLUDED */
