/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 */

#ifndef RAMPART_BLOB_H
#define RAMPART_BLOB_H

#include "duktape.h"

/* Install W3C File API `Blob` and `File` constructors on the global
 * object.  Both are JS-standards (W3C / WHATWG) types — not
 * node-specific — and rampart core exposes them in line with
 * Buffer, Promise, console, Map, Set, TextEncoder/Decoder.
 *
 * Backing storage is a fixed duktape buffer stored as a hidden
 * property; methods access it from C via duk_get_buffer_data() with
 * no JS round-trip for byte access.
 *
 * Surface:
 *   new Blob(parts, options)             // parts: string | ArrayBuffer | TypedArray | DataView | Buffer | Blob
 *   blob.size                            // bytes (read-only)
 *   blob.type                            // MIME, lowercased (read-only)
 *   blob.slice(start, end, contentType)  // new Blob over a byte range
 *   blob.text()        → Promise<string>
 *   blob.arrayBuffer() → Promise<ArrayBuffer>
 *   blob.bytes()       → Promise<Uint8Array>
 *   blob.stream()      // throws NotSupportedError until stream/web lands
 *
 *   new File(parts, name, options)       // same as Blob + name + lastModified
 *   file.name                            // string (read-only)
 *   file.lastModified                    // ms-since-epoch number (read-only)
 *
 * Call from duk_init_context() after duk_rp_buffer_init() (Blob's
 * methods construct Uint8Array views, so Buffer's prototype work
 * must be done first) and after duk_rp_promise_init() (the async-
 * shaped methods wrap results in Promise.resolve(...)).
 */
void duk_rp_blob_init(duk_context *ctx);

#endif
