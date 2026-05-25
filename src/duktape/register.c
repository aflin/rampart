/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "register.h"
#include "core/module.h"
#include "globals/printf.h"
#include "globals/rampart-buffer.h"
#include "rampart.h"
#include "../include/version.h"
#include "rp_transpile.h"

void duk_rp_set_enum_false(duk_context *ctx, duk_idx_t objidx, const char *propname)
{
    objidx=duk_normalize_index(ctx, objidx);
    duk_push_string(ctx, propname);
    duk_def_prop(ctx, objidx, DUK_DEFPROP_CLEAR_ENUMERABLE);
}


/* json parse helpers for cyclic */
static void json_resolve_path(duk_context *ctx, duk_idx_t root_idx, const char *path) {
    const char *p = path;
    const char *start;
    duk_uarridx_t aidx;

    duk_dup(ctx, root_idx);

    if (*p == '$') p++;
    while (*p) {
        if (*p == '.') {
            p++;
            start = p;
            while (*p && *p != '.' && *p != '[') p++;
            duk_push_lstring(ctx, start, (duk_size_t)(p - start));
            duk_get_prop(ctx, -2);
            duk_remove(ctx, -2);
        } else if (*p == '[') {
            p++;
            aidx = (duk_uarridx_t)atoi(p);
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            duk_get_prop_index(ctx, -1, aidx);
            duk_remove(ctx, -2);
        } else {
            p++;
        }
    }
}

static void json_restore(duk_context *ctx, duk_idx_t obj_idx, duk_idx_t root_idx) {
    duk_idx_t enum_idx;

    if (!duk_is_object(ctx, obj_idx) || duk_is_function(ctx, obj_idx))
        return;

    obj_idx = duk_normalize_index(ctx, obj_idx);

    duk_enum(ctx, obj_idx, 0);
    enum_idx = duk_normalize_index(ctx, -1);

    while (duk_next(ctx, enum_idx, 1)) {
        /* stack: ... enum key value */
        if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "_cyclic_ref");
            if (duk_is_string(ctx, -1)) {
                /* stack: ... enum key placeholder ref_string */
                json_resolve_path(ctx, root_idx, duk_get_string(ctx, -1));
                /* stack: ... enum key placeholder ref_string target */
                duk_remove(ctx, -2);  /* remove ref_string */
                duk_remove(ctx, -2);  /* remove placeholder */
                /* stack: ... enum key target */
                duk_put_prop(ctx, obj_idx);
            } else {
                duk_pop(ctx);   /* pop undefined (no _cyclic_ref) */
                json_restore(ctx, -1, root_idx);
                duk_pop_2(ctx); /* pop key and value */
            }
        } else {
            duk_pop_2(ctx); /* pop key and value */
        }
    }
    duk_pop(ctx); /* pop enum */
}


/* allow JSON.parse to accept buffers */
duk_ret_t duk_rp_json_parse(duk_context *ctx)
{
    // normally its JSON.parse(text, reviver) where reviver must be callable.
    // here if reviver is true, we restore, e.g. { "_cyclic_ref": "$.a" }
    int do_restore= duk_get_boolean_default(ctx, 1, 0);

    if(duk_is_buffer_data(ctx,0))
        duk_buffer_to_string(ctx,0);
    duk_get_global_string(ctx, "JSON");
    duk_get_prop_string(ctx, -1, "_parse_orig");
    duk_insert(ctx, 0);
    duk_pop(ctx);//"JSON"
    duk_call(ctx,2);

    // undo any cyclic references that printf('%!J', ...) might produce
    if(do_restore)
    {
        duk_idx_t idx = duk_normalize_index(ctx, -1);
        json_restore(ctx, idx, idx);
    }
    return 1;
}

void fix_json_parse(duk_context *ctx)
{
    duk_get_global_string(ctx, "JSON");
    duk_get_prop_string(ctx, -1, "parse");
    duk_put_prop_string(ctx, -2, "_parse_orig");

    duk_push_c_function(ctx, duk_rp_json_parse, 2);
    duk_put_prop_string(ctx, -2, "parse");

    duk_rp_set_enum_false(ctx, -1, "_parse_orig");
    duk_pop(ctx);
}

duk_ret_t duk_rp_values_from_object(duk_context *ctx, duk_idx_t idx)
{
    duk_uarridx_t i=0;

    idx = duk_normalize_index(ctx, idx);
    if (duk_is_array(ctx,idx))
        return 1;

    duk_push_array(ctx);
    if(duk_is_string(ctx, idx))
    {
        const char *s = duk_get_string(ctx, idx);
        while(*s)
        {
            duk_push_lstring(ctx, s, 1);
            duk_put_prop_index(ctx, -2, i);
            s++;
            i++; 
        }
        return 1;
    }
    if(duk_is_object(ctx, idx))
    {
        duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY|DUK_ENUM_NO_PROXY_BEHAVIOR);
        while (duk_next(ctx, -1 , 1 ))
        {
            duk_put_prop_index(ctx, -4, i);
            i++;
            duk_pop(ctx);
        }
        duk_pop(ctx);
        return 1;
    }
    if(duk_is_number(ctx, idx)|| duk_is_buffer_data(ctx,idx))
        return 1;
    if( duk_is_undefined(ctx, idx) || duk_is_null(ctx, idx) )
        RP_THROW(ctx, "Object.values - Cannot convert undefined or null to object");

    RP_THROW(ctx, "Object.values - Cannot convert to object");

    return 0;
}



static duk_ret_t duk_rp_object_values(duk_context *ctx)
{
    return duk_rp_values_from_object(ctx, 0);
}

static void add_object_values(duk_context *ctx)
{
    duk_get_global_string(ctx, "Object");
    duk_push_c_function(ctx, duk_rp_object_values, 1);
    duk_put_prop_string(ctx, -2, "values");
    duk_rp_set_enum_false(ctx, -1, "values");

    duk_pop(ctx);
}

static int duk_rp_compare(duk_context *ctx, duk_idx_t idx1, duk_idx_t idx2)
{
    if(duk_get_type(ctx,idx1) != duk_get_type(ctx,idx2))
        return 0;

    switch(duk_get_type(ctx,idx1))
    {
        case DUK_TYPE_NUMBER:
        {
            if(duk_get_number(ctx,idx1) == duk_get_number(ctx,idx2))
                return 1;
            else if (duk_is_nan(ctx,idx1) && duk_is_nan(ctx,idx2))
                return 1;
            break;
        }
        case DUK_TYPE_BOOLEAN:
        {
            if(duk_get_boolean(ctx,idx1) == duk_get_boolean(ctx,idx2))
                return 1;
            break;
        }
        case DUK_TYPE_UNDEFINED:
        {
            return 1;
        }
        case DUK_TYPE_NULL:
        {
            return 1;
        }
        default:
        {
            void *v1=duk_get_heapptr(ctx,idx1), *v2=duk_get_heapptr(ctx,idx2);
            return (v1==v2);
        }
    }
    return 0;
}

