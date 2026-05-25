/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 *
 * rampart-blob: W3C File API Blob + File, native C implementation.
 *
 * Backing storage: a fixed duktape buffer stored as the hidden
 * property `\xff_buf` on each instance.  Methods access it via
 * duk_get_buffer_data() with no JS round-trip for byte access.
 * Size and type are stored as regular (read-only) instance
 * properties; for File, name and lastModified likewise.
 */

#include "rampart.h"
#include "rampart-blob.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define BLOB_HIDDEN_BUF DUK_HIDDEN_SYMBOL("_buf")

/* ---------------------------------------------------------------- */
/* Helpers                                                          */
/* ---------------------------------------------------------------- */

/* Read backing bytes from an instance at `idx`.  Returns NULL with
 * *out_len = 0 if the hidden buffer is missing (which shouldn't
 * happen for properly-constructed instances). */
static unsigned char *_blob_data(duk_context *ctx, duk_idx_t idx, duk_size_t *out_len)
{
    unsigned char *p = NULL;
    *out_len = 0;
    if (duk_get_prop_string(ctx, idx, BLOB_HIDDEN_BUF))
        p = (unsigned char *)duk_get_buffer_data(ctx, -1, out_len);
    duk_pop(ctx);
    return p;
}

/* Push the bytes of arg-stack index `i` onto a growing byte vector.
 * Handles: string (UTF-8 encoded), ArrayBuffer, TypedArray, DataView,
 * Buffer (via duk_get_buffer_data — Uint8Array subclass), Blob
 * (recursive — pulls from hidden buffer), and falls back to String(v)
 * for anything else (per W3C File API: "the type of each part
 * argument is converted via ToString").
 *
 * Returns the number of bytes appended, or throws on hard failure. */
static duk_size_t _blob_collect_part(duk_context *ctx, duk_idx_t i,
                                     unsigned char **outp, duk_size_t *outcap, duk_size_t outlen)
{
    duk_size_t need = 0;
    const unsigned char *src = NULL;

    if (duk_is_string(ctx, i)) {
        /* Duktape strings are CESU-8; Blob byte length must be UTF-8.
         * Walk the bytes and collapse any CESU-8 surrogate pair (6
         * bytes) into a 4-byte UTF-8 sequence. */
        duk_size_t slen;
        const unsigned char *s = (const unsigned char *)duk_to_lstring(ctx, i, &slen);
        /* Ensure capacity for worst-case (no shrink possible — copy
         * as we go). */
        if (outlen + slen > *outcap) {
            duk_size_t newcap = (*outcap) ? *outcap : 64;
            while (newcap < outlen + slen) newcap *= 2;
            unsigned char *nb = (unsigned char *)realloc(*outp, newcap);
            if (!nb) RP_THROW(ctx, "Blob: out of memory");
            *outp = nb;
            *outcap = newcap;
        }
        unsigned char *dst = *outp + outlen;
        duk_size_t written = 0;
        const unsigned char *end = s + slen;
        const unsigned char *five_before_end = (slen >= 5) ? (end - 5) : s;
        while (s < end) {
            if (s < five_before_end
                && s[0] == 0xED && (s[1] & 0xF0) == 0xA0 && (s[2] & 0xC0) == 0x80
                && s[3] == 0xED && (s[4] & 0xF0) == 0xB0 && (s[5] & 0xC0) == 0x80) {
                dst[0] = 0xF0 | ((s[1] + 1) & 0x1C) >> 2;
                dst[1] = 0x80 | ((s[1] + 1) & 0x03) << 4 | (s[2] & 0x3C) >> 2;
                dst[2] = 0x80 | (s[2] & 0x03) << 4 | (s[4] & 0x0F);
                dst[3] = s[5];
                dst += 4; s += 6; written += 4;
            } else {
                *dst++ = *s++; written++;
            }
        }
        return written;
    } else if (duk_is_buffer_data(ctx, i)) {
        /* ArrayBuffer / TypedArray / DataView / Buffer all expose
         * bytes via duk_get_buffer_data. */
        src = (const unsigned char *)duk_get_buffer_data(ctx, i, &need);
    } else if (duk_is_object(ctx, i) &&
               duk_has_prop_string(ctx, i, BLOB_HIDDEN_BUF)) {
        /* Another Blob — pull from its hidden backing buffer. */
        src = _blob_data(ctx, i, &need);
    } else {
        /* Fallback: ToString() and treat as UTF-8 bytes. */
        duk_size_t slen;
        const char *s = duk_to_lstring(ctx, i, &slen);
        src = (const unsigned char *)s;
        need = slen;
    }

    if (need == 0) return 0;

    /* Grow the destination buffer if needed (capacity-doubling). */
    if (outlen + need > *outcap) {
        duk_size_t newcap = (*outcap) ? *outcap : 64;
        while (newcap < outlen + need) newcap *= 2;
        unsigned char *nb = (unsigned char *)realloc(*outp, newcap);
        if (!nb) RP_THROW(ctx, "Blob: out of memory");
        *outp = nb;
        *outcap = newcap;
    }
    memcpy(*outp + outlen, src, need);
    return need;
}

