#if !defined(RP_PRINTF_H)
#define RP_PRINTF_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "../core/duktape.h"
  extern void duk_printf_init(duk_context *ctx);

#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* end RP_PRINTF_H */