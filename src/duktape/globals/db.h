#if !defined(RP_DB_H)
#define RP_DB_H

#if defined(__cplusplus)
extern "C"
{
#endif
#include "../core/duktape.h"

  extern void duk_db_init(duk_context *ctx);

#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* end RP_DB_H */