/* Lowercase a string in place (ASCII only — Web spec requires ASCII
 * lowercasing for `type`). */
static void _ascii_lower(char *s)
{
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + ('a' - 'A'));
}

/* Validate `type` per W3C File API §3.2: must be a string of valid
 * ASCII (0x20–0x7E).  Anything else collapses to "". */
static int _valid_blob_type(const char *s, duk_size_t len)
{
    /* HTTP quoted-string charset per WHATWG MIME spec: HTAB, SP-~ (0x20-0x7E),
       obs-text (0x80-0xFF).  We don't reject NUL/CR/LF here either — the
       JS-side wrap (rampart-whatwg.c parse-a-mime-type) already drops any
       parse-failed type to "" so by the time we get the string it's
       already a valid serialized MIME or empty. */
    duk_size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x09 || (c >= 0x20 && c <= 0x7E) || c >= 0x80) continue;
        return 0;
    }
    return 1;
}

/* Install the standard Blob/File data properties on the instance
 * currently at `inst_idx`.  `bytes` is moved into a freshly-allocated
 * fixed duktape buffer and stored as the hidden backing property.
 * `bytes` is consumed (freed) on success. */
static void _blob_install_bytes(duk_context *ctx, duk_idx_t inst_idx,
                                unsigned char *bytes, duk_size_t bytelen,
                                const char *type)
{
    inst_idx = duk_normalize_index(ctx, inst_idx);

    /* Move bytes into a duktape fixed buffer (GC-managed).  Drop the
     * raw alloc afterwards. */
    void *dst = duk_push_fixed_buffer(ctx, bytelen);
    if (bytelen) memcpy(dst, bytes, bytelen);
    free(bytes);
    /* Stash as hidden property; non-enumerable by virtue of the \xff
     * prefix (duktape internal-property convention). */
    duk_put_prop_string(ctx, inst_idx, BLOB_HIDDEN_BUF);

    /* size: read-only, enumerable */
    duk_push_string(ctx, "size");
    duk_push_uint(ctx, (duk_uint_t)bytelen);
    duk_def_prop(ctx, inst_idx, DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE | 0
        | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE
        | DUK_DEFPROP_HAVE_CONFIGURABLE | 0);

    /* type: read-only, enumerable */
    duk_push_string(ctx, "type");
    duk_push_string(ctx, type ? type : "");
    duk_def_prop(ctx, inst_idx, DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE | 0
        | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE
        | DUK_DEFPROP_HAVE_CONFIGURABLE | 0);
}

/* Wrap an already-on-stack value with Promise.resolve(...) and
 * return.  Caller pushes the value, then tail-calls
 * `return _push_resolved(ctx)`. */
static duk_ret_t _push_resolved(duk_context *ctx)
{
    /* Value is at the top of the stack. */
    duk_get_global_string(ctx, "Promise");
    duk_get_prop_string(ctx, -1, "resolve");
    duk_remove(ctx, -2);          /* drop Promise */
    duk_dup(ctx, -2);             /* dup value as arg */
    duk_call(ctx, 1);             /* Promise.resolve(value) */
    duk_remove(ctx, -2);          /* drop original value */
    return 1;
}