static duk_ret_t duk_rp_array_includes(duk_context *ctx)
{
    int i=0, len;

    duk_push_this(ctx);
    len=duk_get_length(ctx, -1);

    if(duk_is_number(ctx, 1))
        i=duk_get_int(ctx, 1);
    if(i<0)i=0;

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        if (duk_rp_compare(ctx, 0, -1))
        {
            duk_push_true(ctx);
            return 1;
        }
        duk_pop(ctx);
    }

    duk_push_false(ctx);
    return 1;
}

duk_ret_t duk_rp_array_find(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Array.find - argument must be a Function");
    duk_bool_t res;
    duk_uarridx_t i=0, len;

    duk_push_this(ctx);
    len = duk_get_length(ctx, -1);

    for(i=0;i<len;i++)
    {
        duk_dup(ctx,0);
        duk_get_prop_index(ctx, 1, i);
        duk_call(ctx, 1);
        res=duk_to_boolean(ctx, -1);
        // bug fix: added duk_pop(ctx) after duk_to_boolean in array_find - 2026-02-27
        duk_pop(ctx);
        if(res)
        {
            duk_get_prop_index(ctx, 1, i);
            return 1;
        }
    }
    return 0;
}

duk_ret_t duk_rp_array_find_index(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Array.find - argument must be a Function");
    duk_bool_t res;
    duk_uarridx_t i=0, len;

    duk_push_this(ctx);
    len = duk_get_length(ctx, -1);

    for(i=0;i<len;i++)
    {
        duk_dup(ctx,0);
        duk_get_prop_index(ctx, 1, i);
        duk_call(ctx, 1);
        res=duk_to_boolean(ctx, -1);
        // bug fix: added duk_pop(ctx) after duk_to_boolean in array_find_index - 2026-02-27
        duk_pop(ctx);
        if(res)
        {
            duk_push_number(ctx, (int)i);
            return 1;
        }
    }
    duk_push_number(ctx, -1);
    return 1;
}

/* ——— Array.from(iterable, mapFn?) ——— */
static duk_ret_t duk_rp_array_from(duk_context *ctx)
{
    duk_idx_t top = duk_get_top(ctx);
    int has_map = (top >= 2 && duk_is_function(ctx, 1));
    duk_uarridx_t i = 0;

    duk_push_array(ctx); /* result array */
    duk_idx_t arr_idx = duk_normalize_index(ctx, -1);

    if (duk_is_string(ctx, 0))
    {
        /* iterate codepoints */
        duk_size_t slen = duk_get_length(ctx, 0);
        for (i = 0; i < (duk_uarridx_t)slen; i++)
        {
            duk_get_prop_index(ctx, 0, i); /* string char */
            if (has_map)
            {
                duk_dup(ctx, 1);
                duk_insert(ctx, -2);
                duk_push_uint(ctx, i);
                duk_call(ctx, 2);
            }
            duk_put_prop_index(ctx, arr_idx, i);
        }
    }
    else if (duk_is_array(ctx, 0))
    {
        duk_size_t len = duk_get_length(ctx, 0);
        for (i = 0; i < (duk_uarridx_t)len; i++)
        {
            duk_get_prop_index(ctx, 0, i);
            if (has_map)
            {
                duk_dup(ctx, 1);
                duk_insert(ctx, -2);
                duk_push_uint(ctx, i);
                duk_call(ctx, 2);
            }
            duk_put_prop_index(ctx, arr_idx, i);
        }
    }
    else if (duk_is_object(ctx, 0))
    {
        /* check for Symbol.iterator */
        int has_iter = 0;
        duk_get_global_string(ctx, "Symbol");
        if (!duk_is_undefined(ctx, -1))
        {
            duk_get_prop_string(ctx, -1, "iterator");
            if (!duk_is_undefined(ctx, -1))
            {
                duk_get_prop(ctx, 0); /* obj[Symbol.iterator] */
                if (duk_is_function(ctx, -1))
                {
                    has_iter = 1;
                    duk_dup(ctx, 0);
                    duk_call_method(ctx, 0); /* call iterator() */
                    duk_idx_t iter_idx = duk_normalize_index(ctx, -1);
                    for (;;)
                    {
                        duk_get_prop_string(ctx, iter_idx, "next");
                        duk_dup(ctx, iter_idx);
                        duk_call_method(ctx, 0);
                        duk_get_prop_string(ctx, -1, "done");
                        int done = duk_to_boolean(ctx, -1);
                        duk_pop(ctx);
                        if (done) { duk_pop(ctx); break; }
                        duk_get_prop_string(ctx, -1, "value");
                        duk_remove(ctx, -2); /* remove result obj */
                        if (has_map)
                        {
                            duk_dup(ctx, 1);
                            duk_insert(ctx, -2);
                            duk_push_uint(ctx, i);
                            duk_call(ctx, 2);
                        }
                        duk_put_prop_index(ctx, arr_idx, i);
                        i++;
                    }
                    duk_pop(ctx); /* iter */
                }
                else
                    duk_pop(ctx);
            }
            else
                duk_pop(ctx);
        }
        duk_pop(ctx); /* Symbol */

        if (!has_iter)
        {
            /* array-like: has .length */
            if (duk_has_prop_string(ctx, 0, "length"))
            {
                duk_get_prop_string(ctx, 0, "length");
                duk_size_t len = (duk_size_t)duk_to_uint(ctx, -1);
                duk_pop(ctx);
                for (i = 0; i < (duk_uarridx_t)len; i++)
                {
                    duk_get_prop_index(ctx, 0, i);
                    if (has_map)
                    {
                        duk_dup(ctx, 1);
                        duk_insert(ctx, -2);
                        duk_push_uint(ctx, i);
                        duk_call(ctx, 2);
                    }
                    duk_put_prop_index(ctx, arr_idx, i);
                }
            }
        }
    }
    return 1;
}

/* ——— Array.of(...args) ——— */
static duk_ret_t duk_rp_array_of(duk_context *ctx)
{
    duk_idx_t nargs = duk_get_top(ctx);
    duk_push_array(ctx);
    for (duk_idx_t i = 0; i < nargs; i++)
    {
        duk_dup(ctx, i);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

/* ——— String.prototype.trimStart / trimEnd ——— */
static duk_ret_t duk_rp_string_trim_start(duk_context *ctx)
{
    duk_push_this(ctx);
    const char *s = duk_to_string(ctx, -1);
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v')
        s++;
    duk_push_string(ctx, s);
    return 1;
}

static duk_ret_t duk_rp_string_trim_end(duk_context *ctx)
{
    duk_push_this(ctx);
    const char *s = duk_to_string(ctx, -1);
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' ||
                       s[len-1] == '\r' || s[len-1] == '\f' || s[len-1] == '\v'))
        len--;
    duk_push_lstring(ctx, s, len);
    return 1;
}

