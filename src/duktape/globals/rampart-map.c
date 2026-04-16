/* rampart-map.c — Map and Set implementations for Duktape
 *
 * Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <math.h>
#include "rampart.h"

/* Hidden symbol for internal storage: {keystr: {k:origKey, v:value}, ...}
   Duktape objects preserve string property insertion order, so no
   separate order array is needed. */
#define MAP_STORE  DUK_HIDDEN_SYMBOL("map_store")

/* ============================================================
   Key formatting: produce a unique string for any JS value
   ============================================================ */

static void map_push_key_string(duk_context *ctx, duk_idx_t idx)
{
    idx = duk_normalize_index(ctx, idx);

    switch (duk_get_type(ctx, idx)) {
    case DUK_TYPE_UNDEFINED:
        duk_push_string(ctx, "z:undef");
        return;
    case DUK_TYPE_NULL:
        duk_push_string(ctx, "z:null");
        return;
    case DUK_TYPE_BOOLEAN:
        duk_push_sprintf(ctx, "b:%d", duk_get_boolean(ctx, idx));
        return;
    case DUK_TYPE_NUMBER: {
        double n = duk_get_number(ctx, idx);
        if (isnan(n))
            duk_push_string(ctx, "n:NaN");
        else if (isinf(n))
            duk_push_string(ctx, n > 0 ? "n:Inf" : "n:-Inf");
        else if (n == 0.0)
            duk_push_string(ctx, "n:0");  /* normalize -0 to 0 */
        else
            duk_push_sprintf(ctx, "n:%.17g", n);
        return;
    }
    case DUK_TYPE_STRING:
        /* Symbols are DUK_TYPE_STRING internally but cannot be
           string-coerced.  Use heap pointer for identity. */
        if (duk_is_symbol(ctx, idx)) {
            void *ptr = duk_get_heapptr(ctx, idx);
            duk_push_sprintf(ctx, "y:%p", ptr);
            return;
        }
        duk_push_string(ctx, "s:");
        duk_dup(ctx, idx);
        duk_concat(ctx, 2);
        return;
    default: {
        void *ptr = duk_get_heapptr(ctx, idx);
        duk_push_sprintf(ctx, "p:%p", ptr);
        return;
    }
    }
}

/* ============================================================
   Map methods
   ============================================================ */

/* Helper: push the store object from 'this' */
static duk_idx_t map_push_store(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, MAP_STORE);
    duk_remove(ctx, -2);
    return duk_normalize_index(ctx, -1);
}

static duk_ret_t map_set(duk_context *ctx)
{
    /* args: 0=key, 1=value */
    duk_idx_t store = map_push_store(ctx);

    map_push_key_string(ctx, 0);
    const char *ks = duk_get_string(ctx, -1);

    /* If key already exists, delete first so re-insert goes to end
       only if we want "update moves to end" behavior.
       Spec says: update does NOT change order.  So just overwrite. */

    /* Create entry {k: key, v: value} */
    duk_push_object(ctx);
    duk_dup(ctx, 0);
    duk_put_prop_string(ctx, -2, "k");
    duk_dup(ctx, 1);
    duk_put_prop_string(ctx, -2, "v");
    duk_put_prop_string(ctx, store, ks);

    duk_pop_2(ctx); /* keystr, store */

    duk_push_this(ctx);
    return 1;
}

static duk_ret_t map_get(duk_context *ctx)
{
    duk_idx_t store = map_push_store(ctx);

    map_push_key_string(ctx, 0);
    if (duk_get_prop(ctx, store)) {
        duk_get_prop_string(ctx, -1, "v");
        return 1;
    }
    duk_push_undefined(ctx);
    return 1;
}

static duk_ret_t map_has(duk_context *ctx)
{
    duk_idx_t store = map_push_store(ctx);
    map_push_key_string(ctx, 0);
    duk_push_boolean(ctx, duk_has_prop(ctx, store));
    return 1;
}

static duk_ret_t map_delete(duk_context *ctx)
{
    duk_idx_t store = map_push_store(ctx);
    map_push_key_string(ctx, 0);
    const char *ks = duk_get_string(ctx, -1);

    if (!duk_has_prop_string(ctx, store, ks)) {
        duk_push_false(ctx);
        return 1;
    }
    duk_del_prop_string(ctx, store, ks);
    duk_push_true(ctx);
    return 1;
}

static duk_ret_t map_clear(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, MAP_STORE);
    duk_pop(ctx);
    return 0;
}