/* ---------------------------------------------------------------- */
/* Blob constructor                                                 */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_ctor(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_THROW(ctx, "Blob: must be called with new");

    /* parts (Array, optional) at index 0; options ({type}) at 1. */
    unsigned char *bytes = NULL;
    duk_size_t cap = 0, len = 0;

    if (duk_is_array(ctx, 0)) {
        duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, 0);
        duk_uarridx_t i;
        for (i = 0; i < n; i++) {
            duk_get_prop_index(ctx, 0, i);
            len += _blob_collect_part(ctx, -1, &bytes, &cap, len);
            duk_pop(ctx);
        }
    } else if (!duk_is_null_or_undefined(ctx, 0)) {
        /* Spec: TypeError if parts is not iterable.  We accept a
         * bare value as a one-element list for ergonomics — matches
         * what most real implementations tolerate. */
        len += _blob_collect_part(ctx, 0, &bytes, &cap, len);
    }

    /* Parse options.type (lowercased, validated). */
    char typebuf[4096];
    typebuf[0] = '\0';
    if (duk_is_object(ctx, 1)) {
        if (duk_get_prop_string(ctx, 1, "type")) {
            duk_size_t tlen;
            const char *t = duk_to_lstring(ctx, -1, &tlen);
            /* The JS-side wrap in rampart-whatwg.c's install JS runs the
               type string through the WHATWG parse-a-mime-type algorithm
               before we get here, so the value is already normalized
               (lowercased type/subtype + param names, param values
               preserved as-is, invalid types collapsed to '').  Just
               store it; no additional lowercasing — that would wrongly
               flatten param values like 'charset=UTF-8'. */
            if (_valid_blob_type(t, tlen) && tlen < sizeof(typebuf)) {
                memcpy(typebuf, t, tlen);
                typebuf[tlen] = '\0';
            }
        }
        duk_pop(ctx);
        /* Spec: endings must be "transparent" or "native" when present
           (undefined skips the dict member; missing-or-undefined defaults
           to "transparent"). */
        if (duk_get_prop_string(ctx, 1, "endings") && !duk_is_undefined(ctx, -1)) {
            const char *e = duk_to_string(ctx, -1);
            if (strcmp(e, "transparent") != 0 && strcmp(e, "native") != 0)
                RP_TYPE_THROW(ctx, "Blob: invalid endings value '%s'", e);
        }
        duk_pop(ctx);
    }

    duk_push_this(ctx);
    _blob_install_bytes(ctx, -1, bytes, len, typebuf);
    /* Return value from a constructor is the `this` object. */
    return 0;
}

/* ---------------------------------------------------------------- */
/* Blob.prototype.slice(start, end, contentType)                    */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_slice(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t total;
    unsigned char *src = _blob_data(ctx, -1, &total);
    if (!src && total) RP_THROW(ctx, "Blob.slice: backing buffer missing");

    /* Resolve start/end per spec: optional, default 0..size,
     * negative = from end, clamp. */
    duk_int_t i_total = (duk_int_t)total;
    duk_int_t start = duk_get_int_default(ctx, 0, 0);
    duk_int_t end   = duk_is_undefined(ctx, 1) ? i_total : duk_get_int(ctx, 1);
    if (start < 0) start += i_total;
    if (end   < 0) end   += i_total;
    if (start < 0) start = 0;
    if (end   < 0) end   = 0;
    if (start > i_total) start = i_total;
    if (end   > i_total) end   = i_total;
    if (end < start) end = start;

    duk_size_t slice_len = (duk_size_t)(end - start);

    /* contentType: defaults to "" (NOT the source blob's type — per spec). */
    char typebuf[4096];
    typebuf[0] = '\0';
    if (!duk_is_undefined(ctx, 2)) {
        duk_size_t tlen;
        const char *t = duk_to_lstring(ctx, 2, &tlen);
        if (_valid_blob_type(t, tlen) && tlen < sizeof(typebuf)) {
            memcpy(typebuf, t, tlen);
            typebuf[tlen] = '\0';
            _ascii_lower(typebuf);
        }
    }

    /* Create a fresh Blob instance without going through the public
     * constructor (which would set size/type non-configurable on the
     * empty placeholder, blocking our redefinition).  Instantiate a
     * bare object with Blob.prototype, then install bytes. */
    duk_push_object(ctx);
    duk_get_global_string(ctx, "Blob");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_remove(ctx, -2);          /* drop Blob ctor */
    duk_set_prototype(ctx, -2);

    unsigned char *copy = NULL;
    if (slice_len) {
        copy = (unsigned char *)malloc(slice_len);
        if (!copy) RP_THROW(ctx, "Blob.slice: out of memory");
        memcpy(copy, src + start, slice_len);
    }
    _blob_install_bytes(ctx, -1, copy, slice_len, typebuf);
    return 1;
}

