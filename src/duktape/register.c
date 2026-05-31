/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "register.h"
#include "core/module.h"
#include "globals/printf.h"
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

/* String.prototype.normalize lazy-load stub.
 *
 * The real Unicode normalization implementation lives in
 * rampart-intl.so because it needs ICU's normalization tables.  To
 * avoid forcing every script that calls .normalize() to first
 * require('rampart-intl') by hand, we install a small JS stub at
 * startup.  The stub on first call does the require itself (which
 * triggers the dlopen + Intl install path), and that install path
 * replaces this stub on String.prototype with the real ICU-backed
 * method.  The stub then re-dispatches so the current call returns
 * the right value.
 *
 * Net effect: code that does `'café'.normalize('NFC')` Just Works,
 * rampart-intl stays unloaded until first use, and once loaded
 * subsequent .normalize() calls have zero stub overhead. */
static const char duk_rp_string_normalize_stub_src[] =
    "(function normalize(form) {"
    "  require('rampart-intl');"
    "  /* rampart-intl's installer replaced String.prototype.normalize"
    "   * with the real one.  If for some reason it didn't, prevent"
    "   * an infinite stub->stub loop. */"
    "  if (String.prototype.normalize === normalize) {"
    "    throw new TypeError("
    "      'String.prototype.normalize requires rampart-intl to be available');"
    "  }"
    "  return String.prototype.normalize.call(this, form);"
    "})";

static void install_string_normalize_stub(duk_context *ctx)
{
    duk_get_global_string(ctx, "String");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_eval_string(ctx, duk_rp_string_normalize_stub_src);
    duk_put_prop_string(ctx, -2, "normalize");
    duk_rp_set_enum_false(ctx, -1, "normalize");
    duk_pop_2(ctx); /* prototype + String */
}

/* TextEncoder/TextDecoder WHATWG-spec fixes, Node-style console additions,
 * and the eager Promise install were all ported into the duktape fork and
 * auto-install at heap-create under DUK_RP_USE_*.  Their former
 * globals/rampart-{textencoding,console,promise}.c sources (and headers)
 * are retired (.bak); no init call or include remains here.
 *
 * rampart-blob likewise moved into the rampart-whatwg.so build; Blob/File
 * become available on first access via the lazy getters installed in
 * duk_init_context. */

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
            const char *emsg = res.errmsg ? res.errmsg : "parse error in eval";
            char *emcopy = emsg ? strdup(emsg) : NULL;
            freeParseRes(&res);
            /* NDE.32: throw a proper SyntaxError so `instanceof
               SyntaxError` and `e.name === 'SyntaxError'` work the
               same under `-t` as without it.  RP_SYNTAX_THROW is
               unrolled here so emcopy can be freed between
               duk_push_error_object (which copies the message into
               the duktape heap) and duk_throw (which longjmps). */
            duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, "%s",
                                  emcopy ? emcopy : "parse error in eval");
            free(emcopy);
            (void) duk_throw(ctx);
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
               doesn't undo anything.

               NDE.44: multi-pass transpilation can stack MULTIPLE
               consecutive preambles (when later passes detect
               additional polyfills, apply_edits prepends a fresh one
               ahead of the existing). Walk past all of them — not
               just the first — so the no-op trailing preambles don't
               leak into the function-expression slot. Matches the
               same loop in transpile_code (transpiler.c:15736). */
            const char *out = res.transpiled;
            const char *prefix = "if(!global._TrN_Sp)";
            const char *marker = ";_TrN_Sp.load();";
            size_t prefix_sz = strlen(prefix);
            size_t marker_sz = strlen(marker);
            const char *split = NULL;
            for (;;)
            {
                if (strncmp(out, prefix, prefix_sz) != 0) break;
                const char *p = strstr(out, marker);
                if (!p) break;
                split = p + marker_sz;
                out = split;
            }
            if (split)
            {
                duk_push_lstring(ctx, res.transpiled,
                                 (size_t)(split - res.transpiled));
                /* peval to surface errors without aborting; on success
                   pop the result; on failure pop the error. The
                   subsequent compile of the function expression will
                   throw if polys really were required. */
                (void)duk_peval(ctx);
                duk_pop(ctx);
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


/* Minimal heap init for "bare" threads (vm sandbox backing).
 * Keeps the heap free of rampart utilities, process, require, and the
 * WHATWG/Intl lazy global getters; installs only what the worker needs
 * to drive itself (the rampart.thread message-passing surface) plus the
 * baseline language fixes that the rest of rampart code assumes. */
void duk_init_context_bare(duk_context *ctx)
{
    duk_push_global_object(ctx);
    duk_put_global_string(ctx, "global");
    fix_json_parse(ctx);
    fix_eval(ctx);
    new_function_transpile(ctx);
    install_transpile_err_trim(ctx);
    install_string_normalize_stub(ctx);  /* triggers rampart-intl load on first .normalize() call */
    duk_event_init(ctx);
    duk_thread_init(ctx);
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


    /* Many former init calls moved into the duktape fork and now
     * auto-install at heap-create when DUK_RP_USE_* (DUK_RP_ALL) is
     * defined: the Array/String/Object builtin polyfills (find, includes,
     * flat, from/of, trimStart/End, replaceAll, hasOwn, fromEntries, the
     * Array/String iterators, Object.groupBy/matchAll, Proxy.revocable,
     * Symbol.asyncIterator, etc.), Buffer, TextEncoder/Decoder, console,
     * Promise, and Map/Set.  Both their former init wrappers
     * (add_array_funcs, add_string_funcs, add_extra_object_funcs,
     * install_modern_polyfills, install_array_iter, install_string_iter,
     * install_async_iterator_symbol, install_proxy_revocable) and the
     * dead C-handler bodies they referenced have now been deleted. */
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
    new_function_transpile(ctx);
    install_transpile_err_trim(ctx);
    install_string_normalize_stub(ctx);  /* triggers rampart-intl load on first .normalize() call */
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
        /* Blob/File now provided by the duktape fork (DUK_RP_USE_BLOB),
           auto-installed at heap-create.  Excluded from the lazy-loader
           NAMES list below so the auto-installed values aren't shadowed
           by a getter. */
        "  var NAMES = ['FileReader','URL','URLSearchParams','URLPattern',"
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
    /* duk_map_set_init: moved into duktape fork as DUK_RP_USE_MAP_SET. */
}
