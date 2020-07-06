#include <ctype.h>
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

duk_ret_t duk_rp_bufToStr(duk_context *ctx)
{

  duk_buffer_to_string(ctx,0);

  return 1;
}

void duk_strToBuf_init(duk_context *ctx)
{
  if (!duk_get_global_string(ctx,"rampart"))
  {
    duk_pop(ctx);
    duk_push_object(ctx);  
  }

  duk_push_c_function(ctx, duk_rp_strToBuf, 2);
  duk_put_prop_string(ctx, -2, "stringToBuffer");
  duk_push_c_function(ctx, duk_rp_bufToStr, 1);
  duk_put_prop_string(ctx, -2, "bufferToString");

  duk_put_global_string(ctx,"rampart");
}

void duk_misc_init(duk_context *ctx)
{
    duk_strToBuf_init(ctx);
}

/* **************************************************************************
   This url(en|de)code is public domain from https://www.geekhideout.com/urlcode.shtml 
   ************************************************************************** */

/* Converts a hex character to its integer value */

static char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
static char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *duk_rp_url_encode(char *str, int len) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  
  if(len<0)len=strlen(str);
  
  while (len) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') 
      *pbuf++ = *pstr;
    else if (*pstr == ' ') 
      *pbuf++ = '+';
    else 
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
    len--;
  }
  *pbuf = '\0';
  return buf;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *duk_rp_url_decode(char *str, int len) {
  char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
  
  if(len<0)len=strlen(str);

  while (len) {
    if (*pstr == '%' && len>2) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
        len-=2;
    } else if (*pstr == '+') { 
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
    len--;
  }
  *pbuf = '\0';
  return buf;
}
