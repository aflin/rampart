/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"

int rp_rbt_allowed(char *robots_txt, char *uagent, char *iurl );

static duk_ret_t allowed(duk_context *ctx)
{
    
    char *uagent = (char *) REQUIRE_STRING(ctx, 0, "First parameter must be a string (user agent)");
    /* duk_is_buffer() is true only for a PLAIN buffer.  A Uint8Array,
       ArrayBuffer or node Buffer is a buffer *object*, which fell through to
       REQUIRE_STRING and threw -- even though getType() calls them "Buffer".
       duk_is_buffer_data() covers both, matching the fix made to
       hexify()/stringToBuffer() in rampart-utils.c. */
    if(duk_is_buffer_data(ctx, 1))
        (void) duk_buffer_to_string(ctx, 1);
    char *robots_txt = (char *) REQUIRE_STRING(ctx, 1, "Second parameter must be a string or buffer (robots.txt)");
    char *iurl = (char *) REQUIRE_STRING(ctx, 2, "Third parameter must be a string (url)");


    int ret = rp_rbt_allowed(robots_txt, uagent, iurl );

    duk_push_boolean(ctx, ret);
    return 1;

}


/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{

  duk_push_object(ctx);
  duk_push_c_function(ctx, allowed, 3);
  duk_put_prop_string(ctx, -2, "isAllowed");
  return 1;
}
