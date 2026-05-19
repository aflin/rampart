/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 */

#ifndef RAMPART_TEXTENCODING_H
#define RAMPART_TEXTENCODING_H

#include "duktape.h"

/* Replace duktape's incomplete TextEncoder/TextDecoder globals with
 * WHATWG-spec-compatible versions backed by rampart's Buffer.
 *
 * Why: duktape ships TextEncoder/TextDecoder but they're effectively
 * utf-8-only -- TextDecoder accepts any label and silently treats
 * everything as utf-8, and TextEncoder lacks encodeInto.  Anyone
 * doing `new TextDecoder('latin1').decode(buf)` quietly gets wrong
 * answers.
 *
 * This module:
 *   - Replaces the global TextDecoder with one that honors the
 *     encoding label (utf-8, utf-16le, utf-16be, iso-8859-1 +
 *     aliases, us-ascii + aliases).  Unknown labels throw
 *     RangeError per spec.
 *   - Adds TextEncoder.prototype.encodeInto.
 *   - Strips/honors BOM per the ignoreBOM option.
 *   - Implements fatal mode for utf-8 / utf-16 / ascii.
 *
 * Routed through rampart-buffer.c's already-implemented encoders.
 * Call AFTER duk_rp_buffer_init(ctx).
 */
void duk_rp_textencoding_init(duk_context *ctx);

#endif
