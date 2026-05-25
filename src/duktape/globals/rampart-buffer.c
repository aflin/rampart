/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * rampart-buffer: node-compat extensions to duktape's built-in Buffer.
 *
 * Duktape provides a Buffer that's roughly Uint8Array + node-style
 * read*-/write*- accessors. It is missing encoding handling
 * (toString/from with hex, base64, latin1, utf16le, binary, base64url)
 * and a handful of utility methods (indexOf, swap16, etc.).
 *
 * This file fills those gaps. See header for the full list.
 */

#include "rampart.h"
#include "rampart-buffer.h"
#include "../register.h"     /* duk_rp_set_enum_false prototype */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================================================
 * encoding identification
 * ============================================================ */

typedef enum {
    ENC_UTF8 = 0,
    ENC_HEX,
    ENC_BASE64,
    ENC_BASE64URL,
    ENC_LATIN1,        /* "latin1" or "binary" */
    ENC_UTF16LE,       /* "utf16le" or "ucs-2" or "ucs2" */
    ENC_ASCII,
    ENC_UNKNOWN
} buf_encoding_t;

static buf_encoding_t parse_encoding(const char *s)
{
    if (!s || !*s) return ENC_UTF8;
    /* case-insensitive comparison */
    char low[16];
    size_t i;
    for (i = 0; s[i] && i < sizeof(low) - 1; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '-') continue;  /* tolerate 'utf-8' */
        low[i] = c;
    }
    low[i] = '\0';
    if (strcmp(low, "utf8") == 0)       return ENC_UTF8;
    if (strcmp(low, "hex") == 0)        return ENC_HEX;
    if (strcmp(low, "base64") == 0)     return ENC_BASE64;
    if (strcmp(low, "base64url") == 0)  return ENC_BASE64URL;
    if (strcmp(low, "latin1") == 0)     return ENC_LATIN1;
    if (strcmp(low, "binary") == 0)     return ENC_LATIN1;
    if (strcmp(low, "utf16le") == 0)    return ENC_UTF16LE;
    if (strcmp(low, "ucs2") == 0)       return ENC_UTF16LE;
    if (strcmp(low, "ucs2") == 0)       return ENC_UTF16LE;
    if (strcmp(low, "ascii") == 0)      return ENC_ASCII;
    return ENC_UNKNOWN;
}

/* ============================================================
 * encoders — string → bytes
 * ============================================================ */

static int hex_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex string → bytes. Returns bytes written. If odd-length or invalid char
 * encountered, stops there (matches node). out_max is buffer capacity. */
static size_t hex_decode(const char *s, size_t slen, unsigned char *out, size_t out_max)
{
    size_t i, written = 0;
    for (i = 0; i + 1 < slen && written < out_max; i += 2) {
        int hi = hex_val((unsigned char)s[i]);
        int lo = hex_val((unsigned char)s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out[written++] = (unsigned char)((hi << 4) | lo);
    }
    return written;
}

static size_t hex_decode_len(const char *s, size_t slen)
{
    size_t i, n = 0;
    for (i = 0; i + 1 < slen; i += 2) {
        if (hex_val((unsigned char)s[i]) < 0 || hex_val((unsigned char)s[i + 1]) < 0) break;
        n++;
    }
    return n;
}

/* base64 + base64url decode. b64url = true uses '-_' instead of '+/' */
static int b64_val(int c, int url)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' && !url)     return 62;
    if (c == '-' && url)      return 62;
    if (c == '/' && !url)     return 63;
    if (c == '_' && url)      return 63;
    return -1;
}

static size_t b64_decode(const char *s, size_t slen, unsigned char *out, size_t out_max, int url)
{
    size_t written = 0;
    int bits = 0, vbits = 0;
    size_t i;
    for (i = 0; i < slen && written < out_max; i++) {
        int c = (unsigned char)s[i];
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        int v = b64_val(c, url);
        if (v < 0) break;  /* stop at first invalid char, like node */
        bits = (bits << 6) | v;
        vbits += 6;
        if (vbits >= 8) {
            vbits -= 8;
            if (written < out_max)
                out[written++] = (unsigned char)((bits >> vbits) & 0xFF);
        }
    }
    return written;
}

static size_t b64_decode_len(const char *s, size_t slen, int url)
{
    size_t n = 0;
    int vbits = 0;
    size_t i;
    for (i = 0; i < slen; i++) {
        int c = (unsigned char)s[i];
        if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (b64_val(c, url) < 0) break;
        vbits += 6;
        if (vbits >= 8) { vbits -= 8; n++; }
    }
    return n;
}

/* base64 encode */
static const char b64_alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b64_alpha_url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static size_t b64_encode(const unsigned char *in, size_t inlen, char *out, int url, int pad)
{
    const char *alpha = url ? b64_alpha_url : b64_alpha;
    size_t i, o = 0;
    for (i = 0; i + 2 < inlen; i += 3) {
        out[o++] = alpha[ in[i] >> 2 ];
        out[o++] = alpha[ ((in[i] & 0x03) << 4) | (in[i+1] >> 4) ];
        out[o++] = alpha[ ((in[i+1] & 0x0F) << 2) | (in[i+2] >> 6) ];
        out[o++] = alpha[ in[i+2] & 0x3F ];
    }
    if (i < inlen) {
        out[o++] = alpha[ in[i] >> 2 ];
        if (i + 1 < inlen) {
            out[o++] = alpha[ ((in[i] & 0x03) << 4) | (in[i+1] >> 4) ];
            out[o++] = alpha[ (in[i+1] & 0x0F) << 2 ];
            if (pad) out[o++] = '=';
        } else {
            out[o++] = alpha[ (in[i] & 0x03) << 4 ];
            if (pad) { out[o++] = '='; out[o++] = '='; }
        }
    }
    return o;
}

static const char hex_alpha[] = "0123456789abcdef";

static size_t hex_encode(const unsigned char *in, size_t inlen, char *out)
{
    size_t i;
    for (i = 0; i < inlen; i++) {
        out[i*2]   = hex_alpha[ in[i] >> 4 ];
        out[i*2+1] = hex_alpha[ in[i] & 0x0F ];
    }
    return inlen * 2;
}

