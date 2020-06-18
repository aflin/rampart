#include "duktape.h"

/* utility function for global object:
      var buf=toBuffer(val); //fixed if string, same type if already buffer
        or
      var buf=toBUffer(val,"[dynamic|fixed]"); //always converted to type
*/

duk_ret_t duk_rp_strToBuf(duk_context *ctx)
{
  duk_size_t sz;
  const char *opt=duk_to_string(ctx,1);

  if( !strcmp(opt,"dynamic") )
    duk_to_dynamic_buffer(ctx,0,&sz);
  else if ( !strcmp(opt,"fixed") )
    duk_to_fixed_buffer(ctx,0,&sz);
  else
    duk_to_buffer(ctx,0,&sz);

  duk_pop(ctx);
  return 1;
}

void duk_strToBuf_init(duk_context *ctx)
{
  duk_push_c_function(ctx, duk_rp_strToBuf, 2);
  duk_put_global_string(ctx, "toBuffer");
}

void duk_misc_init(duk_context *ctx)
{
    duk_strToBuf_init(ctx);
}