/* ——— Array.prototype.flat(depth?) ——— */
static void duk_rp_flat_recursive(duk_context *ctx, duk_idx_t src_idx, duk_idx_t dst_idx,
                                  duk_uarridx_t *out_i, int depth)
{
    duk_size_t len = duk_get_length(ctx, src_idx);
    for (duk_uarridx_t i = 0; i < (duk_uarridx_t)len; i++)
    {
        duk_get_prop_index(ctx, src_idx, i);
        if (depth > 0 && duk_is_array(ctx, -1))
        {
            duk_idx_t sub_idx = duk_normalize_index(ctx, -1);
            duk_rp_flat_recursive(ctx, sub_idx, dst_idx, out_i, depth - 1);
            duk_pop(ctx);
        }
        else
        {
            duk_put_prop_index(ctx, dst_idx, (*out_i)++);
        }
    }
}

static duk_ret_t duk_rp_array_flat(duk_context *ctx)
{
    int depth = 1;
    if (duk_is_number(ctx, 0))
    {
        double d = duk_get_number(ctx, 0);
        if (d == (double)INFINITY || d > 1000000)
            depth = 1000000;
        else
            depth = (int)d;
    }

    duk_push_this(ctx);
    duk_idx_t src_idx = duk_normalize_index(ctx, -1);

    duk_push_array(ctx);
    duk_idx_t dst_idx = duk_normalize_index(ctx, -1);

    duk_uarridx_t out_i = 0;
    duk_rp_flat_recursive(ctx, src_idx, dst_idx, &out_i, depth);

    return 1;
}

/* ——— Array.prototype.flatMap(fn) ——— */
static duk_ret_t duk_rp_array_flat_map(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Array.flatMap - argument must be a Function");
    duk_push_this(ctx);
    duk_idx_t this_idx = duk_normalize_index(ctx, -1);
    duk_size_t len = duk_get_length(ctx, this_idx);

    duk_push_array(ctx);
    duk_idx_t dst_idx = duk_normalize_index(ctx, -1);
    duk_uarridx_t out_i = 0;

    for (duk_uarridx_t i = 0; i < (duk_uarridx_t)len; i++)
    {
        duk_dup(ctx, 0); /* fn */
        duk_get_prop_index(ctx, this_idx, i);
        duk_push_uint(ctx, i);
        duk_dup(ctx, this_idx);
        duk_call(ctx, 3);

        if (duk_is_array(ctx, -1))
        {
            duk_idx_t sub_idx = duk_normalize_index(ctx, -1);
            duk_size_t slen = duk_get_length(ctx, sub_idx);
            for (duk_uarridx_t j = 0; j < (duk_uarridx_t)slen; j++)
            {
                duk_get_prop_index(ctx, sub_idx, j);
                duk_put_prop_index(ctx, dst_idx, out_i++);
            }
            duk_pop(ctx);
        }
        else
        {
            duk_put_prop_index(ctx, dst_idx, out_i++);
        }
    }

    return 1;
}

/* ——— String.prototype.replaceAll(search, replacement) ——— */
static duk_ret_t duk_rp_string_replace_all(duk_context *ctx)
{
    duk_push_this(ctx);
    const char *haystack = duk_to_string(ctx, -1);
    const char *needle = duk_require_string(ctx, 0);
    const char *replacement = duk_require_string(ctx, 1);
    size_t nlen = strlen(needle);
    size_t rlen = strlen(replacement);

    if (nlen == 0)
    {
        /* empty search: insert replacement between every char */
        rp_string *out = rp_string_new(strlen(haystack) * 2);
        const char *p = haystack;
        while (*p)
        {
            rp_string_putsn(out, replacement, rlen);
            rp_string_putsn(out, p, 1);
            p++;
        }
        rp_string_putsn(out, replacement, rlen);
        duk_push_string(ctx, out->str);
        rp_string_free(out);
        return 1;
    }

    rp_string *out = rp_string_new(strlen(haystack));
    const char *p = haystack;
    while (*p)
    {
        if (strncmp(p, needle, nlen) == 0)
        {
            rp_string_putsn(out, replacement, rlen);
            p += nlen;
        }
        else
        {
            rp_string_putsn(out, p, 1);
            p++;
        }
    }
    duk_push_string(ctx, out->str);
    rp_string_free(out);
    return 1;
}

/* ——— Array.prototype.at(index) ——— */
static duk_ret_t duk_rp_array_at(duk_context *ctx)
{
    int idx = duk_require_int(ctx, 0);
    duk_push_this(ctx);
    int len = (int)duk_get_length(ctx, -1);
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len)
        return 0; /* undefined */
    duk_get_prop_index(ctx, -1, (duk_uarridx_t)idx);
    return 1;
}

/* ——— Object.hasOwn(obj, prop) ——— */
static duk_ret_t duk_rp_object_has_own(duk_context *ctx)
{
    duk_require_object(ctx, 0);
    const char *prop = duk_require_string(ctx, 1);
    duk_push_boolean(ctx, duk_has_prop_string(ctx, 0, prop) &&
                          !duk_get_prop_string(ctx, 0, "__proto__")); /* quick check */

    /* proper hasOwnProperty check */
    duk_pop(ctx); /* pop the boolean we just pushed */
    duk_get_global_string(ctx, "Object");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_get_prop_string(ctx, -1, "hasOwnProperty");
    duk_dup(ctx, 0); /* obj as this */
    duk_push_string(ctx, prop);
    duk_call_method(ctx, 1);
    return 1;
}

/* ——— Array.prototype.findLast(fn) ——— */
static duk_ret_t duk_rp_array_find_last(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Array.findLast - argument must be a Function");
    duk_push_this(ctx);
    int len = (int)duk_get_length(ctx, -1);

    for (int i = len - 1; i >= 0; i--)
    {
        duk_dup(ctx, 0);
        duk_get_prop_index(ctx, 1, (duk_uarridx_t)i);
        duk_push_int(ctx, i);
        duk_dup(ctx, 1); /* this array */
        duk_call(ctx, 3);
        if (duk_to_boolean(ctx, -1))
        {
            duk_pop(ctx);
            duk_get_prop_index(ctx, 1, (duk_uarridx_t)i);
            return 1;
        }
        duk_pop(ctx);
    }
    return 0;
}

/* ——— Array.prototype.findLastIndex(fn) ——— */
static duk_ret_t duk_rp_array_find_last_index(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Array.findLastIndex - argument must be a Function");
    duk_push_this(ctx);
    int len = (int)duk_get_length(ctx, -1);

    for (int i = len - 1; i >= 0; i--)
    {
        duk_dup(ctx, 0);
        duk_get_prop_index(ctx, 1, (duk_uarridx_t)i);
        duk_push_int(ctx, i);
        duk_dup(ctx, 1);
        duk_call(ctx, 3);
        if (duk_to_boolean(ctx, -1))
        {
            duk_pop(ctx);
            duk_push_int(ctx, i);
            return 1;
        }
        duk_pop(ctx);
    }
    duk_push_int(ctx, -1);
    return 1;
}

