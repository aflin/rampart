#if !defined(MODULE_H_INCLUDED)
#define MODULE_H_INCLUDED

#if defined(__cplusplus)
extern "C"
{
#endif

#include "duktape.h"
  extern void duk_module_init(duk_context *ctx);

#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */

#endif /* MODULE_H_INCLUDED */