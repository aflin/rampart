/* Copyright (C) 2024 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#if !defined(RP_REGISTER_H)
#define RP_REGISTER_H
#if defined(__cplusplus)
extern "C"
{
#endif

#include "rampart.h"
  extern void duk_init_context(duk_context *ctx);
  void duk_process_init(duk_context *ctx);
  void duk_import_init(duk_context *ctx);
  void duk_event_init(duk_context *ctx);
  void duk_thread_init(duk_context *ctx);
#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* RP_REGISTER_H */