/* ---------------------------------------------------------------- */
/* Blob.prototype.text() → Promise<string>                          */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_text(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t total;
    unsigned char *src = _blob_data(ctx, -1, &total);
    /* Push the decoded string and tail-call _push_resolved.  Duktape
     * accepts arbitrary bytes via duk_push_lstring; downstream
     * consumers see UTF-8-shaped output for the common case. */
    duk_push_lstring(ctx, (const char *)src, total);
    return _push_resolved(ctx);
}

/* ---------------------------------------------------------------- */
/* Blob.prototype.arrayBuffer() → Promise<ArrayBuffer>              */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_array_buffer(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t total;
    unsigned char *src = _blob_data(ctx, -1, &total);
    /* Allocate a fresh fixed buffer and wrap as ArrayBuffer. */
    void *dst = duk_push_fixed_buffer(ctx, total);
    if (total) memcpy(dst, src, total);
    duk_push_buffer_object(ctx, -1, 0, total, DUK_BUFOBJ_ARRAYBUFFER);
    duk_remove(ctx, -2);          /* drop raw buffer */
    return _push_resolved(ctx);
}

/* ---------------------------------------------------------------- */
/* Blob.prototype.bytes() → Promise<Uint8Array>                     */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_bytes(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t total;
    unsigned char *src = _blob_data(ctx, -1, &total);
    void *dst = duk_push_fixed_buffer(ctx, total);
    if (total) memcpy(dst, src, total);
    duk_push_buffer_object(ctx, -1, 0, total, DUK_BUFOBJ_UINT8ARRAY);
    duk_remove(ctx, -2);
    return _push_resolved(ctx);
}

/* ---------------------------------------------------------------- */
/* Blob.prototype._syncBytes() → Uint8Array (non-spec, internal)    */
/* Returns the Blob's bytes synchronously as a fresh Uint8Array     */
/* (NOT a Promise).  Used by rampart-whatwg's _bodyToBytes so the   */
/* Request/Response constructors can accept Blob bodies without a   */
/* round-trip through the async arrayBuffer() Promise.              */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_sync_bytes(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t total;
    unsigned char *src = _blob_data(ctx, -1, &total);
    void *dst = duk_push_fixed_buffer(ctx, total);
    if (total) memcpy(dst, src, total);
    duk_push_buffer_object(ctx, -1, 0, total, DUK_BUFOBJ_UINT8ARRAY);
    duk_remove(ctx, -2);
    return 1;
}

/* ---------------------------------------------------------------- */
/* Blob.prototype.stream() — throws until stream/web lands          */
/* ---------------------------------------------------------------- */

static duk_ret_t blob_stream(duk_context *ctx)
{
    /* Construct a real Error with .name = 'NotSupportedError' to
     * match the DOMException-flavored pattern other node-compat
     * pieces use. */
    duk_push_error_object(ctx, DUK_ERR_ERROR,
        "Blob.stream() requires the stream module (Tier 4 — not yet implemented)");
    duk_push_string(ctx, "NotSupportedError");
    duk_put_prop_string(ctx, -2, "name");
    return duk_throw(ctx);  /* unreachable — duk_throw doesn't return */
}

/* ---------------------------------------------------------------- */
/* File constructor — Blob + name + lastModified                    */
/* ---------------------------------------------------------------- */