static duk_ret_t map_size_getter(duk_context *ctx)
{
    duk_idx_t store = map_push_store(ctx);
    int count = 0;
    duk_enum(ctx, store, DUK_ENUM_OWN_PROPERTIES_ONLY);
    while (duk_next(ctx, -1, 0)) { count++; duk_pop(ctx); }
    duk_push_int(ctx, count);
    return 1;
}

static duk_ret_t map_forEach(duk_context *ctx)
{
    /* args: 0=callback, 1=thisArg (optional) */
    REQUIRE_FUNCTION(ctx, 0, "Map.forEach: callback must be a function");
    duk_idx_t store = map_push_store(ctx);

    duk_enum(ctx, store, DUK_ENUM_OWN_PROPERTIES_ONLY);
    while (duk_next(ctx, -1, 1)) {
        /* stack: [store, enum, keystr, entry] */
        duk_get_prop_string(ctx, -1, "v");
        duk_get_prop_string(ctx, -2, "k");
        duk_remove(ctx, -3); /* remove entry */
        duk_remove(ctx, -3); /* remove keystr */
        /* stack: [store, enum, value, key] */

        /* build: callback.call(thisArg, value, key, map) */
        duk_dup(ctx, 0); /* callback */
        if (duk_is_undefined(ctx, 1))
            duk_push_undefined(ctx);
        else
            duk_dup(ctx, 1);
        duk_dup(ctx, -4); /* value */
        duk_dup(ctx, -4); /* key */
        duk_push_this(ctx);
        duk_call_method(ctx, 3);
        duk_pop(ctx); /* result */
        duk_pop_2(ctx); /* value, key */
    }
    duk_pop(ctx); /* enum */
    return 0;
}

/* ============================================================
   Iterator
   ============================================================ */

#define ITER_KEYS  DUK_HIDDEN_SYMBOL("iter_keys")
#define ITER_IDX   DUK_HIDDEN_SYMBOL("iter_idx")
#define ITER_KIND  DUK_HIDDEN_SYMBOL("iter_kind")

/* Helper: return this (for Symbol.iterator on iterators) */
static duk_ret_t map_return_this(duk_context *ctx)
{
    duk_push_this(ctx);
    return 1;
}

static duk_ret_t map_iter_next(duk_context *ctx)
{
    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, ITER_IDX);
    duk_uarridx_t idx = (duk_uarridx_t)duk_get_uint(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, ITER_KIND);
    int kind = duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, ITER_KEYS);
    duk_size_t len = duk_get_length(ctx, -1);
    duk_idx_t keys_idx = duk_normalize_index(ctx, -1);

    duk_push_object(ctx); /* result */

    if (idx >= (duk_uarridx_t)len) {
        duk_push_true(ctx);
        duk_put_prop_string(ctx, -2, "done");
        duk_push_undefined(ctx);
        duk_put_prop_string(ctx, -2, "value");
    } else {
        duk_push_false(ctx);
        duk_put_prop_string(ctx, -2, "done");

        /* Get the keystr, then look up in store */
        duk_get_prop_index(ctx, keys_idx, idx);
        const char *ks = duk_get_string(ctx, -1);
        duk_pop(ctx); /* keystr */

        duk_push_this(ctx);
        duk_get_prop_string(ctx, -1, MAP_STORE);
        duk_get_prop_string(ctx, -1, ks);
        /* stack: [..., result, this, store, entry] */

        if (kind == 0) {
            /* entries: [key, value] */
            duk_push_array(ctx);
            duk_get_prop_string(ctx, -2, "k");
            duk_put_prop_index(ctx, -2, 0);
            duk_get_prop_string(ctx, -2, "v");
            duk_put_prop_index(ctx, -2, 1);
        } else if (kind == 1) {
            /* keys */
            duk_get_prop_string(ctx, -1, "k");
        } else {
            /* values */
            duk_get_prop_string(ctx, -1, "v");
        }
        /* stack: [..., result, this, store, entry, val_or_pair] */
        duk_put_prop_string(ctx, -5, "value"); /* result.value = ... */
        duk_pop_3(ctx); /* entry, store, this */

        /* Advance index */
        duk_push_this(ctx);
        duk_push_uint(ctx, idx + 1);
        duk_put_prop_string(ctx, -2, ITER_IDX);
        duk_pop(ctx);
    }

    duk_remove(ctx, -2); /* remove keys array */
    duk_remove(ctx, -2); /* remove this (from initial push) */
    return 1;
}

