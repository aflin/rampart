#include "rp.h"
#include "duktape.h"

void rp_register_functions(duk_context *ctx)
{
//  duk_db_init(ctx); /* register database functions from db.c */
  
  duk_ra_init(ctx); /* register ramis functions */
  
  duk_printf_init(ctx); /* register the printf and sprintf functions from printf.c */

  duk_misc_init(ctx); /* register functions in misc.c */
}