static duk_ret_t file_ctor(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "File: must be called with new");
    /* args: parts (required), name (required), options ({type, lastModified, endings}) */
    if (duk_get_top(ctx) < 1 || duk_is_undefined(ctx, 0))
        RP_TYPE_THROW(ctx, "File: bits argument required");
    if (duk_is_undefined(ctx, 1))
        RP_TYPE_THROW(ctx, "File: name argument required");

    /* Run Blob's logic on (parts, options) for bytes + type.  Stash
     * name + lastModified, then call _blob_install_bytes manually. */
    unsigned char *bytes = NULL;
    duk_size_t cap = 0, len = 0;

    if (duk_is_array(ctx, 0)) {
        duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, 0);
        duk_uarridx_t i;
        for (i = 0; i < n; i++) {
            duk_get_prop_index(ctx, 0, i);
            len += _blob_collect_part(ctx, -1, &bytes, &cap, len);
            duk_pop(ctx);
        }
    } else if (!duk_is_null_or_undefined(ctx, 0)) {
        len += _blob_collect_part(ctx, 0, &bytes, &cap, len);
    }

    duk_size_t namelen;
    const char *name = duk_to_lstring(ctx, 1, &namelen);
    /* Buffer the name into our own storage because options-handling
     * below may push/pop and we want a stable string. */
    char *namecopy = (char *)malloc(namelen + 1);
    if (!namecopy) { free(bytes); RP_THROW(ctx, "File: out of memory"); }
    memcpy(namecopy, name, namelen);
    namecopy[namelen] = '\0';

    char typebuf[4096];
    typebuf[0] = '\0';
    duk_double_t last_modified = 0.0;
    int have_lm = 0;
    if (duk_is_object(ctx, 2)) {
        if (duk_get_prop_string(ctx, 2, "type")) {
            duk_size_t tlen;
            const char *t = duk_to_lstring(ctx, -1, &tlen);
            /* The JS-side wrap in rampart-whatwg.c's install JS runs the
               type string through the WHATWG parse-a-mime-type algorithm
               before we get here, so the value is already normalized
               (lowercased type/subtype + param names, param values
               preserved as-is, invalid types collapsed to '').  Just
               store it; no additional lowercasing — that would wrongly
               flatten param values like 'charset=UTF-8'. */
            if (_valid_blob_type(t, tlen) && tlen < sizeof(typebuf)) {
                memcpy(typebuf, t, tlen);
                typebuf[tlen] = '\0';
            }
        }
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "lastModified")) {
            last_modified = duk_to_number(ctx, -1);
            have_lm = 1;
        }
        duk_pop(ctx);
        /* Spec: endings must be "transparent" or "native" when present
           (undefined skips the dict member). */
        if (duk_get_prop_string(ctx, 2, "endings") && !duk_is_undefined(ctx, -1)) {
            const char *e = duk_to_string(ctx, -1);
            if (strcmp(e, "transparent") != 0 && strcmp(e, "native") != 0) {
                free(namecopy); free(bytes);
                RP_TYPE_THROW(ctx, "File: invalid endings value '%s'", e);
            }
        }
        duk_pop(ctx);
    }
    if (!have_lm) {
        /* Default: now (ms since epoch).  Fetch via Date.now() so it
         * matches whatever clock other code is using. */
        duk_get_global_string(ctx, "Date");
        duk_get_prop_string(ctx, -1, "now");
        duk_call(ctx, 0);
        last_modified = duk_to_number(ctx, -1);
        duk_pop_2(ctx);
    }

    duk_push_this(ctx);
    duk_idx_t self = duk_normalize_index(ctx, -1);

    _blob_install_bytes(ctx, self, bytes, len, typebuf);

    /* name: read-only, enumerable */
    duk_push_string(ctx, "name");
    duk_push_string(ctx, namecopy);
    duk_def_prop(ctx, self, DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE | 0
        | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE
        | DUK_DEFPROP_HAVE_CONFIGURABLE | 0);
    free(namecopy);

    /* lastModified: read-only, enumerable */
    duk_push_string(ctx, "lastModified");
    duk_push_number(ctx, last_modified);
    duk_def_prop(ctx, self, DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE | 0
        | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE
        | DUK_DEFPROP_HAVE_CONFIGURABLE | 0);

    return 0;
}