/* utf16le encoding: emit JS UCS-2 chars from byte pairs */
static void utf16le_decode_push(duk_context *ctx, const unsigned char *in, size_t inlen)
{
    /* Build a JS string from UCS-2 code units. duktape strings are UTF-8,
     * so we encode each 16-bit code unit as UTF-8 ourselves. */
    size_t out_max = inlen / 2 * 3 + 1;
    char *out = malloc(out_max);
    if (!out) { duk_push_string(ctx, ""); return; }
    size_t o = 0;
    size_t i;
    for (i = 0; i + 1 < inlen; i += 2) {
        unsigned int cu = (unsigned int)in[i] | ((unsigned int)in[i+1] << 8);
        if (cu < 0x80) {
            out[o++] = (char)cu;
        } else if (cu < 0x800) {
            out[o++] = (char)(0xC0 | (cu >> 6));
            out[o++] = (char)(0x80 | (cu & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (cu >> 12));
            out[o++] = (char)(0x80 | ((cu >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cu & 0x3F));
        }
    }
    duk_push_lstring(ctx, out, o);
    free(out);
}

/* utf16le encode: take a JS string (which we read as UTF-8), produce UTF-16LE bytes.
 * Returns bytes written. */
static size_t utf16le_encode(const char *in, size_t inlen, unsigned char *out, size_t out_max)
{
    size_t i = 0, o = 0;
    while (i < inlen && o + 1 < out_max) {
        unsigned int c;
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) { c = b; i++; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < inlen) {
            c = ((b & 0x1F) << 6) | (in[i+1] & 0x3F); i += 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < inlen) {
            c = ((b & 0x0F) << 12) | ((in[i+1] & 0x3F) << 6) | (in[i+2] & 0x3F); i += 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < inlen) {
            /* 4-byte UTF-8 → surrogate pair */
            unsigned int sup = ((b & 0x07) << 18) | ((in[i+1] & 0x3F) << 12) |
                               ((in[i+2] & 0x3F) << 6) | (in[i+3] & 0x3F);
            i += 4;
            unsigned int high = 0xD800 + ((sup - 0x10000) >> 10);
            unsigned int low  = 0xDC00 + ((sup - 0x10000) & 0x3FF);
            out[o++] = (unsigned char)(high & 0xFF);
            out[o++] = (unsigned char)((high >> 8) & 0xFF);
            if (o + 1 >= out_max) break;
            out[o++] = (unsigned char)(low & 0xFF);
            out[o++] = (unsigned char)((low >> 8) & 0xFF);
            continue;
        } else { i++; continue; }
        out[o++] = (unsigned char)(c & 0xFF);
        out[o++] = (unsigned char)((c >> 8) & 0xFF);
    }
    return o;
}

static size_t utf16le_encode_len(const char *in, size_t inlen)
{
    size_t i = 0, n = 0;
    while (i < inlen) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) { n += 2; i++; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < inlen) { n += 2; i += 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < inlen) { n += 2; i += 3; }
        else if ((b & 0xF8) == 0xF0 && i + 3 < inlen) { n += 4; i += 4; }
        else i++;
    }
    return n;
}

/* latin1: each byte 0..255 maps to one Unicode code point 0..255.
 * Encode: take JS string's UTF-8 bytes, for each code point < 256 keep byte. */
static size_t latin1_encode(const char *in, size_t inlen, unsigned char *out, size_t out_max)
{
    size_t i = 0, o = 0;
    while (i < inlen && o < out_max) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) { out[o++] = b; i++; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < inlen) {
            unsigned int c = ((b & 0x1F) << 6) | (in[i+1] & 0x3F);
            out[o++] = (unsigned char)(c & 0xFF);
            i += 2;
        } else {
            out[o++] = '?';  /* node truncates; we approximate */
            i++;
        }
    }
    return o;
}

static size_t latin1_encode_len(const char *in, size_t inlen)
{
    size_t i = 0, n = 0;
    while (i < inlen) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) { n++; i++; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < inlen) { n++; i += 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < inlen) { n++; i += 3; }
        else if ((b & 0xF8) == 0xF0 && i + 3 < inlen) { n++; i += 4; }
        else { n++; i++; }
    }
    return n;
}

/* latin1 decode: bytes → JS string where each byte is U+0000..U+00FF */
static void latin1_decode_push(duk_context *ctx, const unsigned char *in, size_t inlen)
{
    /* UTF-8 expansion: bytes < 0x80 are 1 byte each; bytes >= 0x80 are 2 bytes each */
    size_t out_max = inlen * 2 + 1;
    char *out = malloc(out_max);
    if (!out) { duk_push_string(ctx, ""); return; }
    size_t o = 0;
    size_t i;
    for (i = 0; i < inlen; i++) {
        unsigned char b = in[i];
        if (b < 0x80) out[o++] = (char)b;
        else {
            out[o++] = (char)(0xC0 | (b >> 6));
            out[o++] = (char)(0x80 | (b & 0x3F));
        }
    }
    duk_push_lstring(ctx, out, o);
    free(out);
}

/* ============================================================
 * Buffer.from — existing function, extended to handle encoding
 * ============================================================ */

duk_ret_t duk_rp_buffer_from(duk_context *ctx)
{
    /* Buffer.from(string, encoding) — encoding-aware path */
    if (duk_is_string(ctx, 0) && duk_is_string(ctx, 1)) {
        duk_size_t slen;
        const char *s = duk_get_lstring(ctx, 0, &slen);
        buf_encoding_t enc = parse_encoding(duk_get_string(ctx, 1));
        size_t out_len;
        unsigned char *to_buf;

        switch (enc) {
            case ENC_HEX:
                out_len = hex_decode_len(s, slen);
                duk_get_global_string(ctx, "Buffer");
                duk_push_number(ctx, (double)out_len);
                duk_new(ctx, 1);
                to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
                hex_decode(s, slen, to_buf, out_len);
                return 1;
            case ENC_BASE64:
            case ENC_BASE64URL:
                out_len = b64_decode_len(s, slen, enc == ENC_BASE64URL);
                duk_get_global_string(ctx, "Buffer");
                duk_push_number(ctx, (double)out_len);
                duk_new(ctx, 1);
                to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
                b64_decode(s, slen, to_buf, out_len, enc == ENC_BASE64URL);
                return 1;
            case ENC_LATIN1:
                out_len = latin1_encode_len(s, slen);
                duk_get_global_string(ctx, "Buffer");
                duk_push_number(ctx, (double)out_len);
                duk_new(ctx, 1);
                to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
                latin1_encode(s, slen, to_buf, out_len);
                return 1;
            case ENC_UTF16LE:
                out_len = utf16le_encode_len(s, slen);
                duk_get_global_string(ctx, "Buffer");
                duk_push_number(ctx, (double)out_len);
                duk_new(ctx, 1);
                to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
                utf16le_encode(s, slen, to_buf, out_len);
                return 1;
            case ENC_UTF8:
            case ENC_ASCII:
            case ENC_UNKNOWN:
            default:
                /* fall through to default path */
                break;
        }
    }

    /* Default path (also used for: buffer, plain string without encoding, array)
     *
     * Node API: Buffer.from(arrayBuffer[, byteOffset[, length]]) creates
     * a Buffer over the SAME memory as the given ArrayBuffer, starting
     * at byteOffset for length bytes.  Default offset=0, length=remaining.
     * Our Buffer uses a fresh fixed buffer (no shared backing), so we
     * COPY the requested slice rather than view — but we still honor
     * byteOffset and byteLength correctly per spec.
     */
    if (duk_is_buffer_data(ctx, 0) || duk_is_string(ctx, 0)) {
        duk_size_t from_sz;
        const char *from_buf = REQUIRE_STR_OR_BUF(ctx, 0, &from_sz, "");
        /* Honor optional byteOffset / byteLength args when the source
           is a buffer/ArrayBuffer (Node Buffer.from(ab, off, len) form).
           Skip for string sources — those forms aren't spec'd. */
        duk_size_t offset = 0;
        duk_size_t length = from_sz;
        if (duk_is_buffer_data(ctx, 0)) {
            if (!duk_is_undefined(ctx, 1)) {
                double od = duk_to_number(ctx, 1);
                if (od < 0 || od > (double)from_sz)
                    RP_THROW(ctx, "Buffer.from: byteOffset out of range");
                offset = (duk_size_t)od;
                length = from_sz - offset;
            }
            if (!duk_is_undefined(ctx, 2)) {
                double ld = duk_to_number(ctx, 2);
                if (ld < 0 || ld > (double)(from_sz - offset))
                    RP_THROW(ctx, "Buffer.from: length out of range");
                length = (duk_size_t)ld;
            }
        }
        duk_get_global_string(ctx, "Buffer");
        duk_push_number(ctx, (double)length);
        duk_new(ctx, 1);
        void *to_buf = duk_get_buffer_data(ctx, -1, NULL);
        memcpy(to_buf, from_buf + offset, length);
    } else if (duk_is_array(ctx, 0)) {
        duk_size_t arr_len = duk_get_length(ctx, 0);
        duk_size_t i;
        duk_get_global_string(ctx, "Buffer");
        duk_push_number(ctx, (double)arr_len);
        duk_new(ctx, 1);
        unsigned char *to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
        for (i = 0; i < arr_len; i++) {
            duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
            to_buf[i] = (unsigned char)(duk_to_int(ctx, -1) & 0xFF);
            duk_pop(ctx);
        }
    } else {
        RP_THROW(ctx, "Buffer.from: Argument must be a Buffer, String, or Array");
    }
    return 1;
}