static void map_push_iterator(duk_context *ctx, int kind)
{
    duk_push_object(ctx);

    /* Copy store reference */
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, MAP_STORE);
    duk_put_prop_string(ctx, -3, MAP_STORE);

    /* Snapshot the key strings into an array (so iteration is
       stable even if the map is mutated during iteration) */
    duk_get_prop_string(ctx, -1, MAP_STORE);
    duk_push_array(ctx);
    duk_uarridx_t i = 0;
    duk_enum(ctx, -2, DUK_ENUM_OWN_PROPERTIES_ONLY);
    while (duk_next(ctx, -1, 0)) {
        duk_put_prop_index(ctx, -3, i++);
    }
    duk_pop(ctx); /* enum */
    duk_remove(ctx, -2); /* remove store dup */
    duk_put_prop_string(ctx, -3, ITER_KEYS);
    duk_pop(ctx); /* this */

    /* Position and kind */
    duk_push_uint(ctx, 0);
    duk_put_prop_string(ctx, -2, ITER_IDX);
    duk_push_int(ctx, kind);
    duk_put_prop_string(ctx, -2, ITER_KIND);

    /* next() method */
    duk_push_c_function(ctx, map_iter_next, 0);
    duk_put_prop_string(ctx, -2, "next");

    /* Symbol.iterator returns self */
    duk_get_global_string(ctx, "Symbol");
    if (!duk_is_undefined(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "iterator");
        if (!duk_is_undefined(ctx, -1)) {
            duk_push_c_function(ctx, map_return_this, 0);
            duk_put_prop(ctx, -4);
        } else {
            duk_pop(ctx);
        }
    }
    duk_pop(ctx); /* Symbol */
}

static duk_ret_t map_entries(duk_context *ctx)
{
    map_push_iterator(ctx, 0);
    return 1;
}

static duk_ret_t map_keys(duk_context *ctx)
{
    map_push_iterator(ctx, 1);
    return 1;
}

static duk_ret_t map_values(duk_context *ctx)
{
    map_push_iterator(ctx, 2);
    return 1;
}

/* ============================================================
   Map constructor
   ============================================================ */

static duk_ret_t map_constructor(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_THROW(ctx, "Map must be called with 'new'");

    duk_push_this(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, MAP_STORE);
    duk_pop(ctx);

    /* Populate from iterable */
    if (duk_is_array(ctx, 0)) {
        duk_size_t len = duk_get_length(ctx, 0);
        for (duk_uarridx_t i = 0; i < (duk_uarridx_t)len; i++) {
            duk_get_prop_index(ctx, 0, i);
            if (duk_is_array(ctx, -1) && duk_get_length(ctx, -1) >= 2) {
                duk_get_prop_index(ctx, -1, 0);
                duk_get_prop_index(ctx, -2, 1);
                duk_push_this(ctx);
                duk_get_prop_string(ctx, -1, "set");
                duk_dup(ctx, -2);
                duk_dup(ctx, -5);
                duk_dup(ctx, -5);
                duk_call_method(ctx, 2);
                duk_pop_n(ctx, 4);
            }
            duk_pop(ctx);
        }
    }
    return 0;
}

/* ============================================================
   Set methods
   ============================================================ */

static duk_ret_t set_add(duk_context *ctx)
{
    duk_idx_t store = map_push_store(ctx);

    map_push_key_string(ctx, 0);
    const char *ks = duk_get_string(ctx, -1);

    duk_push_object(ctx);
    duk_dup(ctx, 0);
    duk_put_prop_string(ctx, -2, "k");
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "v");
    duk_put_prop_string(ctx, store, ks);

    duk_pop_2(ctx);

    duk_push_this(ctx);
    return 1;
}

static duk_ret_t set_forEach(duk_context *ctx)
{
    REQUIRE_FUNCTION(ctx, 0, "Set.forEach: callback must be a function");
    duk_idx_t store = map_push_store(ctx);

    duk_enum(ctx, store, DUK_ENUM_OWN_PROPERTIES_ONLY);
    while (duk_next(ctx, -1, 1)) {
        /* stack: [..., store, enum, keystr, entry] */
        duk_get_prop_string(ctx, -1, "k");
        duk_remove(ctx, -2); /* remove entry */
        duk_remove(ctx, -2); /* remove keystr */
        /* stack: [..., store, enum, value] */

        /* callback.call(thisArg, value, value, set) */
        duk_dup(ctx, 0); /* callback */
        if (duk_is_undefined(ctx, 1))
            duk_push_undefined(ctx);
        else
            duk_dup(ctx, 1);
        duk_dup(ctx, -3); /* value */
        duk_dup(ctx, -4); /* value again */
        duk_push_this(ctx);
        duk_call_method(ctx, 3);
        duk_pop(ctx); /* result */
        duk_pop(ctx); /* value */
    }
    duk_pop(ctx); /* enum */
    return 0;
}