/* ——— Object.fromEntries(iterable) ——— */
static duk_ret_t duk_rp_object_from_entries(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_idx_t obj_idx = duk_normalize_index(ctx, -1);

    if (duk_is_array(ctx, 0))
    {
        duk_size_t len = duk_get_length(ctx, 0);
        for (duk_uarridx_t i = 0; i < (duk_uarridx_t)len; i++)
        {
            duk_get_prop_index(ctx, 0, i); /* entry [key, value] */
            duk_get_prop_index(ctx, -1, 0); /* key */
            duk_get_prop_index(ctx, -2, 1); /* value */
            duk_put_prop(ctx, obj_idx); /* obj[key] = value */
            duk_pop(ctx); /* pop entry */
        }
    }
    return 1;
}

static void add_array_funcs(duk_context *ctx)
{
    duk_get_global_string(ctx, "Array");
    duk_get_prop_string(ctx, -1, "prototype");

    duk_push_c_function(ctx, duk_rp_array_find, 1);
    duk_put_prop_string(ctx, -2, "find");
    duk_rp_set_enum_false(ctx, -1, "find");

    duk_push_c_function(ctx, duk_rp_array_find_index, 1);
    duk_put_prop_string(ctx, -2, "findIndex");
    duk_rp_set_enum_false(ctx, -1, "findIndex");

    duk_push_c_function(ctx, duk_rp_array_includes, 2);
    duk_put_prop_string(ctx, -2, "includes");
    duk_rp_set_enum_false(ctx, -1, "includes");

    duk_push_c_function(ctx, duk_rp_array_flat, 1);
    duk_put_prop_string(ctx, -2, "flat");
    duk_rp_set_enum_false(ctx, -1, "flat");

    duk_push_c_function(ctx, duk_rp_array_flat_map, 1);
    duk_put_prop_string(ctx, -2, "flatMap");
    duk_rp_set_enum_false(ctx, -1, "flatMap");

    duk_push_c_function(ctx, duk_rp_array_at, 1);
    duk_put_prop_string(ctx, -2, "at");
    duk_rp_set_enum_false(ctx, -1, "at");

    duk_push_c_function(ctx, duk_rp_array_find_last, 1);
    duk_put_prop_string(ctx, -2, "findLast");
    duk_rp_set_enum_false(ctx, -1, "findLast");

    duk_push_c_function(ctx, duk_rp_array_find_last_index, 1);
    duk_put_prop_string(ctx, -2, "findLastIndex");
    duk_rp_set_enum_false(ctx, -1, "findLastIndex");

    duk_pop(ctx); /* pop prototype */

    /* Static methods on Array */
    duk_push_c_function(ctx, duk_rp_array_from, 2);
    duk_put_prop_string(ctx, -2, "from");
    duk_rp_set_enum_false(ctx, -1, "from");

    duk_push_c_function(ctx, duk_rp_array_of, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "of");
    duk_rp_set_enum_false(ctx, -1, "of");

    duk_pop(ctx); /* pop Array */
}

static void add_string_funcs(duk_context *ctx)
{
    duk_get_global_string(ctx, "String");
    duk_get_prop_string(ctx, -1, "prototype");

    duk_push_c_function(ctx, duk_rp_string_trim_start, 0);
    duk_put_prop_string(ctx, -2, "trimStart");
    duk_rp_set_enum_false(ctx, -1, "trimStart");

    duk_push_c_function(ctx, duk_rp_string_trim_end, 0);
    duk_put_prop_string(ctx, -2, "trimEnd");
    duk_rp_set_enum_false(ctx, -1, "trimEnd");

    duk_push_c_function(ctx, duk_rp_string_replace_all, 2);
    duk_put_prop_string(ctx, -2, "replaceAll");
    duk_rp_set_enum_false(ctx, -1, "replaceAll");

    duk_pop_2(ctx); /* pop prototype and String */
}

static void add_extra_object_funcs(duk_context *ctx)
{
    duk_get_global_string(ctx, "Object");

    duk_push_c_function(ctx, duk_rp_object_has_own, 2);
    duk_put_prop_string(ctx, -2, "hasOwn");
    duk_rp_set_enum_false(ctx, -1, "hasOwn");

    duk_push_c_function(ctx, duk_rp_object_from_entries, 1);
    duk_put_prop_string(ctx, -2, "fromEntries");
    duk_rp_set_enum_false(ctx, -1, "fromEntries");

    duk_pop(ctx); /* pop Object */
}

/* Buffer.from / Buffer.alloc / encoding-aware methods moved to
 * duktape/globals/rampart-buffer.c. Init is duk_rp_buffer_init(). */

/* TextEncoder/TextDecoder WHATWG-spec fixes live in
 * duktape/globals/rampart-textencoding.c. Init runs AFTER Buffer init
 * because TextDecoder is implemented in terms of Buffer.toString(enc). */
#include "globals/rampart-textencoding.h"

/* Node-style additions to the global console (time/table/group/count/clear).
 * Patches the global; benefits all rampart code. */
#include "globals/rampart-console.h"

/* Eager Promise install in vanilla rampart.  Same polyfill source the
 * transpiler emits under PROMISE_PF; available before any -t/-b. */
#include "globals/rampart-promise.h"
/* rampart-blob.h was moved into the rampart-whatwg.so build; no
 * longer included by the rampart binary.  Blob/File become available
 * on first access via the lazy getters installed in duk_init_context. */

/* Install a Duktape.errCreate hook that trims transpile-output parse
   errors.  When the transpiled source fails to parse, duktape's
   SyntaxError.message includes the *entire* source — for transpiled
   async/regenerator code that's 10–12 KB of `_TrN_Sp.load()`
   boilerplate followed by the user's source, with `^` at the failure
   position.  The relevant context is buried.  The hook strips the
   boilerplate prefix and clips to a window around `^` so the visible
   error starts at the user's source.  Fires for *every* Error object
   created in the heap, but only modifies the message when the marker
   `_TrN_Sp.load();` is present — so unrelated errors pass through.
   NDE.4 in transpiler-todo.md. */
