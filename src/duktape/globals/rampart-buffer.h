/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#ifndef RAMPART_BUFFER_H
#define RAMPART_BUFFER_H

#include "duktape.h"

/* Register additional Buffer static methods + prototype methods on the
 * global Buffer constructor that duktape already provides.
 *
 * Without this, duktape's Buffer exposes the basic shape (alloc, from,
 * write, slice, etc.) but ignores the `encoding` argument and treats
 * everything as UTF-8. This file fills the node-compat gap:
 *   - Buffer.from(string, encoding) for hex/base64/base64url/latin1/binary/utf16le
 *   - Buffer.prototype.toString(encoding) for the same set
 *   - Buffer.prototype.write(string, [offset], [length], [encoding])
 *   - Buffer.byteLength(string, encoding)
 *   - Buffer.allocUnsafe (alias of alloc)
 *   - Buffer.prototype.indexOf / lastIndexOf / includes
 *   - Buffer.prototype.swap16 / swap32
 *   - Buffer.prototype.subarray returns Buffer (not bare Uint8Array)
 *
 * Called from register.c after duktape's own Buffer is set up.
 */
void duk_rp_buffer_init(duk_context *ctx);

/* Exposed for use by other rampart code that previously called these
 * directly from register.c. */
duk_ret_t duk_rp_buffer_from(duk_context *ctx);
duk_ret_t duk_rp_buffer_alloc(duk_context *ctx);

#endif