/* ============================================================
 * Buffer.alloc — existing function (moved verbatim)
 * ============================================================ */

duk_ret_t duk_rp_buffer_alloc(duk_context *ctx)
{
    int size = REQUIRE_INT(ctx, 0, "Buffer.alloc: size must be an integer");
    if (size < 0)
        RP_THROW(ctx, "Buffer.alloc: size must be a positive number");

    duk_get_global_string(ctx, "Buffer");
    duk_dup(ctx, 0);
    duk_new(ctx, 1);

    if (duk_is_number(ctx, 1)) {
        unsigned char fill_val = (unsigned char)(duk_to_int(ctx, 1) & 0xFF);
        unsigned char *to_buf = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
        memset(to_buf, fill_val, (size_t)size);
    } else if (duk_is_buffer_data(ctx, 1) || duk_is_string(ctx, 1)) {
        duk_size_t from_sz;
        int i = 0;
        const char *from_buf = REQUIRE_STR_OR_BUF(ctx, 1, &from_sz, "");
        char *to_buf = (char *)duk_get_buffer_data(ctx, -1, NULL);
        if (from_sz > 0) {
            while (i < size) {
                to_buf[i] = from_buf[i % from_sz];
                i++;
            }
        }
    } else if (!duk_is_undefined(ctx, 1)) {
        RP_THROW(ctx, "Buffer.alloc: Second argument must be a Number, Buffer, or String");
    }
    return 1;
}

/* ============================================================
 * Buffer.byteLength(string, [encoding])
 * ============================================================ */

static duk_ret_t buf_byte_length(duk_context *ctx)
{
    if (duk_is_buffer_data(ctx, 0)) {
        duk_size_t sz;
        duk_get_buffer_data(ctx, 0, &sz);
        duk_push_number(ctx, (double)sz);
        return 1;
    }
    duk_size_t slen;
    const char *s = duk_require_lstring(ctx, 0, &slen);
    buf_encoding_t enc = ENC_UTF8;
    if (duk_is_string(ctx, 1)) enc = parse_encoding(duk_get_string(ctx, 1));

    size_t out;
    switch (enc) {
        case ENC_HEX:       out = slen / 2; break;
        case ENC_BASE64:    out = b64_decode_len(s, slen, 0); break;
        case ENC_BASE64URL: out = b64_decode_len(s, slen, 1); break;
        case ENC_LATIN1:    out = latin1_encode_len(s, slen); break;
        case ENC_ASCII:     out = latin1_encode_len(s, slen); break;
        case ENC_UTF16LE:   out = utf16le_encode_len(s, slen); break;
        case ENC_UTF8:
        case ENC_UNKNOWN:
        default:            out = slen; break;
    }
    duk_push_number(ctx, (double)out);
    return 1;
}

/* ============================================================
 * Buffer.prototype.toString([encoding], [start], [end])
 * Wraps duktape's built-in toString to add encoding handling.
 * ============================================================ */

static duk_ret_t buf_to_string(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.toString: this is not a buffer");

    buf_encoding_t enc = ENC_UTF8;
    if (duk_is_string(ctx, 0)) enc = parse_encoding(duk_get_string(ctx, 0));

    /* start/end slicing */
    duk_int_t start = 0, end = (duk_int_t)blen;
    if (duk_is_number(ctx, 1)) start = duk_get_int(ctx, 1);
    if (duk_is_number(ctx, 2)) end   = duk_get_int(ctx, 2);
    if (start < 0) start = 0;
    if (end > (duk_int_t)blen) end = (duk_int_t)blen;
    if (start > end) start = end;
    size_t len = (size_t)(end - start);
    unsigned char *p = buf + start;

    switch (enc) {
        case ENC_HEX: {
            char *out = malloc(len * 2 + 1);
            if (!out) RP_THROW(ctx, "Buffer.toString: oom");
            hex_encode(p, len, out);
            duk_push_lstring(ctx, out, len * 2);
            free(out);
            return 1;
        }
        case ENC_BASE64: {
            size_t out_max = ((len + 2) / 3) * 4 + 4;
            char *out = malloc(out_max);
            if (!out) RP_THROW(ctx, "Buffer.toString: oom");
            size_t o = b64_encode(p, len, out, 0, 1);
            duk_push_lstring(ctx, out, o);
            free(out);
            return 1;
        }
        case ENC_BASE64URL: {
            size_t out_max = ((len + 2) / 3) * 4 + 4;
            char *out = malloc(out_max);
            if (!out) RP_THROW(ctx, "Buffer.toString: oom");
            size_t o = b64_encode(p, len, out, 1, 0);  /* url=true, pad=false (node default) */
            duk_push_lstring(ctx, out, o);
            free(out);
            return 1;
        }
        case ENC_LATIN1:
            latin1_decode_push(ctx, p, len);
            return 1;
        case ENC_UTF16LE:
            utf16le_decode_push(ctx, p, len);
            return 1;
        case ENC_ASCII: {
            /* ASCII: each byte masked to 7 bits */
            char *out = malloc(len + 1);
            if (!out) RP_THROW(ctx, "Buffer.toString: oom");
            size_t i;
            for (i = 0; i < len; i++) out[i] = (char)(p[i] & 0x7F);
            duk_push_lstring(ctx, out, len);
            free(out);
            return 1;
        }
        case ENC_UTF8:
        case ENC_UNKNOWN:
        default:
            duk_push_lstring(ctx, (const char *)p, len);
            return 1;
    }
}