static void install_transpile_err_trim(duk_context *ctx)
{
    const char *src =
        "Duktape.errCreate = function(err) {"
        "  try {"
        "    if (!err || typeof err.message !== 'string') return err;"
        "    var marker = '_TrN_Sp.load();';"
        "    var idx = err.message.indexOf(marker);"
        "    if (idx === -1) return err;"
        "    var rest = err.message.substring(idx + marker.length);"
        /* Skip any pragma / whitespace immediately after the marker. */
        "    rest = rest.replace(/^[\\s\"']+(use [^\"']*[\"'])?\\s*/, '');"
        /* If a `^` (parse-position caret) is in the trimmed text,
           clip to a ±300 char window around it so the error fits
           on screen.  Otherwise show the first 600 chars. */
        "    var car = rest.indexOf('\\n^');"
        "    var prefix = '', suffix = '';"
        "    if (car === -1) car = rest.indexOf('^');"
        "    if (car >= 0) {"
        "      var s = Math.max(0, car - 300);"
        "      var e = Math.min(rest.length, car + 300);"
        "      if (s > 0) prefix = '... ';"
        "      if (e < rest.length) suffix = ' ...';"
        "      rest = rest.substring(s, e);"
        "    } else if (rest.length > 600) {"
        "      rest = rest.substring(0, 600);"
        "      suffix = ' ...';"
        "    }"
        "    err.message = 'SyntaxError in transpiled output' +"
        "                  (car >= 0 ? ' (context around ^)' : '') +"
        "                  ':\\n' + prefix + rest + suffix;"
        "  } catch (_e) {}"
        "  return err;"
        "};";
    if (duk_peval_string(ctx, src) != 0)
        fprintf(stderr, "errCreate trim install failed: %s\n",
                duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
}

static duk_ret_t rp_eval_js(duk_context *ctx)
{
    const char *source=NULL;
    const char *bfn=NULL;

    if(!duk_is_string(ctx,0))
    {
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));
        duk_insert(ctx, 0);
        duk_call(ctx, duk_get_top_index(ctx));
        return 1;
    }

    source=duk_get_string(ctx, 0);

    struct timespec tsnow;
    clock_gettime(CLOCK_REALTIME, &tsnow);

    // get orig eval from DUK_HIDDEN_SYMBOL("buildin_eval")
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));

    // main_babel_opt is non null if this script was previously babelized.
    if ( !main_babel_opt || ! (bfn=duk_rp_babelize(ctx, "eval_code", (char*)source, tsnow.tv_sec, babel_setting_nostrict, main_babel_opt)) )
    {
        /*
        int err=0, lineno=0;
        char *tickified = tickify(source, strlen(source), &err, &lineno);
        if (err)
        {
            RP_THROW(ctx, "SyntaxError: %s (line %d of eval code)\n", tickify_err(err), lineno);
        }

        duk_push_string(ctx, tickified);
        free(tickified);
        */
        /* Only transpile if -t was passed or source has "use transpiler".
           Otherwise tickify (template-literal processing only).  This
           keeps eval'd code free of `_TrN_Sp._fs(...)` wrappers in the
           common no-transpiler case — important for worker threads that
           eval plain ES5 in fresh duktape heaps where _TrN_Sp may not
           be installed. */
        RP_ParseRes res = rp_get_transpiled_eval((char *)source, NULL);

        if (res.err)
        {
            const char *emsg = res.errmsg ? res.errmsg : "Syntax error: parse error in eval";
            char *emcopy = emsg ? strdup(emsg) : NULL;
            freeParseRes(&res);
            RP_THROW(ctx, "%s", emcopy ? emcopy : "Syntax error: parse error in eval");
            if (emcopy) free(emcopy);
        }

        if(res.transpiled)
        {
            duk_push_string(ctx, res.transpiled);
        }
        else /* code unaltered */
            duk_push_string(ctx, source);

        freeParseRes(&res);

    }
    if(bfn)
        free((char*)bfn);
    duk_call(ctx,1);
    return 1;
}

static void fix_eval(duk_context *ctx)
{
    /* new eval code */
    // save orig eval to DUK_HIDDEN_SYMBOL("buildin_eval") if not already there
    if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval")))
    {
        duk_get_global_string(ctx, "eval");
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("buildin_eval"));
    }
    duk_pop(ctx);
    duk_push_c_function(ctx, rp_eval_js, 1);
    duk_put_global_string(ctx, "eval");
}

static duk_ret_t transpile_rewrite_args (duk_context *ctx)
{
    duk_idx_t i=0, top=duk_get_top(ctx);

    if(!top)
        duk_push_string(ctx, "function anonymous(){}");
    else
    {
        if(top==1)
        {
            if(!duk_is_string(ctx, 0))
                RP_SYNTAX_THROW(ctx, "parse error");
            duk_push_sprintf(ctx, "function anonymous(){%s}", duk_get_string(ctx,0) );
        }
        else 
        {

            duk_push_string(ctx, "function anonymous(");

            for(i=0;i<top; i++) {
                if(i==top-1)
                {
                    if(!duk_is_string(ctx, i))
                        RP_SYNTAX_THROW(ctx, "parse error");
                    duk_push_sprintf(ctx, "){%s}", duk_get_string(ctx, i));
                }
                else
                {
                    if(!duk_is_string(ctx, i))
                        RP_SYNTAX_THROW(ctx, "expected identifier");
                    
                    if(i==0)
                        duk_dup(ctx, i);
                    else
                        duk_push_sprintf(ctx, ", %s", duk_get_string(ctx, i));
                }
            }
            duk_concat(ctx, top+1);
        }
        const char *src = duk_get_string(ctx,-1);

        /* Disable fn-sources for `new Function(...)` bodies:
           1. `_TrN_Sp._fs(...)` wrapper calls would emit AFTER the
              function declaration, turning the source into multiple
              statements which DUK_COMPILE_FUNCTION rejects.
           2. The FN_SOURCE_PF polyfill preamble would be prepended
              (when polysdone hasn't yet recorded it), also breaking
              DUK_COMPILE_FUNCTION's single-expression requirement.
           3. A `new Function`-built function's "source" is just the
              body again — not useful for debugging.
           The body still gets full ES2015+ transpilation (async, gen,
           classes, etc.) — just no `__source__` attachment. */
        char *free_src=strdup(src);
        RP_ParseRes res = rp_get_transpiled_no_fn_sources(free_src, NULL);
        free(free_src);

        if (!res.err && res.transpiled)
        {
            /* The transpiler always emits a `if(!global._TrN_Sp){…};
               _TrN_Sp.load();` preamble. duk_compile(DUK_COMPILE_FUNCTION)
               requires a single function expression — preamble bytes
               ahead of the function break that. Eval the preamble
               first (so polyfills the body needs but the outer script
               didn't are installed), then compile only the function
               expression part. `_TrN_Sp.load()` is idempotent and only
               adds polys, so re-running it across new-Function calls
               doesn't undo anything. */
            const char *out = res.transpiled;
            const char *marker = ";_TrN_Sp.load();";
            const char *p = strstr(out, marker);
            if (p)
            {
                size_t preamble_end = (size_t)(p - out) + strlen(marker);
                duk_push_lstring(ctx, out, preamble_end);
                /* peval to surface errors without aborting; on success
                   pop the result; on failure pop the error. The
                   subsequent compile of the function expression will
                   throw if polys really were required. */
                (void)duk_peval(ctx);
                duk_pop(ctx);
                out = p + strlen(marker);
            }
            duk_pop(ctx); //src
            duk_push_string(ctx, out);
        }
        freeParseRes(&res);
    }

    duk_push_string(ctx, "anonymous");
    duk_compile(ctx, DUK_COMPILE_FUNCTION);

    //duk_eval(ctx);

    return 1;
}



/* Install `Proxy.revocable(target, handler)` polyfill — duktape has
   Proxy + Reflect but not the revocable factory. Returns
   `{proxy, revoke}` where calling `revoke()` causes every subsequent
   trap on `proxy` to throw TypeError, matching ES2015 spec. Wraps the
   caller's handler so each trap method first checks the revoked flag,
   then delegates to the user's trap (or Reflect.<trap> as fallback). */