static duk_ret_t set_constructor(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_THROW(ctx, "Set must be called with 'new'");

    duk_push_this(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, MAP_STORE);
    duk_pop(ctx);

    if (duk_is_array(ctx, 0)) {
        duk_size_t len = duk_get_length(ctx, 0);
        for (duk_uarridx_t i = 0; i < (duk_uarridx_t)len; i++) {
            duk_get_prop_index(ctx, 0, i);
            duk_push_this(ctx);
            duk_get_prop_string(ctx, -1, "add");
            duk_dup(ctx, -2);
            duk_dup(ctx, -4);
            duk_call_method(ctx, 1);
            duk_pop_n(ctx, 3);
        }
    }
    return 0;
}

/* ============================================================
   Registration
   ============================================================ */

void duk_map_set_init(duk_context *ctx)
{
    /* ---- Map ---- */
    duk_push_c_function(ctx, map_constructor, 1);
    duk_push_object(ctx); /* prototype */

    duk_push_c_function(ctx, map_set, 2);
    duk_put_prop_string(ctx, -2, "set");
    duk_push_c_function(ctx, map_get, 1);
    duk_put_prop_string(ctx, -2, "get");
    duk_push_c_function(ctx, map_has, 1);
    duk_put_prop_string(ctx, -2, "has");
    duk_push_c_function(ctx, map_delete, 1);
    duk_put_prop_string(ctx, -2, "delete");
    duk_push_c_function(ctx, map_clear, 0);
    duk_put_prop_string(ctx, -2, "clear");
    duk_push_c_function(ctx, map_forEach, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "forEach");
    duk_push_c_function(ctx, map_entries, 0);
    duk_put_prop_string(ctx, -2, "entries");
    duk_push_c_function(ctx, map_keys, 0);
    duk_put_prop_string(ctx, -2, "keys");
    duk_push_c_function(ctx, map_values, 0);
    duk_put_prop_string(ctx, -2, "values");

    /* size getter */
    duk_push_string(ctx, "size");
    duk_push_c_function(ctx, map_size_getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER |
                           DUK_DEFPROP_SET_ENUMERABLE |
                           DUK_DEFPROP_SET_CONFIGURABLE);

    /* Symbol.iterator = entries */
    duk_get_global_string(ctx, "Symbol");
    if (!duk_is_undefined(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "iterator");
        if (!duk_is_undefined(ctx, -1)) {
            duk_push_c_function(ctx, map_entries, 0);
            duk_put_prop(ctx, -4);
        } else {
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);

    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_global_string(ctx, "Map");

    /* ---- Set ---- */
    duk_push_c_function(ctx, set_constructor, 1);
    duk_push_object(ctx); /* prototype */

    duk_push_c_function(ctx, set_add, 1);
    duk_put_prop_string(ctx, -2, "add");
    duk_push_c_function(ctx, map_has, 1);
    duk_put_prop_string(ctx, -2, "has");
    duk_push_c_function(ctx, map_delete, 1);
    duk_put_prop_string(ctx, -2, "delete");
    duk_push_c_function(ctx, map_clear, 0);
    duk_put_prop_string(ctx, -2, "clear");
    duk_push_c_function(ctx, set_forEach, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "forEach");
    /* Set stores values in .k field, so use map_keys (kind=1) for values */
    duk_push_c_function(ctx, map_keys, 0);
    duk_put_prop_string(ctx, -2, "values");
    duk_push_c_function(ctx, map_keys, 0);
    duk_put_prop_string(ctx, -2, "keys");
    duk_push_c_function(ctx, map_entries, 0);
    duk_put_prop_string(ctx, -2, "entries");

    /* size getter */
    duk_push_string(ctx, "size");
    duk_push_c_function(ctx, map_size_getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER |
                           DUK_DEFPROP_SET_ENUMERABLE |
                           DUK_DEFPROP_SET_CONFIGURABLE);

    /* Symbol.iterator = values for Set */
    duk_get_global_string(ctx, "Symbol");
    if (!duk_is_undefined(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "iterator");
        if (!duk_is_undefined(ctx, -1)) {
            duk_push_c_function(ctx, map_keys, 0);
            duk_put_prop(ctx, -4);
        } else {
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);

    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_global_string(ctx, "Set");
}
