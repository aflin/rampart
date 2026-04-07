/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"
#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"

#define checkflag(name,flag) do{\
    if(duk_get_prop_string(ctx, obj_idx, name)){\
        if(REQUIRE_BOOL(ctx, -1, "cmark.toHtml - option %s requires a boolean",name))\
            opts|=flag;\
    }\
    duk_pop(ctx);\
} while (0)

#define attachext(name) do{\
    cmark_syntax_extension *ext = cmark_find_syntax_extension(name);\
    if(ext)\
        cmark_parser_attach_syntax_extension(parser, ext);\
} while (0)

#define checkext(name) do{\
    if(duk_get_prop_string(ctx, obj_idx, name)){\
        if(!REQUIRE_BOOL(ctx, -1, "cmark.toHtml - option %s requires a boolean",name)){\
            duk_pop(ctx);\
            break;\
        }\
    }\
    duk_pop(ctx);\
    attachext(name);\
} while (0)

static duk_ret_t to_html(duk_context *ctx)
{
    duk_size_t sz=0;
    int opts=0;
    const char *input=NULL;
    char * res=NULL;
    duk_idx_t obj_idx=-1, str_idx=0;
    cmark_parser *parser=NULL;
    cmark_node *doc=NULL;

    if(duk_is_string(ctx, 1))
        str_idx=1;
    if(duk_is_object(ctx, !str_idx))
        obj_idx = !str_idx;

    input = REQUIRE_LSTRING(ctx, 0, &sz, "cmark.toHtml - first argument must be a string");

    if(obj_idx != -1)
    {
        checkflag("sourcePos",CMARK_OPT_SOURCEPOS);
        checkflag("hardBreaks",CMARK_OPT_HARDBREAKS);
        checkflag("unsafe",CMARK_OPT_UNSAFE);
        checkflag("noBreaks",CMARK_OPT_NOBREAKS);
        checkflag("smart",CMARK_OPT_SMART);
    }

    parser = cmark_parser_new(opts);

    if(obj_idx != -1)
    {
        checkext("table");
        checkext("strikethrough");
        checkext("autolink");
        checkext("tagfilter");
        checkext("tasklist");
    }
    else
    {
        attachext("table");
        attachext("strikethrough");
        attachext("autolink");
        attachext("tagfilter");
        attachext("tasklist");
    }

    cmark_parser_feed(parser, input, (size_t)sz);
    doc = cmark_parser_finish(parser);
    res = cmark_render_html(doc, opts, cmark_parser_get_syntax_extensions(parser));

    duk_push_string(ctx, res);
    free(res);
    cmark_node_free(doc);
    cmark_parser_free(parser);
    return 1;
}


/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
  cmark_gfm_core_extensions_ensure_registered();
  duk_push_object(ctx);
  duk_push_c_function(ctx, to_html, 2);
  duk_put_prop_string(ctx, -2, "toHtml");
  return 1;
}