/* JS-level polyfills for ES2020+ globals duktape doesn't provide.
   Each is gated on `typeof X !== 'function'` so a future duktape
   that adds them natively will skip the polyfill.  Costs ~120 bytes
   of source at context init; no runtime overhead unless called. */
static void install_modern_polyfills(duk_context *ctx)
{
    const char *src =
        /* String.prototype.matchAll(regex) — ES2020.  Returns an
           iterator over all matches of a global regex.  Spec throws
           TypeError if the regex is non-null and lacks the `g` flag. */
        "if (typeof String.prototype.matchAll !== 'function') {"
        "  Object.defineProperty(String.prototype, 'matchAll', {"
        "    configurable: true, writable: true, enumerable: false,"
        "    value: function(regex) {"
        "      if (regex != null && regex instanceof RegExp) {"
        "        if (regex.flags.indexOf('g') === -1)"
        "          throw new TypeError('String.prototype.matchAll requires a global RegExp');"
        "      }"
        "      var re = (regex != null && regex instanceof RegExp)"
        "               ? new RegExp(regex.source, regex.flags)"
        "               : new RegExp(String(regex == null ? '' : regex), 'g');"
        "      var str = String(this);"
        "      var done = false;"
        "      var iter = {"
        "        next: function() {"
        "          if (done) return { value: undefined, done: true };"
        "          var m = re.exec(str);"
        "          if (m === null) { done = true; return { value: undefined, done: true }; }"
        /* zero-width match: advance lastIndex by 1 to avoid infinite loop */
        "          if (m[0] === '') re.lastIndex = re.lastIndex + 1;"
        "          return { value: m, done: false };"
        "        }"
        "      };"
        "      if (typeof Symbol !== 'undefined' && Symbol.iterator)"
        "        iter[Symbol.iterator] = function() { return this; };"
        "      return iter;"
        "    }"
        "  });"
        "}"
        /* Object.groupBy(items, keyFn) — ES2024.  Returns a null-
           prototype object whose keys are the keyFn results and
           values are arrays of grouped items.  Accepts any iterable. */
        "if (typeof Object.groupBy !== 'function') {"
        "  Object.defineProperty(Object, 'groupBy', {"
        "    configurable: true, writable: true, enumerable: false,"
        "    value: function(items, keyFn) {"
        "      if (items == null) throw new TypeError('Object.groupBy: items must be iterable');"
        "      if (typeof keyFn !== 'function') throw new TypeError('Object.groupBy: keyFn must be callable');"
        "      var result = Object.create(null);"
        "      var i = 0;"
        "      if (typeof Symbol !== 'undefined' && Symbol.iterator"
        "          && typeof items[Symbol.iterator] === 'function') {"
        "        var it = items[Symbol.iterator]();"
        "        var step;"
        "        while (!(step = it.next()).done) {"
        "          var key = keyFn(step.value, i++);"
        "          if (!result[key]) result[key] = [];"
        "          result[key].push(step.value);"
        "        }"
        "      } else {"
        /* array-like fallback (length-based, skips holes) */
        "        var len = items.length >>> 0;"
        "        for (i = 0; i < len; i++) {"
        "          if (i in items) {"
        "            var k = keyFn(items[i], i);"
        "            if (!result[k]) result[k] = [];"
        "            result[k].push(items[i]);"
        "          }"
        "        }"
        "      }"
        "      return result;"
        "    }"
        "  });"
        "}"
        /* TypedArray.from / TypedArray.of (ES2015). Spec defines these
           on %TypedArray% but duktape has no shared base; install on
           each concrete constructor.  `this` is the constructor when
           called as Uint8Array.from(...). */
        "['Int8Array','Uint8Array','Uint8ClampedArray','Int16Array',"
        " 'Uint16Array','Int32Array','Uint32Array','Float32Array',"
        " 'Float64Array'].forEach(function (n) {"
        "  var C = globalThis[n];"
        "  if (!C) return;"
        "  if (typeof C.from !== 'function') {"
        "    Object.defineProperty(C, 'from', {"
        "      configurable: true, writable: true,"
        "      value: function (src, mapFn, thisArg) {"
        "        var arr = Array.from(src, mapFn, thisArg);"
        "        return new this(arr);"
        "      }"
        "    });"
        "  }"
        "  if (typeof C.of !== 'function') {"
        "    Object.defineProperty(C, 'of', {"
        "      configurable: true, writable: true,"
        "      value: function () {"
        "        var a = new Array(arguments.length);"
        "        for (var i = 0; i < arguments.length; i++) a[i] = arguments[i];"
        "        return new this(a);"
        "      }"
        "    });"
        "  }"
        "});"
        /* ArrayBuffer.prototype.transfer / transferToFixedLength / detached
           (ES2024).  Partial-spec polyfill: copies bytes into a fresh
           buffer and flips a JS-level _detached flag.  Tests that just
           check 'transfer() returns a new buffer with the right bytes'
           pass; tests that verify true detachment (the source's
           byteLength reads 0, and TypedArray views over it collapse to
           zero) still fail.  True detach needs a duktape engine patch:
           the AB's [[ArrayBufferByteLength]] is a cached slot that
           neither duk_resize_buffer nor duk_steal_buffer can reach
           through the public API, and views don't auto-track their
           parent AB's underlying buffer reference. */
        "if (typeof ArrayBuffer.prototype.transfer !== 'function') {"
        "  Object.defineProperty(ArrayBuffer.prototype, 'detached', {"
        "    configurable: true,"
        "    get: function () { return this._detached === true; }"
        "  });"
        "  function __abTransfer(self, newLength) {"
        "    if (self._detached) {"
        "      var de = new TypeError('ArrayBuffer is detached');"
        "      de.name = 'TypeError'; throw de;"
        "    }"
        "    var len = (newLength === undefined) ? self.byteLength : (newLength|0);"
        "    if (len < 0) throw new RangeError('Invalid length');"
        "    var dst = new ArrayBuffer(len);"
        "    var src = new Uint8Array(self);"
        "    var view = new Uint8Array(dst);"
        "    var copy = Math.min(src.length, len);"
        "    for (var i = 0; i < copy; i++) view[i] = src[i];"
        "    Object.defineProperty(self, '_detached', "
        "      {value: true, writable: false, configurable: true, enumerable: false});"
        "    return dst;"
        "  }"
        "  Object.defineProperty(ArrayBuffer.prototype, 'transfer', {"
        "    configurable: true, writable: true,"
        "    value: function (newLength) { return __abTransfer(this, newLength); }"
        "  });"
        "  Object.defineProperty(ArrayBuffer.prototype, 'transferToFixedLength', {"
        "    configurable: true, writable: true,"
        "    value: function (newLength) { return __abTransfer(this, newLength); }"
        "  });"
        "}"
        /* Array.prototype.fill(value, start?, end?) — ES2015.  Fills
           [start, end) in-place with `value`; negative indices wrap from
           length.  Required by WPT tests that construct fixed-size
           buffers via `Array(N).fill(0)`. */
        "if (typeof Array.prototype.fill !== 'function') {"
        "  Object.defineProperty(Array.prototype, 'fill', {"
        "    configurable: true, writable: true, enumerable: false,"
        "    value: function (value, start, end) {"
        "      var len = this.length >>> 0;"
        "      var s = start === undefined ? 0 : (start | 0);"
        "      var e = end === undefined ? len : (end | 0);"
        "      if (s < 0) s = Math.max(len + s, 0); else s = Math.min(s, len);"
        "      if (e < 0) e = Math.max(len + e, 0); else e = Math.min(e, len);"
        "      for (var i = s; i < e; i++) this[i] = value;"
        "      return this;"
        "    }"
        "  });"
        "}";
    if (duk_peval_string(ctx, src) != 0)
    {
        fprintf(stderr, "modern polyfills install failed: %s\n", duk_safe_to_string(ctx, -1));
    }
    duk_pop(ctx);
}