/* ============================================================
 * Buffer.prototype.write(string, [offset], [length], [encoding])
 * Encoding-aware. Returns bytes written.
 * ============================================================ */

static duk_ret_t buf_write(duk_context *ctx)
{
    duk_size_t slen;
    const char *s = duk_require_lstring(ctx, 0, &slen);

    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.write: this is not a buffer");
    duk_pop(ctx);

    /* node arg shapes:
     *   write(string)
     *   write(string, encoding)
     *   write(string, offset[, encoding])
     *   write(string, offset, length[, encoding])
     */
    int offset = 0;
    int length = -1;
    buf_encoding_t enc = ENC_UTF8;
    if (duk_is_string(ctx, 1)) {
        enc = parse_encoding(duk_get_string(ctx, 1));
    } else if (duk_is_number(ctx, 1)) {
        offset = duk_get_int(ctx, 1);
        if (duk_is_string(ctx, 2)) {
            enc = parse_encoding(duk_get_string(ctx, 2));
        } else if (duk_is_number(ctx, 2)) {
            length = duk_get_int(ctx, 2);
            if (duk_is_string(ctx, 3)) enc = parse_encoding(duk_get_string(ctx, 3));
        }
    }
    if (offset < 0) offset = 0;
    if (offset > (int)blen) offset = (int)blen;
    size_t avail = blen - offset;
    if (length < 0 || (size_t)length > avail) length = (int)avail;

    size_t written = 0;
    switch (enc) {
        case ENC_HEX:
            written = hex_decode(s, slen, buf + offset, (size_t)length);
            break;
        case ENC_BASE64:
            written = b64_decode(s, slen, buf + offset, (size_t)length, 0);
            break;
        case ENC_BASE64URL:
            written = b64_decode(s, slen, buf + offset, (size_t)length, 1);
            break;
        case ENC_LATIN1:
        case ENC_ASCII:
            written = latin1_encode(s, slen, buf + offset, (size_t)length);
            break;
        case ENC_UTF16LE:
            written = utf16le_encode(s, slen, buf + offset, (size_t)length);
            break;
        case ENC_UTF8:
        case ENC_UNKNOWN:
        default:
            written = slen < (size_t)length ? slen : (size_t)length;
            memcpy(buf + offset, s, written);
            break;
    }
    duk_push_number(ctx, (double)written);
    return 1;
}

/* ============================================================
 * Buffer.prototype.indexOf / lastIndexOf / includes
 * value can be number, string, or Buffer.
 * ============================================================ */

static int indexof_impl(const unsigned char *buf, size_t blen,
                         const unsigned char *needle, size_t nlen,
                         int from, int last)
{
    if (nlen == 0) return -1;
    if (last) {
        if (from < 0) from = (int)blen - 1;
        if (from > (int)blen - (int)nlen) from = (int)blen - (int)nlen;
        for (int i = from; i >= 0; i--) {
            if (memcmp(buf + i, needle, nlen) == 0) return i;
        }
        return -1;
    } else {
        if (from < 0) {
            from = (int)blen + from;
            if (from < 0) from = 0;
        }
        for (int i = from; i + (int)nlen <= (int)blen; i++) {
            if (memcmp(buf + i, needle, nlen) == 0) return i;
        }
        return -1;
    }
}

/* Coerce value (arg 0) into a needle buffer. Returns malloc'd or NULL if
 * value is a single byte (filled into out_one[0]). Caller frees malloc'd. */
static unsigned char *resolve_needle(duk_context *ctx, duk_idx_t idx, size_t *out_len,
                                      unsigned char *out_one)
{
    if (duk_is_number(ctx, idx)) {
        out_one[0] = (unsigned char)(duk_get_int(ctx, idx) & 0xFF);
        *out_len = 1;
        return NULL;
    }
    if (duk_is_buffer_data(ctx, idx)) {
        duk_size_t sz;
        unsigned char *p = (unsigned char *)duk_get_buffer_data(ctx, idx, &sz);
        *out_len = (size_t)sz;
        unsigned char *out = malloc(sz);
        if (!out) return NULL;
        memcpy(out, p, sz);
        return out;
    }
    if (duk_is_string(ctx, idx)) {
        duk_size_t sz;
        const char *p = duk_get_lstring(ctx, idx, &sz);
        buf_encoding_t enc = ENC_UTF8;
        /* In indexOf, the encoding is the 3rd arg (after value, byteOffset).
         * We don't know which arg we are — caller handles that. For now,
         * treat as UTF-8 bytes. */
        (void)enc;
        unsigned char *out = malloc(sz);
        if (!out) return NULL;
        memcpy(out, p, sz);
        *out_len = (size_t)sz;
        return out;
    }
    return NULL;
}

