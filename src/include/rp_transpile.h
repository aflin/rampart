#ifndef RP_TRANSPILE_H
#define RP_TRANSPILE_H

#include "../../extern/tree-sitter/transpiler.h"

/* use transpiler if src has "use transpiler" or "use transpilerGlobally"
   if not, then use the stable simple template processing in tickify
   and set is_tickified to 1                                               */
RP_ParseRes rp_get_transpiled(char *src, int *is_tickified);

/* Same as rp_get_transpiled but with file-based caching.
   Cache file: file.js -> file.transpiled.js
   If cache exists and is newer than src_mtime, loads from cache.
   Otherwise transpiles and writes the cache file.                  */
RP_ParseRes rp_get_transpiled_cached(char *fn, char *src, time_t src_mtime, int *is_tickified);

#endif
