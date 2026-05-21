#ifndef RP_TRANSPILE_H
#define RP_TRANSPILE_H

#include "../../extern/tree-sitter/transpiler.h"

/* use transpiler if src has "use transpiler" or "use transpilerGlobally"
   if not, then use the stable simple template processing in tickify
   and set is_tickified to 1                                               */
RP_ParseRes rp_get_transpiled(char *src, int *is_tickified);

/* Same as rp_get_transpiled but forces fn_sources=0. Used for
   `new Function(body)` bodies, where the resulting code is fed to
   duk_compile(DUK_COMPILE_FUNCTION) which expects a single function
   expression. fn-source's `_TrN_Sp._fs(...)` post-call would break
   that, and the polyfill preamble would also break it. */
RP_ParseRes rp_get_transpiled_no_fn_sources(char *src, int *is_tickified);

/* Same gating as rp_get_transpiled, but for eval() — uses
   transpile_eval() so the program-level IIFE wrap is skipped (eval
   preserves caller scope).  Without -t or "use transpiler", tickify
   only — no _TrN_Sp._fs() wrappers are emitted.                       */
RP_ParseRes rp_get_transpiled_eval(char *src, int *is_tickified);

/* Same as rp_get_transpiled but with file-based caching.
   Cache file: file.js -> file.transpiled.js
   If cache exists and is newer than src_mtime, loads from cache.
   Otherwise transpiles and writes the cache file.                  */
RP_ParseRes rp_get_transpiled_cached(char *fn, char *src, time_t src_mtime, int *is_tickified);

#endif