static duk_ret_t buf_indexof_impl(duk_context *ctx, int last)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.indexOf: this is not a buffer");
    duk_pop(ctx);

    /* arg shapes: indexOf(value[, byteOffset][, encoding])
     * If arg 1 is a string and arg 2 is undefined → arg 1 is encoding.
     * If arg 1 is a number → arg 1 is byteOffset.
     */
    int from = last ? (int)blen - 1 : 0;
    buf_encoding_t enc = ENC_UTF8;
    if (duk_is_number(ctx, 1)) from = duk_get_int(ctx, 1);
    else if (duk_is_string(ctx, 1) && duk_is_undefined(ctx, 2))
        enc = parse_encoding(duk_get_string(ctx, 1));
    if (duk_is_string(ctx, 2)) enc = parse_encoding(duk_get_string(ctx, 2));

    /* Build needle. For string value with encoding != utf8, decode it. */
    unsigned char one[1];
    size_t nlen = 0;
    unsigned char *needle_alloc = NULL;
    unsigned char *needle = NULL;

    if (duk_is_string(ctx, 0)) {
        duk_size_t slen;
        const char *s = duk_get_lstring(ctx, 0, &slen);
        if (enc == ENC_HEX) {
            nlen = hex_decode_len(s, slen);
            needle_alloc = malloc(nlen ? nlen : 1);
            if (!needle_alloc) RP_THROW(ctx, "oom");
            hex_decode(s, slen, needle_alloc, nlen);
            needle = needle_alloc;
        } else if (enc == ENC_BASE64 || enc == ENC_BASE64URL) {
            nlen = b64_decode_len(s, slen, enc == ENC_BASE64URL);
            needle_alloc = malloc(nlen ? nlen : 1);
            if (!needle_alloc) RP_THROW(ctx, "oom");
            b64_decode(s, slen, needle_alloc, nlen, enc == ENC_BASE64URL);
            needle = needle_alloc;
        } else if (enc == ENC_LATIN1) {
            nlen = latin1_encode_len(s, slen);
            needle_alloc = malloc(nlen ? nlen : 1);
            if (!needle_alloc) RP_THROW(ctx, "oom");
            latin1_encode(s, slen, needle_alloc, nlen);
            needle = needle_alloc;
        } else if (enc == ENC_UTF16LE) {
            nlen = utf16le_encode_len(s, slen);
            needle_alloc = malloc(nlen ? nlen : 1);
            if (!needle_alloc) RP_THROW(ctx, "oom");
            utf16le_encode(s, slen, needle_alloc, nlen);
            needle = needle_alloc;
        } else {
            nlen = (size_t)slen;
            needle_alloc = malloc(nlen ? nlen : 1);
            if (!needle_alloc) RP_THROW(ctx, "oom");
            memcpy(needle_alloc, s, nlen);
            needle = needle_alloc;
        }
    } else {
        needle = resolve_needle(ctx, 0, &nlen, one);
        if (!needle) needle = one;
        else needle_alloc = needle;
    }

    int idx = indexof_impl(buf, blen, needle, nlen, from, last);
    free(needle_alloc);
    duk_push_int(ctx, idx);
    return 1;
}

static duk_ret_t buf_indexof(duk_context *ctx)     { return buf_indexof_impl(ctx, 0); }
static duk_ret_t buf_lastindexof(duk_context *ctx) { return buf_indexof_impl(ctx, 1); }

static duk_ret_t buf_includes(duk_context *ctx)
{
    buf_indexof_impl(ctx, 0);
    int r = duk_get_int(ctx, -1);
    duk_pop(ctx);
    duk_push_boolean(ctx, r != -1);
    return 1;
}

/* ============================================================
 * Buffer.prototype.swap16/swap32
 * ============================================================ */

static duk_ret_t buf_swap16(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.swap16: this is not a buffer");
    if (blen % 2 != 0) RP_THROW(ctx, "Buffer.swap16: length must be a multiple of 2");
    for (size_t i = 0; i < blen; i += 2) {
        unsigned char t = buf[i]; buf[i] = buf[i+1]; buf[i+1] = t;
    }
    return 1;
}

static duk_ret_t buf_swap32(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.swap32: this is not a buffer");
    if (blen % 4 != 0) RP_THROW(ctx, "Buffer.swap32: length must be a multiple of 4");
    for (size_t i = 0; i < blen; i += 4) {
        unsigned char t;
        t = buf[i];   buf[i]   = buf[i+3]; buf[i+3] = t;
        t = buf[i+1]; buf[i+1] = buf[i+2]; buf[i+2] = t;
    }
    return 1;
}

static duk_ret_t buf_swap64(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *buf = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!buf) RP_THROW(ctx, "Buffer.swap64: this is not a buffer");
    if (blen % 8 != 0) RP_THROW(ctx, "Buffer.swap64: length must be a multiple of 8");
    for (size_t i = 0; i < blen; i += 8) {
        unsigned char t;
        t = buf[i];   buf[i]   = buf[i+7]; buf[i+7] = t;
        t = buf[i+1]; buf[i+1] = buf[i+6]; buf[i+6] = t;
        t = buf[i+2]; buf[i+2] = buf[i+5]; buf[i+5] = t;
        t = buf[i+3]; buf[i+3] = buf[i+4]; buf[i+4] = t;
    }
    return 1;
}

/* keys/values/entries — return arrays (node returns iterators; arrays
 * work for most consumers including for...of). */
/* keys()/values()/entries() are intentionally NOT installed on
   Buffer.prototype.  Buffer inherits them from %TypedArray%.prototype
   via the JS polyfill installed below by rp_install_typedarray_methods,
   which returns spec-compliant iterators (an object with .next() and
   [Symbol.iterator]).  An earlier C implementation returned plain
   Arrays — that violated the spec contract and silently broke
   `for (b of buffer)` (the inherited [Symbol.iterator] calls
   this.values() and expected a real iterator, not an Array). */

/* ============================================================
 * Buffer.prototype.subarray — returns a Buffer (not bare Uint8Array)
 * SHARING the underlying memory of `this`, matching node + spec.
 *
 * Strategy: call duktape's native Uint8Array.prototype.subarray (which
 * does create a real shared view), then reparent the result to
 * Buffer.prototype so it carries the Buffer methods.
 * ============================================================ */

static duk_ret_t buf_subarray(duk_context *ctx)
{
    int has_start = !duk_is_undefined(ctx, 0);
    int has_end   = !duk_is_undefined(ctx, 1);

    /* Look up Uint8Array.prototype.subarray */
    duk_get_global_string(ctx, "Uint8Array");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_get_prop_string(ctx, -1, "subarray");
    duk_remove(ctx, -2);    /* prototype */
    duk_remove(ctx, -2);    /* Uint8Array */
    /* stack: [arg0, arg1, native_subarray] */

    /* Bind `this` and forward args. */
    duk_push_this(ctx);
    if (has_start) duk_dup(ctx, 0);
    if (has_end)   duk_dup(ctx, 1);
    duk_call_method(ctx, (has_start ? 1 : 0) + (has_end ? 1 : 0));
    /* stack: [arg0, arg1, shared_view_uint8array] */

    /* Reparent to Buffer.prototype so the result is a Buffer instance
     * carrying all Buffer-specific methods (write, toString-with-enc,
     * indexOf, swap16/32/64, etc.). */
    duk_get_global_string(ctx, "Buffer");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_remove(ctx, -2);    /* Buffer */
    duk_set_prototype(ctx, -2);
    return 1;
}

/* ============================================================
 * Buffer.isEncoding(name)
 * ============================================================ */

static duk_ret_t buf_is_encoding(duk_context *ctx)
{
    if (!duk_is_string(ctx, 0)) { duk_push_boolean(ctx, 0); return 1; }
    buf_encoding_t enc = parse_encoding(duk_get_string(ctx, 0));
    duk_push_boolean(ctx, enc != ENC_UNKNOWN);
    return 1;
}

