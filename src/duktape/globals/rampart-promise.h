/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 */

#ifndef RAMPART_PROMISE_H
#define RAMPART_PROMISE_H

#include "duktape.h"

/* Install a JavaScript Promise constructor on the global object.
 * Eagerly evaluated at context init so vanilla rampart has Promise
 * without needing `-t` / `-b` / `"use transpiler"`.  Uses the same
 * polyfill source the transpiler emits under PROMISE_PF (Taylor
 * Hakes' promise-polyfill, MIT, ~5 KB).
 *
 * Self-guarding: if Promise is already present (e.g. installed by
 * a previous evaluator), patches missing methods (allSettled, any,
 * finally) instead of replacing the constructor.
 *
 * Call from duk_init_context() alongside the other JS-string
 * polyfill installs.
 */
void duk_rp_promise_init(duk_context *ctx);

#endif
