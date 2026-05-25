/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
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
  void duk_rp_url_init(duk_context *ctx);     /* defined in C++: rampart-url.cpp */
  void duk_process_init(duk_context *ctx);
  void duk_import_init(duk_context *ctx);
  void duk_event_init(duk_context *ctx);
  void duk_vector_init(duk_context *ctx);
  void duk_thread_init(duk_context *ctx);
  void duk_map_set_init(duk_context *ctx);
  /* Mark a property non-enumerable. Defined in register.c; consumed by
     rampart-buffer.c (and others) — without a prototype, macOS clang
     hard-fails on the implicit declaration. */
  void duk_rp_set_enum_false(duk_context *ctx, duk_idx_t objidx, const char *propname);
#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* RP_REGISTER_H */