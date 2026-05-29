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
  /* Minimal init for "bare" threads (vm sandbox).  Installs ECMAScript
     primordials (via duk_create_heap_default), language fixes (json/eval/
     transpile), the rampart-events backbone, and rampart.thread surface
     (put/get/waitfor/onGet/exec).  Does NOT install rampart.utils,
     process, require, vector, version, or any WHATWG/Intl lazy getters. */
  extern void duk_init_context_bare(duk_context *ctx);
  void duk_rp_url_init(duk_context *ctx);     /* defined in C++: rampart-url.cpp */
  void duk_process_init(duk_context *ctx);
  void duk_import_init(duk_context *ctx);
  void duk_event_init(duk_context *ctx);
  void duk_vector_init(duk_context *ctx);
  void duk_thread_init(duk_context *ctx);
  /* duk_map_set_init: moved into duktape fork (DUK_RP_USE_MAP_SET);
     no external decl needed. */
  /* Mark a property non-enumerable. Defined in register.c; consumed
     historically by rampart-buffer.c etc.  Kept here for any remaining
     external callers — Buffer/Blob/etc. ported into the duktape fork
     now use the in-amalgamation static `duk_rp_set_enum_false` from
     duk_rp_internal.h instead. */
  void duk_rp_set_enum_false(duk_context *ctx, duk_idx_t objidx, const char *propname);
#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */
#endif /* RP_REGISTER_H */