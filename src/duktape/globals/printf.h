#if !defined(RP_PRINTF_H)
#define RP_PRINTF_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "rampart.h"
  extern void duk_printf_init(duk_context *ctx);
  // output function type
  typedef void (*out_fct_type)(char character, void *buffer, size_t idx, size_t maxlen);
  extern int rp_printf(out_fct_type out, char *buffer, const size_t maxlen, duk_context *ctx, duk_idx_t fidx, pthread_mutex_t *lock_p);
#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* end RP_PRINTF_H */