#ifndef RP_TRANSPILE_H
#define RP_TRANSPILE_H

#include "../../extern/tree-sitter/transpiler.h"

/* use transpiler if src has "use transpiler" or "use transpilerGlobally"
   if not, then use the stable simple template processing in tickify
   and set is_tickified to 1                                               */
RP_ParseRes rp_get_transpiled(char *src, int *is_tickified);

#endif