/* ---------------------------------------------------------------- */
/* Init                                                              */
/* ---------------------------------------------------------------- */

void duk_rp_blob_init(duk_context *ctx)
{
    /* === Blob ===================================================== */
    duk_push_c_function(ctx, blob_ctor, 2);  /* (parts, options) */
    /* Prototype */
    duk_push_object(ctx);
    /* Blob.prototype.constructor = Blob */
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, "constructor");

    duk_push_c_function(ctx, blob_slice,        3); duk_put_prop_string(ctx, -2, "slice");
    duk_push_c_function(ctx, blob_text,         0); duk_put_prop_string(ctx, -2, "text");
    duk_push_c_function(ctx, blob_array_buffer, 0); duk_put_prop_string(ctx, -2, "arrayBuffer");
    duk_push_c_function(ctx, blob_bytes,        0); duk_put_prop_string(ctx, -2, "bytes");
    duk_push_c_function(ctx, blob_sync_bytes,   0); duk_put_prop_string(ctx, -2, "_syncBytes");
    duk_push_c_function(ctx, blob_stream,       0); duk_put_prop_string(ctx, -2, "stream");

    /* Symbol.toStringTag = 'Blob' so Object.prototype.toString.call(b)
     * returns "[object Blob]" per spec. */
    if (duk_get_global_string(ctx, "Symbol")) {
        if (duk_get_prop_string(ctx, -1, "toStringTag")) {
            duk_push_string(ctx, "Blob");
            duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_VALUE
                | DUK_DEFPROP_HAVE_WRITABLE | 0
                | DUK_DEFPROP_HAVE_ENUMERABLE | 0
                | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE);
        } else duk_pop(ctx);
        duk_pop(ctx);
    } else duk_pop(ctx);

    /* Attach prototype to constructor. */
    duk_put_prop_string(ctx, -2, "prototype");

    /* Install Blob as a configurable global, REPLACING any existing
     * descriptor (the lazy getter we install in register.c is a
     * getter-only accessor — duk_put_global_string would fail with
     * "setter undefined" on it).  duk_def_prop with HAVE_VALUE
     * replaces the descriptor wholesale. */
    duk_push_global_object(ctx);
    duk_push_string(ctx, "Blob");
    duk_dup(ctx, -3);                 /* dup the Blob constructor */
    duk_def_prop(ctx, -3,
          DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE   | DUK_DEFPROP_WRITABLE
        | DUK_DEFPROP_HAVE_ENUMERABLE | 0
        | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE);
    duk_pop_2(ctx);  /* drop global obj + original Blob ctor */

    /* === File extends Blob ======================================= */
    duk_push_c_function(ctx, file_ctor, 3);  /* (parts, name, options) */
    /* Prototype that inherits from Blob.prototype. */
    duk_push_object(ctx);
    /* Set [[Prototype]] = Blob.prototype */
    duk_get_global_string(ctx, "Blob");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_remove(ctx, -2);
    duk_set_prototype(ctx, -2);
    /* File.prototype.constructor = File */
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, "constructor");

    /* Symbol.toStringTag = 'File' */
    if (duk_get_global_string(ctx, "Symbol")) {
        if (duk_get_prop_string(ctx, -1, "toStringTag")) {
            duk_push_string(ctx, "File");
            duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_VALUE
                | DUK_DEFPROP_HAVE_WRITABLE | 0
                | DUK_DEFPROP_HAVE_ENUMERABLE | 0
                | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE);
        } else duk_pop(ctx);
        duk_pop(ctx);
    } else duk_pop(ctx);

    duk_put_prop_string(ctx, -2, "prototype");
    /* File: same replace-the-accessor dance as Blob above. */
    duk_push_global_object(ctx);
    duk_push_string(ctx, "File");
    duk_dup(ctx, -3);
    duk_def_prop(ctx, -3,
          DUK_DEFPROP_HAVE_VALUE
        | DUK_DEFPROP_HAVE_WRITABLE   | DUK_DEFPROP_WRITABLE
        | DUK_DEFPROP_HAVE_ENUMERABLE | 0
        | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE);
    duk_pop_2(ctx);
}
