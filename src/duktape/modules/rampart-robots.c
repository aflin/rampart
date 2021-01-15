#include "duktape.h"


int rp_rbt_allowed(char *robots_txt, char *uagent, char *iurl );


#define RP_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define REQUIRE_STRING(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    const char *r=duk_get_string((ctx),i);\
    r;\
})


static duk_ret_t allowed(duk_context *ctx)
{
    
    char *uagent = (char *) REQUIRE_STRING(ctx, 0, "First parameter must be a string (user agent)");
    char *robots_txt = (char *) REQUIRE_STRING(ctx, 1, "Second parameter must be a string (robots.txt)");
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

  duk_push_c_function(ctx, allowed, 3);

  return 1;
}
