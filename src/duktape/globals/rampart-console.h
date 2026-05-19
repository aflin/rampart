/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 */

#ifndef RAMPART_CONSOLE_H
#define RAMPART_CONSOLE_H

#include "duktape.h"

/* Augment duktape's global `console` with node-style methods:
 *   console.time(label) / timeEnd(label) / timeLog(label)
 *   console.table(data, [columns])
 *   console.group(label) / groupEnd() / groupCollapsed()
 *   console.count(label) / countReset(label)
 *   console.clear()
 *
 * Duktape ships log/error/warn/info/debug/trace/dir/assert already;
 * this fills the gap.  All rampart code benefits, not just nodeshim
 * consumers.
 *
 * Call AFTER duk_rp_buffer_init / textencoding_init etc.
 */
void duk_rp_console_init(duk_context *ctx);

#endif