/* ============================================================
 * Buffer.copyBytesFrom(view, offset, length)
 *
 * Node 18+: copies bytes out of a TypedArray (or DataView) into a
 * NEW Buffer.  Unlike Buffer.from(typedArray), this respects
 * `offset`/`length` in source typed-array elements (not bytes).
 * For Uint8Array source, elements == bytes; for Int16Array etc.,
 * each element is multiplied by BYTES_PER_ELEMENT.
 * ============================================================ */

static duk_ret_t buf_copy_bytes_from(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0))
        RP_THROW(ctx, "Buffer.copyBytesFrom: first argument must be a TypedArray");

    /* Determine BYTES_PER_ELEMENT from constructor. */
    int bpe = 1;
    if (duk_get_prop_string(ctx, 0, "BYTES_PER_ELEMENT"))
        bpe = duk_to_int(ctx, -1);
    duk_pop(ctx);
    if (bpe <= 0) bpe = 1;

    /* Element-count length of source view. */
    duk_get_prop_string(ctx, 0, "length");
    int src_elements = duk_to_int(ctx, -1);
    duk_pop(ctx);

    int offset = duk_is_number(ctx, 1) ? duk_get_int(ctx, 1) : 0;
    int length = duk_is_number(ctx, 2) ? duk_get_int(ctx, 2)
                                       : (src_elements - offset);
    if (offset < 0 || offset > src_elements)
        RP_THROW(ctx, "Buffer.copyBytesFrom: offset out of range");
    if (length < 0) length = 0;
    if (offset + length > src_elements)
        length = src_elements - offset;

    /* Byte counts on the underlying buffer. */
    int byte_offset = offset * bpe;
    int byte_length = length * bpe;

    duk_size_t buf_len;
    unsigned char *src = (unsigned char *)duk_get_buffer_data(ctx, 0, &buf_len);
    if (!src) RP_THROW(ctx, "Buffer.copyBytesFrom: source is not buffer-backed");

    /* Account for the view's own byteOffset (e.g. typed array over a
     * larger ArrayBuffer).  duk_get_buffer_data on a typed array already
     * returns a pointer + length restricted to that view, so byteOffset
     * is implicit -- we add source-level offset on top. */
    if (byte_offset + byte_length > (int)buf_len)
        byte_length = (int)buf_len - byte_offset;
    if (byte_length < 0) byte_length = 0;

    duk_get_global_string(ctx, "Buffer");
    duk_push_int(ctx, byte_length);
    duk_new(ctx, 1);
    unsigned char *dst = (unsigned char *)duk_get_buffer_data(ctx, -1, NULL);
    if (byte_length > 0) memcpy(dst, src + byte_offset, (size_t)byte_length);
    return 1;
}

/* ============================================================
 * Buffer.prototype.inspect()
 *
 * Used by util.inspect / console.log to render Buffers as
 * "<Buffer 01 02 03 ...>" (truncated when long).
 * ============================================================ */

static duk_ret_t buf_inspect(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_size_t blen;
    unsigned char *b = (unsigned char *)duk_get_buffer_data(ctx, -1, &blen);
    if (!b) RP_THROW(ctx, "Buffer.inspect: this is not a buffer");

    /* node truncates at INSPECT_MAX_BYTES (default 50).  Users can
     * override by setting `Buffer.INSPECT_MAX_BYTES = N` (or via the
     * nodeshim.buffer module's identical constant). */
    duk_size_t cap = 50;
    duk_get_global_string(ctx, "Buffer");
    if (duk_get_prop_string(ctx, -1, "INSPECT_MAX_BYTES"))
    {
        int v = duk_to_int(ctx, -1);
        if (v >= 0) cap = (duk_size_t)v;
    }
    duk_pop_2(ctx);  /* prop + Buffer */

    duk_size_t show = (blen > cap) ? cap : blen;
    int truncated  = (show < blen);

    /* "<Buffer aa bb cc ... and 17 more bytes>" */
    size_t bufsize = show * 3 + 64;
    char *out = (char *)malloc(bufsize);
    if (!out) RP_THROW(ctx, "Buffer.inspect: oom");
    int pos = snprintf(out, bufsize, "<Buffer");
    for (duk_size_t i = 0; i < show; i++)
        pos += snprintf(out + pos, bufsize - pos, " %02x", b[i]);
    if (truncated)
        pos += snprintf(out + pos, bufsize - pos, " ... %d more bytes",
                        (int)(blen - show));
    snprintf(out + pos, bufsize - pos, ">");

    duk_push_string(ctx, out);
    free(out);
    return 1;
}

/* ============================================================
 * Polyfill the missing ES2017+/ES2023 methods on the shared
 * %TypedArray% prototype.
 *
 * Duktape's TypedArray prototypes ship only `subarray`, `set`,
 * `buffer`, and indexed access -- almost every modern iteration
 * helper is absent.  Anyone doing `buf.forEach(...)` or
 * `uint8.slice(...)` or `arr.map(fn)` gets a TypeError today.
 *
 * We attach these to the *shared* %TypedArray% prototype (parent of
 * Uint8Array.prototype, Int8Array.prototype, etc.), so all 9 typed
 * arrays gain them at once.  Buffer's own prototype methods
 * (Buffer.prototype.indexOf, .slice, .fill, etc.) still take
 * precedence for Buffer instances via prototype-chain shadowing,
 * preserving Buffer's richer string-aware overloads.
 *
 * The implementations are pure JS for portability; performance is
 * fine for the typical Buffer/Uint8Array sizes script code touches.
 * ============================================================ */