/* Install Array.prototype.{keys,values,entries,[Symbol.iterator]}.
   Duktape's stock Array prototype is missing the iteration spec
   surface entirely — plain arrays aren't iterable, so `for (x of arr)`,
   `[...arr]`, `new Set(arr)` all fail.  Pure addition: every slot
   below is currently `undefined`, so installing them can't displace
   any existing behavior.  Per spec, Array.prototype[Symbol.iterator]
   IS Array.prototype.values (same function reference). */
static void install_array_iter(duk_context *ctx)
{
    const char *src =
        "if (typeof Array.prototype[Symbol.iterator] !== 'function') {"
        "  var _arrIter = function(self, kind) {"
        "    var i = 0;"
        "    var iter = {"
        "      next: function() {"
        "        if (i >= self.length) return {value: undefined, done: true};"
        "        var idx = i++;"
        "        var v = (kind === 0) ? idx"
        "              : (kind === 1) ? self[idx]"
        "              :                [idx, self[idx]];"
        "        return {value: v, done: false};"
        "      }"
        "    };"
        "    iter[Symbol.iterator] = function() { return this; };"
        "    return iter;"
        "  };"
        "  var _def = function(name, fn) {"
        "    Object.defineProperty(Array.prototype, name,"
        "      {value: fn, writable: true, enumerable: false, configurable: true});"
        "  };"
        "  _def('keys',    function() { return _arrIter(this, 0); });"
        "  var _valuesFn  = function() { return _arrIter(this, 1); };"
        "  _def('values',  _valuesFn);"
        "  _def('entries', function() { return _arrIter(this, 2); });"
        "  Object.defineProperty(Array.prototype, Symbol.iterator,"
        "    {value: _valuesFn, writable: true, enumerable: false, configurable: true});"
        "}";
    if (duk_peval_string(ctx, src) != 0)
    {
        fprintf(stderr, "Array iterator install failed: %s\n", duk_safe_to_string(ctx, -1));
    }
    duk_pop(ctx);
}

static void install_proxy_revocable(duk_context *ctx)
{
    const char *src =
        "if(typeof Proxy==='function' && !Proxy.revocable){"
            "Proxy.revocable=function(target,handler){"
                "if(target==null||(typeof target!=='object'&&typeof target!=='function'))"
                    "throw new TypeError('Cannot create proxy with a non-object as target');"
                "if(handler==null||typeof handler!=='object')"
                    "throw new TypeError('Cannot create proxy with a non-object as handler');"
                "var revoked=false;"
                "var traps=['getPrototypeOf','setPrototypeOf','isExtensible','preventExtensions','getOwnPropertyDescriptor','defineProperty','has','get','set','deleteProperty','ownKeys','apply','construct'];"
                "var wrapped={};"
                "traps.forEach(function(t){"
                    "wrapped[t]=function(){"
                        "if(revoked)throw new TypeError(\"Cannot perform '\"+t+\"' on a proxy that has been revoked\");"
                        "var fn=handler[t];"
                        "if(typeof fn==='function')return fn.apply(handler,arguments);"
                        "if(typeof Reflect!=='undefined'&&typeof Reflect[t]==='function')"
                            "return Reflect[t].apply(Reflect,arguments);"
                        /* minimal fallbacks for the common traps when Reflect lacks them */
                        "var a=arguments;"
                        "if(t==='get')return a[0][a[1]];"
                        "if(t==='set'){a[0][a[1]]=a[2];return true;}"
                        "if(t==='has')return a[1] in a[0];"
                        "if(t==='deleteProperty'){delete a[0][a[1]];return true;}"
                        "if(t==='ownKeys')return Object.getOwnPropertyNames(a[0]);"
                        "if(t==='getOwnPropertyDescriptor')return Object.getOwnPropertyDescriptor(a[0],a[1]);"
                        "throw new TypeError('Proxy trap \"'+t+'\" not available');"
                    "};"
                "});"
                "return {proxy:new Proxy(target,wrapped),revoke:function(){revoked=true;}};"
            "};"
        "}";
    if (duk_peval_string(ctx, src) != 0)
    {
        fprintf(stderr, "Proxy.revocable install failed: %s\n", duk_safe_to_string(ctx, -1));
    }
    duk_pop(ctx);
}

static void new_function_transpile(duk_context *ctx) {
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "Function");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_put_prop_string(ctx, -3, "FunctionES5");
    duk_push_c_function(ctx, transpile_rewrite_args, DUK_VARARGS);
    duk_pull(ctx, -2);
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "Function");
}


