#include "register.h"
#include "core/duk_console.h"
#include "core/module.h"
#include "globals/misc.h"
#include "globals/printf.h"
#include "globals/ramis.h"
#include "globals/db.h"

void duk_init_context(duk_context *ctx)
{
  duk_console_init(ctx, DUK_CONSOLE_FLUSH); /* register console.log, console.error etc. */
  duk_module_init(ctx);                     /* register require() function */
  duk_db_init(ctx);                         /* register database functions from db.c */
  duk_ra_init(ctx);                         /* register ramis functions */
  duk_printf_init(ctx);                     /* register the printf and sprintf functions from printf.c */
  duk_misc_init(ctx);                       /* register functions in misc.c */
}