static const char *_typedarray_methods_js =
"function() {\n"
"  'use strict';\n"
"  var TAProto = Object.getPrototypeOf(Uint8Array.prototype);\n"
"  function def(name, fn) {\n"
"    if (TAProto[name] !== undefined) return;\n"
"    Object.defineProperty(TAProto, name, {\n"
"      value: fn, writable: true, enumerable: false, configurable: true\n"
"    });\n"
"  }\n"
"\n"
"  /* ----- iteration helpers ----- */\n"
"  def('forEach', function(cb, thisArg) {\n"
"    for (var i = 0; i < this.length; i++) cb.call(thisArg, this[i], i, this);\n"
"  });\n"
"  def('map', function(cb, thisArg) {\n"
"    var n = this.length, r = new this.constructor(n);\n"
"    for (var i = 0; i < n; i++) r[i] = cb.call(thisArg, this[i], i, this);\n"
"    return r;\n"
"  });\n"
"  def('filter', function(cb, thisArg) {\n"
"    var n = this.length, tmp = [];\n"
"    for (var i = 0; i < n; i++)\n"
"      if (cb.call(thisArg, this[i], i, this)) tmp.push(this[i]);\n"
"    var r = new this.constructor(tmp.length);\n"
"    for (var j = 0; j < tmp.length; j++) r[j] = tmp[j];\n"
"    return r;\n"
"  });\n"
"  def('reduce', function(cb /*, initial */) {\n"
"    var i = 0, n = this.length, acc;\n"
"    if (arguments.length >= 2) acc = arguments[1];\n"
"    else if (n === 0) throw new TypeError('Reduce of empty array with no initial value');\n"
"    else { acc = this[0]; i = 1; }\n"
"    for (; i < n; i++) acc = cb(acc, this[i], i, this);\n"
"    return acc;\n"
"  });\n"
"  def('reduceRight', function(cb /*, initial */) {\n"
"    var n = this.length, i = n - 1, acc;\n"
"    if (arguments.length >= 2) acc = arguments[1];\n"
"    else if (n === 0) throw new TypeError('Reduce of empty array with no initial value');\n"
"    else { acc = this[i]; i--; }\n"
"    for (; i >= 0; i--) acc = cb(acc, this[i], i, this);\n"
"    return acc;\n"
"  });\n"
"  def('some', function(cb, thisArg) {\n"
"    for (var i = 0; i < this.length; i++) if (cb.call(thisArg, this[i], i, this)) return true;\n"
"    return false;\n"
"  });\n"
"  def('every', function(cb, thisArg) {\n"
"    for (var i = 0; i < this.length; i++) if (!cb.call(thisArg, this[i], i, this)) return false;\n"
"    return true;\n"
"  });\n"
"  def('find', function(cb, thisArg) {\n"
"    for (var i = 0; i < this.length; i++) if (cb.call(thisArg, this[i], i, this)) return this[i];\n"
"    return undefined;\n"
"  });\n"
"  def('findIndex', function(cb, thisArg) {\n"
"    for (var i = 0; i < this.length; i++) if (cb.call(thisArg, this[i], i, this)) return i;\n"
"    return -1;\n"
"  });\n"
"  def('findLast', function(cb, thisArg) {\n"
"    for (var i = this.length - 1; i >= 0; i--)\n"
"      if (cb.call(thisArg, this[i], i, this)) return this[i];\n"
"    return undefined;\n"
"  });\n"
"  def('findLastIndex', function(cb, thisArg) {\n"
"    for (var i = this.length - 1; i >= 0; i--)\n"
"      if (cb.call(thisArg, this[i], i, this)) return i;\n"
"    return -1;\n"
"  });\n"
"  def('at', function(i) {\n"
"    var n = this.length;\n"
"    var idx = (i < 0) ? n + Number(i) : Number(i);\n"
"    if (idx < 0 || idx >= n) return undefined;\n"
"    return this[idx | 0];\n"
"  });\n"
"\n"
"  /* ----- manipulation ----- */\n"
"  /* Per ES spec helper: clamp a relative index into [0, n]. */\n"
"  function _bound(idx, n, dflt) {\n"
"    if (idx === undefined) return dflt;\n"
"    idx = Number(idx);\n"
"    if (idx !== idx) return 0;       /* NaN -> 0 */\n"
"    if (idx < 0)   return Math.max(0, n + idx);\n"
"    return Math.min(n, idx);\n"
"  }\n"
"  def('slice', function(start, end) {\n"
"    var n = this.length;\n"
"    var s = _bound(start, n, 0), e = _bound(end, n, n);\n"
"    var len = Math.max(0, e - s);\n"
"    var r = new this.constructor(len);\n"
"    for (var i = 0; i < len; i++) r[i] = this[s + i];\n"
"    return r;\n"
"  });\n"
"  def('fill', function(value, start, end) {\n"
"    var n = this.length;\n"
"    var s = _bound(start, n, 0), e = _bound(end, n, n);\n"
"    for (var i = s; i < e; i++) this[i] = value;\n"
"    return this;\n"
"  });\n"
"  def('copyWithin', function(target, start, end) {\n"
"    var n = this.length;\n"
"    var t = _bound(target, n, 0), s = _bound(start, n, 0), e = _bound(end, n, n);\n"
"    var count = Math.min(e - s, n - t);\n"
"    if (count <= 0) return this;\n"
"    /* If source and dest overlap with target > start, copy backwards. */\n"
"    if (t > s && t < s + count) {\n"
"      for (var i = count - 1; i >= 0; i--) this[t + i] = this[s + i];\n"
"    } else {\n"
"      for (var j = 0; j < count; j++) this[t + j] = this[s + j];\n"
"    }\n"
"    return this;\n"
"  });\n"
"  def('reverse', function() {\n"
"    var n = this.length;\n"
"    for (var i = 0, j = n - 1; i < j; i++, j--) {\n"
"      var t = this[i]; this[i] = this[j]; this[j] = t;\n"
"    }\n"
"    return this;\n"
"  });\n"
"  def('sort', function(cmp) {\n"
"    /* TypedArray default ordering is numeric (not lexicographic). */\n"
"    if (cmp === undefined) cmp = function(a, b) { return a - b; };\n"
"    var n = this.length;\n"
"    var tmp = new Array(n);\n"
"    for (var i = 0; i < n; i++) tmp[i] = this[i];\n"
"    tmp.sort(cmp);\n"
"    for (var k = 0; k < n; k++) this[k] = tmp[k];\n"
"    return this;\n"
"  });\n"
"\n"
"  /* ----- search ----- */\n"
"  /* Standard numeric-only indexOf/lastIndexOf/includes. Buffer's\n"
"     own prototype methods (which also accept strings) shadow these\n"
"     for Buffer instances. */\n"
"  def('indexOf', function(target, fromIndex) {\n"
"    var n = this.length;\n"
"    var i = (fromIndex === undefined) ? 0\n"
"          : (fromIndex < 0 ? Math.max(0, n + fromIndex) : Number(fromIndex));\n"
"    for (; i < n; i++) if (this[i] === target) return i;\n"
"    return -1;\n"
"  });\n"
"  def('lastIndexOf', function(target, fromIndex) {\n"
"    var n = this.length;\n"
"    var i = (fromIndex === undefined) ? n - 1\n"
"          : (fromIndex < 0 ? n + Number(fromIndex)\n"
"                           : Math.min(n - 1, Number(fromIndex)));\n"
"    for (; i >= 0; i--) if (this[i] === target) return i;\n"
"    return -1;\n"
"  });\n"
"  def('includes', function(target, fromIndex) {\n"
"    return this.indexOf(target, fromIndex) !== -1;\n"
"  });\n"
"\n"
"  /* ----- string ----- */\n"
"  def('join', function(sep) {\n"
"    sep = (sep === undefined) ? ',' : String(sep);\n"
"    var n = this.length;\n"
"    if (n === 0) return '';\n"
"    var s = String(this[0]);\n"
"    for (var i = 1; i < n; i++) s += sep + String(this[i]);\n"
"    return s;\n"
"  });\n"
"\n"
"  /* ----- iterators ----- */\n"
"  function _makeIter(self, valueFn) {\n"
"    var i = 0;\n"
"    var iter = {\n"
"      next: function() {\n"
"        if (i < self.length) {\n"
"          var v = valueFn(i, self[i]);\n"
"          i++;\n"
"          return {value: v, done: false};\n"
"        }\n"
"        return {value: undefined, done: true};\n"
"      }\n"
"    };\n"
"    if (typeof Symbol !== 'undefined' && Symbol.iterator) {\n"
"      iter[Symbol.iterator] = function() { return this; };\n"
"    }\n"
"    return iter;\n"
"  }\n"
"  def('entries', function() {\n"
"    var self = this; return _makeIter(self, function(i, v) { return [i, v]; });\n"
"  });\n"
"  def('keys', function() {\n"
"    var self = this; return _makeIter(self, function(i) { return i; });\n"
"  });\n"
"  def('values', function() {\n"
"    var self = this; return _makeIter(self, function(i, v) { return v; });\n"
"  });\n"
"\n"
"  /* `for (x of typedArr)` -- @@iterator yields values. */\n"
"  if (typeof Symbol !== 'undefined' && Symbol.iterator && !TAProto[Symbol.iterator]) {\n"
"    Object.defineProperty(TAProto, Symbol.iterator, {\n"
"      value: function() { return this.values(); },\n"
"      writable: true, enumerable: false, configurable: true\n"
"    });\n"
"  }\n"
"}";

