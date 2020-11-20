/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#if !defined(RP_RAMIS_H)
#define RP_RAMIS_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "../core/duktape.h"
  extern void duk_ra_init(duk_context *ctx);

#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* end RP_RAMIS_H */