void duk_init_context(duk_context *ctx)
{
    /* https://wiki.duktape.org/howtoglobalobjectreference */
    /*char globdef[]="if (typeof global === 'undefined') {(function () {var global = new Function('return this;')();Object.defineProperty(global, 'global', {value: global,writable: true,enumerable: false,configurable: true});})()}";
    
    if (duk_pcompile_string(ctx, 0, globdef) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
    }
    if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
    }
    duk_pop(ctx);
    */

    //maybe just do this?
    // printf("%J, obj) can handle global.global == global
    duk_push_global_object(ctx);
    duk_put_global_string(ctx,"global");


    duk_console_init(ctx, DUK_CONSOLE_FLUSH); /* register console.log, console.error etc. */
    duk_module_init(ctx);                     /* register require() function */
    duk_printf_init(ctx);                     /* register the printf and sprintf functions from printf.c */
    duk_misc_init(ctx);                       /* register functions in rampart-utils.c */
    duk_rp_url_init(ctx);                     /* rampart.utils.urlComponents/absUrl/toASCII/toUnicode via upa-url */
    duk_import_init(ctx);                     /* register functions in rampart-import.c */
    duk_process_init(ctx);                    /* register process.* vars */
    duk_event_init(ctx);                      /* register functions in rampart-event.c */
    duk_thread_init(ctx);                     /* register functions in rampart-thread.c */
    duk_vector_init(ctx);                     /* register functions in rampart-vector.c */
    duk_rp_push_rampart_version(ctx);         /* rampart version info */
    fix_json_parse(ctx);
    fix_eval(ctx);
    add_object_values(ctx);
    add_array_funcs(ctx);
    add_string_funcs(ctx);
    add_extra_object_funcs(ctx);
    duk_rp_buffer_init(ctx);
    duk_rp_textencoding_init(ctx);
    duk_rp_console_init(ctx);
    new_function_transpile(ctx);
    install_proxy_revocable(ctx);
    install_modern_polyfills(ctx);
    install_array_iter(ctx);
    install_transpile_err_trim(ctx);
    duk_rp_promise_init(ctx);
    /* WHATWG / W3C Web platform standards live in rampart-whatwg.so
     * (Blob, File, URL, URLSearchParams, Event, EventTarget,
     * CustomEvent, AbortController, AbortSignal, structuredClone,
     * atob, btoa, MessageChannel, MessagePort, BroadcastChannel,
     * queueMicrotask, reportError, performance extras, crypto
     * (Web Crypto), navigator).
     *
     * Same lazy-load pattern as Intl below: install a configurable
     * getter on globalThis for each name; first access deletes ALL
     * the getters together, require()s rampart-whatwg (which installs
     * the real values), and returns the now-real value.  Scripts that
     * never touch a WHATWG global never load the .so. */
    duk_eval_string_noresult(ctx,
        "(function(){"
        /*   NAMES excludes 'performance' because duktape already
             provides performance.now() — augmenting (not replacing)
             happens at whatwg load time (the PNAMES block below
             installs lazy getters on the existing performance object
             for the W3C extras). */
        "  var NAMES = ['Blob','File','FileReader','URL','URLSearchParams','URLPattern',"
        "               'Event','EventTarget','CustomEvent',"
        "               'MessageEvent','CloseEvent','ErrorEvent',"
        "               'ProgressEvent','PromiseRejectionEvent',"
        "               'AbortController','AbortSignal','DOMException',"
        "               'structuredClone','atob','btoa',"
        "               'MessageChannel','MessagePort','BroadcastChannel',"
        "               'queueMicrotask','reportError','navigator','location',"
        "               'WebSocket','EventSource',"
        "               'PerformanceObserver','PerformanceEntry','PerformanceMark','PerformanceMeasure',"
        "               'crypto','Crypto','SubtleCrypto','CryptoKey',"
        /* WHATWG Streams — installed by rampart-whatwg from the
           vendored web-streams-polyfill bundle. */
        "               'ReadableStream','ReadableStreamDefaultController',"
        "               'ReadableByteStreamController','ReadableStreamBYOBRequest',"
        "               'ReadableStreamDefaultReader','ReadableStreamBYOBReader',"
        "               'WritableStream','WritableStreamDefaultController',"
        "               'WritableStreamDefaultWriter',"
        "               'ByteLengthQueuingStrategy','CountQueuingStrategy',"
        "               'TransformStream','TransformStreamDefaultController',"
        /* Stream-flavored encoders / compression — built on top of
           TransformStream in rampart-whatwg's install JS. */
        "               'TextEncoderStream','TextDecoderStream',"
        "               'CompressionStream','DecompressionStream',"
        /* WHATWG Fetch — Headers/FormData/Request/Response are pure
           JS; fetch() lazy-requires rampart-curl on first call. */
        "               'Headers','FormData','Request','Response','fetch',"
        "               'XMLHttpRequest','WebSocketError',"
        /* Tier 3 — Storage/Cache (in-memory v1) */
        "               'Storage','localStorage','sessionStorage',"
        "               'Cache','CacheStorage','caches'];"
        "  NAMES.forEach(function(n){"
        "    Object.defineProperty(globalThis, n, {"
        "      configurable: true,"
        "      get: function(){"
        /*       Delete only THIS getter before requiring whatwg.  The
                 other getters stay in place; whatwg's install replaces
                 each one with a real value via defineProperty(...,
                 configurable:true).  Deleting all up-front would
                 leave (e.g.) `performance`/`Buffer`-like globals in
                 an unexpected state during nodeshim's load chain. */
        "        delete globalThis[n];"
        "        require('rampart-whatwg');"
        "        return globalThis[n];"
        "      },"
        /*    A setter is essential: user code like `var crypto =
              require('rampart-crypto')` at top level is an assignment
              to globalThis.crypto.  Without a setter, that assignment
              silently fails (non-strict) or throws (strict), because
              the lazy accessor is getter-only.  The setter replaces
              the accessor with a data property holding the user's
              value — exactly what a plain global would do.

              enumerable:true is important — rampart.thread copies
              globals via duk_enum (which by default skips non-
              enumerable), so a user-assigned value MUST be enumerable
              to propagate into spawned threads.  Matches the
              `{writable, enumerable, configurable}` flags that a
              top-level `var x = y` would create. */
        "      set: function(v){"
        "        delete globalThis[n];"
        "        Object.defineProperty(globalThis, n, {"
        "          value: v, writable: true, configurable: true, enumerable: true"
        "        });"
        "      }"
        "    });"
        "  });"
        /*   Augmentation case: duktape's `performance` object already
             exists with `.now()`.  Install lazy getters for the W3C
             Performance Timeline extras on the SAME object — first
             access to any triggers whatwg load, which augments the
             existing performance with the real methods. */
        "  if (typeof globalThis.performance === 'object' && globalThis.performance !== null) {\n"
        "    var P = globalThis.performance;\n"
        "    var PNAMES = ['mark','measure','clearMarks','clearMeasures',\n"
        "                  'getEntries','getEntriesByName','getEntriesByType',\n"
        "                  'PerformanceEntry','PerformanceMark','PerformanceMeasure',\n"
        "                  'timeOrigin'];\n"
        "    PNAMES.forEach(function(n){\n"
        "      if (n in P) return;  /* already defined */\n"
        "      Object.defineProperty(P, n, {\n"
        "        configurable: true,\n"
        "        get: function(){\n"
        "          delete P[n];\n"
        "          require('rampart-whatwg');\n"
        "          return P[n];\n"
        "        },\n"
        "        set: function(v){\n"
        "          delete P[n];\n"
        "          Object.defineProperty(P, n, {\n"
        "            value: v, writable: true, configurable: true, enumerable: true\n"
        "          });\n"
        "        }\n"
        "      });\n"
        "    });\n"
        "  }\n"
        "})();");
    /* Install `globalThis.Intl` as a lazy getter — `require('rampart-intl')`
       only fires on first access.  Two motivations:
         1. ~37 MB ICU data isn't paid for by scripts that never touch Intl.
         2. Bundled-rampart binaries don't always carry rampart-intl.so,
            so an eager require would FATAL the context init for any
            bundle (see test/bundle-test.js) — lazy makes the cost optional.
       rampart-intl's open-module entry assigns globalThis.Intl as a
       side-effect; we delete our getter first so the assignment isn't
       blocked, then return whatever it installed. */
    duk_eval_string_noresult(ctx,
        "Object.defineProperty(globalThis, 'Intl', {"
        "  configurable: true,"
        "  get: function() {"
        "    delete globalThis.Intl;"
        "    require('rampart-intl');"
        "    return globalThis.Intl;"
        "  }"
        "});");
    duk_map_set_init(ctx);
}