static void rp_install_typedarray_methods(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-buffer.c:_typedarray_methods_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, _typedarray_methods_js);
    duk_call(ctx, 0);
    duk_pop(ctx);  /* discard result */
}

/* ArrayBuffer.prototype.transfer/transferToFixedLength (ES2024) is
 * installed as a JS polyfill in register.c rather than natively here.
 * The polyfill copies bytes into a fresh ArrayBuffer and flips a
 * JS-level _detached flag.  True spec detach (views collapse to
 * byteLength=0, source.byteLength=0) would require a duktape engine
 * patch — duktape's ArrayBuffer caches [[ArrayBufferByteLength]] per
 * ES spec and there's no public-API primitive to clear it.  See
 * earlier git history for a probe that confirms duk_steal_buffer +
 * duk_to_dynamic_buffer don't reach the slot either. */

/* ============================================================
 * Install everything onto the global Buffer
 * ============================================================ */

void duk_rp_buffer_init(duk_context *ctx)
{
    duk_get_global_string(ctx, "Buffer");
    if (!duk_is_function(ctx, -1)) {
        duk_pop(ctx);
        return;  /* no Buffer? shouldn't happen */
    }

    /* Statics */
    duk_push_c_function(ctx, duk_rp_buffer_alloc, 3);
    duk_put_prop_string(ctx, -2, "alloc");
    duk_push_c_function(ctx, duk_rp_buffer_from, 3);
    duk_put_prop_string(ctx, -2, "from");
    /* allocUnsafe: node's name for unfilled allocation. We zero-fill
     * anyway (cheap, safer). Alias to alloc. */
    duk_push_c_function(ctx, duk_rp_buffer_alloc, 3);
    duk_put_prop_string(ctx, -2, "allocUnsafe");
    duk_push_c_function(ctx, duk_rp_buffer_alloc, 3);
    duk_put_prop_string(ctx, -2, "allocUnsafeSlow");
    duk_push_c_function(ctx, buf_byte_length, 2);
    duk_put_prop_string(ctx, -2, "byteLength");
    duk_push_c_function(ctx, buf_is_encoding, 1);
    duk_put_prop_string(ctx, -2, "isEncoding");

    duk_rp_set_enum_false(ctx, -1, "alloc");
    duk_rp_set_enum_false(ctx, -1, "from");
    duk_rp_set_enum_false(ctx, -1, "allocUnsafe");
    duk_rp_set_enum_false(ctx, -1, "allocUnsafeSlow");
    duk_rp_set_enum_false(ctx, -1, "byteLength");
    duk_rp_set_enum_false(ctx, -1, "isEncoding");

    /* Prototype methods */
    duk_get_prop_string(ctx, -1, "prototype");

    duk_push_c_function(ctx, buf_to_string, 3);
    duk_put_prop_string(ctx, -2, "toString");
    duk_push_c_function(ctx, buf_write, 4);
    duk_put_prop_string(ctx, -2, "write");
    duk_push_c_function(ctx, buf_indexof, 3);
    duk_put_prop_string(ctx, -2, "indexOf");
    duk_push_c_function(ctx, buf_lastindexof, 3);
    duk_put_prop_string(ctx, -2, "lastIndexOf");
    duk_push_c_function(ctx, buf_includes, 3);
    duk_put_prop_string(ctx, -2, "includes");
    duk_push_c_function(ctx, buf_swap16, 0);
    duk_put_prop_string(ctx, -2, "swap16");
    duk_push_c_function(ctx, buf_swap32, 0);
    duk_put_prop_string(ctx, -2, "swap32");
    duk_push_c_function(ctx, buf_swap64, 0);
    duk_put_prop_string(ctx, -2, "swap64");
    duk_push_c_function(ctx, buf_subarray, 2);
    duk_put_prop_string(ctx, -2, "subarray");
    /* keys/values/entries: inherited from %TypedArray%.prototype (iterator-returning). */
    duk_push_c_function(ctx, buf_inspect, 0);
    duk_put_prop_string(ctx, -2, "inspect");

    duk_rp_set_enum_false(ctx, -1, "toString");
    duk_rp_set_enum_false(ctx, -1, "write");
    duk_rp_set_enum_false(ctx, -1, "indexOf");
    duk_rp_set_enum_false(ctx, -1, "lastIndexOf");
    duk_rp_set_enum_false(ctx, -1, "includes");
    duk_rp_set_enum_false(ctx, -1, "swap16");
    duk_rp_set_enum_false(ctx, -1, "swap32");
    duk_rp_set_enum_false(ctx, -1, "swap64");
    duk_rp_set_enum_false(ctx, -1, "subarray");
    duk_rp_set_enum_false(ctx, -1, "inspect");

    duk_pop(ctx);  /* prototype */

    /* Buffer.copyBytesFrom(view[, offset[, length]]) — node 18+ */
    duk_push_c_function(ctx, buf_copy_bytes_from, 3);
    duk_put_prop_string(ctx, -2, "copyBytesFrom");

    /* Buffer.poolSize — node has 8192 default; rampart doesn't pool but
     * tests check for the property. */
    duk_push_int(ctx, 8192);
    duk_put_prop_string(ctx, -2, "poolSize");

    duk_pop(ctx);  /* Buffer */

    /* Polyfill missing TypedArray.prototype.* methods (forEach, map,
     * slice, fill, sort, iterators, etc.) on the shared %TypedArray%
     * prototype.  All typed arrays + Buffer pick these up. */
    rp_install_typedarray_methods(ctx);
}
