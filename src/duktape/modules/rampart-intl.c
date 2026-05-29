/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * rampart-intl.so — Intl runtime for rampart, backed by ICU4C 76.1.
 *
 * Exposes globalThis.Intl with constructors that match the ECMA-402
 * surface node uses:
 *   - Intl.DateTimeFormat(locale, opts)
 *   - Intl.NumberFormat(locale, opts)
 *   - Intl.Collator(locale, opts)
 *   - Intl.getCanonicalLocales(locales)
 *   - Intl.supportedValuesOf(key)        (key="timeZone" only for now)
 *
 * Phase 1. Future: PluralRules, RelativeTimeFormat, ListFormat,
 *   DisplayNames, Locale, Segmenter.
 *
 * Required by libraries that depend on `Intl` existing (luxon's
 * primary need is DateTimeFormat with timeZone). The module is
 * loaded once at rampart startup (or via require("rampart-intl")
 * idempotently) and installs the Intl global if not already present.
 */

#include "rampart.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <unicode/utypes.h>
#include <unicode/ustring.h>
#include <unicode/uloc.h>
#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/udateintervalformat.h>
#include <unicode/ucal.h>
#include <unicode/unum.h>
#include <unicode/unumberformatter.h>
#include <unicode/unumberrangeformatter.h>
#include <unicode/ucol.h>
#include <unicode/ufieldpositer.h>
#include <unicode/unorm2.h>
#include <unicode/uenum.h>
#include <unicode/upluralrules.h>
#include <unicode/ureldatefmt.h>
#include <unicode/ulistformatter.h>
#include <unicode/uldnames.h>
#include <unicode/ubrk.h>
#include <unicode/ucurr.h>
#include <unicode/uscript.h>
#include <unicode/unumsys.h>
#include <unicode/uclean.h>

/* ECMA-402 mandates specific error subtypes (TypeError for receiver
   / brand-check violations, RangeError for out-of-range options).
   RP_THROW emits a generic Error — provide subtype variants. */
#define RP_TYPE_THROW(ctx, ...) do { \
    duk_push_error_object(ctx, DUK_ERR_TYPE_ERROR, __VA_ARGS__); \
    (void)duk_throw(ctx); \
} while (0)
#define RP_RANGE_THROW(ctx, ...) do { \
    duk_push_error_object(ctx, DUK_ERR_RANGE_ERROR, __VA_ARGS__); \
    (void)duk_throw(ctx); \
} while (0)

/* CLDR alias maps ICU doesn't apply.  Each entry rewrites the BCP 47
   tag's relevant subtag to its canonical replacement. */
static const struct { const char *from; const char *to; } LANG_ALIAS[] = {
    /* CLDR languageAlias entries that ICU's uloc_toLanguageTag misses. */
    {"cmn",         "zh"},
    {"sh",          "sr-Latn"},
    {"prs",         "fa-AF"},
    {"swc",         "sw-CD"},
    {"art-lojban",  "jbo"},
    {"cel-gaulish", "xtg"},
    {"i-ami",       "ami"},
    {"i-bnn",       "bnn"},
    {"i-default",   "en-x-i-default"},
    {"i-hak",       "hak"},
    {"i-klingon",   "tlh"},
    {"i-lux",       "lb"},
    {"i-navajo",    "nv"},
    {"i-pwn",       "pwn"},
    {"i-tao",       "tao"},
    {"i-tay",       "tay"},
    {"i-tsu",       "tsu"},
    {"i-enochian",  "und-x-i-enochian"},
    {"i-mingo",     "see-x-i-mingo"},
    {"sgn-be-fr",   "sfb"},
    {"sgn-be-nl",   "vgt"},
    {"sgn-ch-de",   "sgg"},
    {"no-bok",      "nb"},
    {"no-nyn",      "nn"},
    {"zh-guoyu",    "zh"},
    {"zh-hakka",    "hak"},
    {"zh-xiang",    "hsn"},
    {"zh-min-nan",  "nan"},
    {"zh-min",      "nan-x-zh-min"},
    {NULL, NULL}
};
static const struct { const char *from; const char *to; } REGION_ALIAS[] = {
    /* CLDR territoryAlias subset for regions test262 checks. */
    {"SU", "RU"}, {"DD", "DE"}, {"YU", "RS"}, {"BU", "MM"},
    {"AN", "CW"}, {"CS", "RS"}, {"NT", "SA"}, {"YD", "YE"},
    {"ZR", "CD"}, {"TP", "TL"}, {"DY", "BJ"}, {"UV", "BF"},
    {"HV", "BF"}, {"RH", "ZW"}, {"VD", "VN"},
    {NULL, NULL}
};
static const struct { const char *from; const char *to; } SUBDIV_ALIAS[] = {
    /* CLDR subdivisionAlias subset. */
    {"no23", "no50"}, {"czjc", "cz31"}, {"czjm", "cz64"},
    {"czpl", "cz41"}, {"czpa", "cz53"}, {"czli", "cz51"},
    {"frh", "fr20r"}, {"frcp", "fr20r"},
    {NULL, NULL}
};

/* Rewrite a BCP 47 tag through CLDR alias tables.  Operates on a
   mutable buffer; returns the buffer.  Idempotent. */
static void apply_cldr_aliases(char *tag, size_t cap)
{
    /* Match grandfathered/full-tag aliases first. */
    for (int i = 0; LANG_ALIAS[i].from; i++) {
        if (!strcmp(tag, LANG_ALIAS[i].from)) {
            strncpy(tag, LANG_ALIAS[i].to, cap - 1);
            tag[cap - 1] = 0;
            return;
        }
    }
    /* Match language subtag (first hyphen-separated segment). */
    const char *dash = strchr(tag, '-');
    size_t llen = dash ? (size_t)(dash - tag) : strlen(tag);
    for (int i = 0; LANG_ALIAS[i].from; i++) {
        const char *from = LANG_ALIAS[i].from;
        /* Skip multi-subtag alias entries (handled above). */
        if (strchr(from, '-')) continue;
        if (strlen(from) != llen) continue;
        if (strncmp(tag, from, llen) == 0) {
            char tail[256];
            strncpy(tail, dash ? dash : "", sizeof(tail) - 1);
            tail[sizeof(tail) - 1] = 0;
            int n = snprintf(tag, cap, "%s%s", LANG_ALIAS[i].to, tail);
            (void)n;
            break;
        }
    }
    /* Region subtag (the 2nd segment if it's exactly 2 alpha chars
       or 3 digits).  Walk segments looking for the region position. */
    char *p = tag;
    char *seg_start = tag;
    /* Skip past language subtag(s). */
    if ((p = strchr(tag, '-')) != NULL) {
        seg_start = p + 1;
        /* If next subtag is 4 alpha (script), skip past it. */
        char *next = strchr(seg_start, '-');
        size_t sl = next ? (size_t)(next - seg_start) : strlen(seg_start);
        int alpha = 1;
        for (size_t k = 0; k < sl; k++)
            if (!((seg_start[k] >= 'a' && seg_start[k] <= 'z')
               || (seg_start[k] >= 'A' && seg_start[k] <= 'Z'))) { alpha = 0; break; }
        if (sl == 4 && alpha && next) seg_start = next + 1;
        /* Now check if this segment is a 2-alpha region. */
        char *seg_end = strchr(seg_start, '-');
        sl = seg_end ? (size_t)(seg_end - seg_start) : strlen(seg_start);
        if (sl == 2 || (sl == 3 && seg_start[0] >= '0' && seg_start[0] <= '9')) {
            char region[8] = {0};
            for (size_t k = 0; k < sl && k < 7; k++)
                region[k] = (seg_start[k] >= 'a' && seg_start[k] <= 'z')
                            ? (seg_start[k] - 'a' + 'A') : seg_start[k];
            for (int i = 0; REGION_ALIAS[i].from; i++) {
                if (!strcmp(region, REGION_ALIAS[i].from)) {
                    char tmp[256];
                    strncpy(tmp, tag, (size_t)(seg_start - tag));
                    tmp[seg_start - tag] = 0;
                    strncat(tmp, REGION_ALIAS[i].to, sizeof(tmp) - strlen(tmp) - 1);
                    if (seg_end) strncat(tmp, seg_end, sizeof(tmp) - strlen(tmp) - 1);
                    strncpy(tag, tmp, cap - 1);
                    tag[cap - 1] = 0;
                    break;
                }
            }
        }
    }
    /* Subdivision (`-u-sd-XXXX` or `-u-rg-XXXX`).  Look for -sd- or -rg-
       prefix and rewrite the value through SUBDIV_ALIAS. */
    static const char *SUB_KEYS[] = {"-sd-", "-rg-", NULL};
    for (int i = 0; SUB_KEYS[i]; i++) {
        char *pos = strstr(tag, SUB_KEYS[i]);
        if (!pos) continue;
        char *val = pos + 4;
        char *val_end = strchr(val, '-');
        size_t vl = val_end ? (size_t)(val_end - val) : strlen(val);
        char vbuf[16] = {0};
        for (size_t k = 0; k < vl && k < 15; k++)
            vbuf[k] = val[k];
        for (int j = 0; SUBDIV_ALIAS[j].from; j++) {
            if (!strcmp(vbuf, SUBDIV_ALIAS[j].from)) {
                char tmp[256];
                strncpy(tmp, tag, (size_t)(val - tag));
                tmp[val - tag] = 0;
                strncat(tmp, SUBDIV_ALIAS[j].to, sizeof(tmp) - strlen(tmp) - 1);
                if (val_end) strncat(tmp, val_end, sizeof(tmp) - strlen(tmp) - 1);
                strncpy(tag, tmp, cap - 1);
                tag[cap - 1] = 0;
                break;
            }
        }
    }
}

/* Convert an ICU locale id (possibly with `_` and `@keyword=value`)
   to a canonical BCP 47 language tag (`-` separators, `-u-` extensions).
   Returns ptr into a static buffer overwritten on each call — fine
   for the immediate push_string consumer that comes right after.
   ECMA-402 `resolvedOptions().locale` must be BCP 47. */
static const char *icu_to_bcp47(const char *icu)
{
    static char buf[128];
    if (!icu || !*icu) { buf[0] = 0; return buf; }
    UErrorCode err = U_ZERO_ERROR;
    int32_t n = uloc_toLanguageTag(icu, buf, sizeof(buf), 0, &err);
    if (U_FAILURE(err) || n <= 0) {
        /* Fallback: copy as-is with underscores → hyphens. */
        size_t i = 0;
        for (; icu[i] && i < sizeof(buf)-1; i++) {
            buf[i] = (icu[i] == '_') ? '-' : icu[i];
        }
        buf[i] = 0;
    }
    return buf;
}

/* Brand check: `this` (at stack top) must carry the named internal
   slot.  Throws TypeError if not — matches the spec's `IsX(this)`
   abstract operation that every Intl prototype method runs first.
   `require_buffer` distinguishes the ICU-handle case (buffer-typed)
   from the Locale/DurationFormat case (string-typed slot). */
static void require_internal_slot_kind(duk_context *ctx, const char *slot,
                                       const char *kind, int require_buffer)
{
    if (!duk_is_object(ctx, -1))
        RP_TYPE_THROW(ctx, "Intl.%s: receiver is not an object", kind);
    duk_get_prop_string(ctx, -1, slot);
    int ok = require_buffer ? duk_is_buffer_data(ctx, -1)
                            : !duk_is_undefined(ctx, -1);
    duk_pop(ctx);
    if (!ok)
        RP_TYPE_THROW(ctx, "Intl.%s: receiver lacks internal slot", kind);
}
static void require_internal_slot(duk_context *ctx, const char *slot, const char *kind)
{
    require_internal_slot_kind(ctx, slot, kind, 1);
}

/* Validate a locale tag (BCP 47). Returns the canonicalized tag
   (in malloc'd buffer? — but we need it returned safely). Simpler:
   throw RangeError if the input is malformed.  ECMA-402 explicitly
   rejects tags with underscores (e.g. "en_US") — those are ICU's
   internal form, not BCP 47. */
static void validate_locale_tag(duk_context *ctx, const char *tag)
{
    if (!tag || !*tag) return;
    /* Underscore is the ICU-internal locale separator; BCP 47 uses
       hyphens.  Reject as an obvious mistake. */
    if (strchr(tag, '_'))
        RP_RANGE_THROW(ctx, "Invalid locale: %s", tag);
    /* Reject obvious malformed: contains spaces, empty subtags, etc. */
    if (strchr(tag, ' '))
        RP_RANGE_THROW(ctx, "Invalid locale: %s", tag);
    /* ICU's uloc_forLanguageTag is permissive but does flag some
       grossly malformed tags.  `und` (BCP 47 "undetermined") parses
       to an empty ICU id (n=0) but is a valid language tag —
       check only U_FAILURE. */
    char canon[256]; UErrorCode err = U_ZERO_ERROR;
    int32_t plen = 0;
    (void)uloc_forLanguageTag(tag, canon, sizeof(canon), &plen, &err);
    if (U_FAILURE(err))
        RP_RANGE_THROW(ctx, "Invalid locale: %s", tag);
    /* plen < strlen(tag) means ICU didn't consume the whole tag — that
       indicates the trailing portion is malformed. */
    if (plen > 0 && (size_t)plen < strlen(tag))
        RP_RANGE_THROW(ctx, "Invalid locale: %s", tag);
}

/* Per ECMA-402 GetOptionsObject: if `options` is not undefined and
   not an object, throw TypeError.  Call once at the top of each
   construct to reject explicit `null` (and other non-object values). */
static __attribute__((unused)) void
require_options_arg(duk_context *ctx, duk_idx_t opts_idx,
                                const char *kind)
{
    if (opts_idx == DUK_INVALID_INDEX) return;
    if (duk_is_undefined(ctx, opts_idx)) return;
    if (!duk_is_object(ctx, opts_idx))
        RP_TYPE_THROW(ctx, "Intl.%s: options must be an object or undefined", kind);
}

/* Validate an option string against a NULL-terminated allowlist.
   Returns the value pointer on success (with the value still on the
   stack so caller can duk_pop), or NULL if the property is absent /
   undefined.  Throws RangeError if present-but-not-allowed.  Pass
   reject_empty=1 to also throw on empty string (most enum options
   don't allow ""). */
static const char *validate_enum_opt(duk_context *ctx, duk_idx_t opts_idx,
                                     const char *key, const char *const *allowed,
                                     int reject_empty)
{
    if (opts_idx == DUK_INVALID_INDEX) return NULL;
    if (!duk_is_object(ctx, opts_idx)) return NULL;
    duk_get_prop_string(ctx, opts_idx, key);
    /* Per ECMA-402 §9.2.10 (GetOption): only `undefined` means "absent";
       `null` is coerced via ToString to "null" and then rejected by the
       allowlist.  Don't short-circuit null here. */
    if (duk_is_undefined(ctx, -1)) {
        duk_pop(ctx);
        return NULL;
    }
    /* Coerce to string per spec.  This invokes Symbol.toPrimitive /
       toString on objects — if those throw, the error propagates. */
    const char *v = duk_to_string(ctx, -1);
    if (reject_empty && !*v) {
        RP_RANGE_THROW(ctx, "Invalid %s option: empty string", key);
    }
    if (allowed) {
        for (int i = 0; allowed[i]; i++) {
            if (!strcmp(allowed[i], v)) return v;
        }
        RP_RANGE_THROW(ctx, "Invalid %s option: %s", key, v);
    }
    return v;
}

/* ---------------- helpers ---------------- */

/* Hidden slot prefix on the constructed Intl object: we attach the
   ICU resource handle as a buffer property. Keep these short and
   unlikely to conflict. */
#define K_UDAT  "\xff" "udat"
#define K_UNUM  "\xff" "unum"
#define K_UCOL  "\xff" "ucol"
#define K_LOC   "\xff" "loc"
#define K_OPTS  "\xff" "opts"
#define K_TZ    "\xff" "tz"
#define K_UPRL  "\xff" "uprl"   /* PluralRules */
#define K_URLT  "\xff" "urlt"   /* RelativeTimeFormat */
#define K_ULST  "\xff" "ulst"   /* ListFormat */
#define K_ULDN  "\xff" "uldn"   /* DisplayNames */
#define K_UBRK  "\xff" "ubrk"   /* Segmenter */
#define K_TYPE  "\xff" "type"
#define K_STYLE "\xff" "style"
#define K_SKEL  "\xff" "skel"  /* date skeleton string for formatRange */

/* Pull a string property from the options object at idx, or NULL.
   Caller must duk_pop after using the returned const char*. */
static const char *opt_get_string(duk_context *ctx, duk_idx_t opts_idx, const char *key)
{
    if (duk_is_undefined(ctx, opts_idx) || duk_is_null(ctx, opts_idx)) return NULL;
    if (!duk_is_object(ctx, opts_idx)) return NULL;
    duk_get_prop_string(ctx, opts_idx, key);
    if (duk_is_string(ctx, -1))
        return duk_get_string(ctx, -1);
    duk_pop(ctx);
    return NULL;
}

/* Duktape stores strings as CESU-8: BMP code points are encoded as
   1–3 byte UTF-8 sequences, and supplementary code points are encoded
   as two 3-byte sequences representing the UTF-16 surrogate pair.
   ICU's u_strFromUTF8 is strict UTF-8 and rejects surrogate code
   points as malformed, so we walk the bytes ourselves.  This handles
   both pure-ASCII (the common case) and emoji/CJK supplementary. */
static UChar *utf8_to_uchar(const char *cesu, int32_t bytes, int32_t *out_len)
{
    if (!cesu) return NULL;
    if (bytes < 0) bytes = (int32_t)strlen(cesu);
    UChar *out = (UChar *)malloc(sizeof(UChar) * (size_t)(bytes + 2));
    if (!out) return NULL;
    int32_t n = 0, i = 0;
    const unsigned char *s = (const unsigned char *)cesu;
    while (i < bytes) {
        unsigned c = s[i];
        uint32_t cp;
        if (c < 0x80) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < bytes) {
            cp = ((c & 0x1Fu) << 6) | (s[i+1] & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < bytes) {
            cp = ((c & 0x0Fu) << 12) | ((s[i+1] & 0x3Fu) << 6) | (s[i+2] & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < bytes) {
            /* Real 4-byte UTF-8 (rare from duktape) — emit pair. */
            cp = ((c & 0x07u) << 18) | ((s[i+1] & 0x3Fu) << 12)
               | ((s[i+2] & 0x3Fu) << 6) | (s[i+3] & 0x3Fu);
            i += 4;
            if (cp > 0xFFFF) {
                cp -= 0x10000;
                out[n++] = (UChar)(0xD800u | (cp >> 10));
                out[n++] = (UChar)(0xDC00u | (cp & 0x3FFu));
                continue;
            }
        } else {
            /* malformed: skip */
            i += 1; continue;
        }
        if (cp <= 0xFFFF) {
            out[n++] = (UChar)cp;
        } else {
            cp -= 0x10000;
            out[n++] = (UChar)(0xD800u | (cp >> 10));
            out[n++] = (UChar)(0xDC00u | (cp & 0x3FFu));
        }
    }
    out[n] = 0;
    if (out_len) *out_len = n;
    return out;
}

/* UTF-16 → CESU-8 → push as duktape string.  Each surrogate emits its
   own 3-byte sequence (matches duktape's internal representation). */
static void uchar_push_utf8(duk_context *ctx, const UChar *u, int32_t ulen)
{
    if (!u || ulen == 0) { duk_push_string(ctx, ""); return; }
    /* Worst-case: every UChar is 3 bytes of CESU-8. */
    char *buf = (char *)malloc((size_t)ulen * 3 + 1);
    if (!buf) { duk_push_string(ctx, ""); return; }
    size_t b = 0;
    for (int32_t i = 0; i < ulen; i++) {
        UChar c = u[i];
        if (c < 0x80) {
            buf[b++] = (char)c;
        } else if (c < 0x800) {
            buf[b++] = (char)(0xC0u | (c >> 6));
            buf[b++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            /* Includes surrogate code points — encode as 3 bytes each. */
            buf[b++] = (char)(0xE0u | (c >> 12));
            buf[b++] = (char)(0x80u | ((c >> 6) & 0x3Fu));
            buf[b++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    duk_push_lstring(ctx, buf, (duk_size_t)b);
    free(buf);
}

/* ---------------- DateTimeFormat ---------------- */

/* Map ECMA-402 dateStyle/timeStyle strings -> ICU UDateFormatStyle */
static UDateFormatStyle ds_to_icu(const char *s, UDateFormatStyle dflt)
{
    if (!s) return dflt;
    if (strcmp(s, "full")   == 0) return UDAT_FULL;
    if (strcmp(s, "long")   == 0) return UDAT_LONG;
    if (strcmp(s, "medium") == 0) return UDAT_MEDIUM;
    if (strcmp(s, "short")  == 0) return UDAT_SHORT;
    return dflt;
}

/* Build an ICU skeleton string from the ECMA-402 option components.
   The skeleton is a pattern of letters (y, M, d, H, m, s, …) that
   ICU then resolves to a locale-appropriate pattern. */
static void build_skeleton(duk_context *ctx, duk_idx_t opts_idx, char *sk, size_t sksz)
{
    sk[0] = 0;
    if (opts_idx == DUK_INVALID_INDEX) return;
    if (duk_is_undefined(ctx, opts_idx) || duk_is_null(ctx, opts_idx)) return;
    if (!duk_is_object(ctx, opts_idx)) return;

#define APPEND_IF(field, mapping) do { \
    duk_get_prop_string(ctx, opts_idx, field); \
    if (duk_is_string(ctx, -1)) { \
        const char *v = duk_get_string(ctx, -1); \
        mapping; \
    } \
    duk_pop(ctx); \
} while (0)

    APPEND_IF("year", {
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "y",    sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "yy",   sksz - strlen(sk) - 1);
    });
    APPEND_IF("month", {
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "M",    sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "MM",   sksz - strlen(sk) - 1);
        else if (strcmp(v, "long")     == 0) strncat(sk, "MMMM", sksz - strlen(sk) - 1);
        else if (strcmp(v, "short")    == 0) strncat(sk, "MMM",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "narrow")   == 0) strncat(sk, "MMMMM",sksz - strlen(sk) - 1);
    });
    APPEND_IF("day", {
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "d",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "dd", sksz - strlen(sk) - 1);
    });
    APPEND_IF("weekday", {
        if      (strcmp(v, "long")     == 0) strncat(sk, "EEEE",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "short")    == 0) strncat(sk, "EEE",   sksz - strlen(sk) - 1);
        else if (strcmp(v, "narrow")   == 0) strncat(sk, "EEEEE", sksz - strlen(sk) - 1);
    });
    APPEND_IF("hour", {
        /* default to 24h ("H"); options.hour12 can flip below */
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "H",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "HH", sksz - strlen(sk) - 1);
    });
    APPEND_IF("minute", {
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "m",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "mm", sksz - strlen(sk) - 1);
    });
    APPEND_IF("second", {
        if      (strcmp(v, "numeric")  == 0) strncat(sk, "s",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "2-digit")  == 0) strncat(sk, "ss", sksz - strlen(sk) - 1);
    });
    APPEND_IF("timeZoneName", {
        if      (strcmp(v, "short")    == 0) strncat(sk, "z",     sksz - strlen(sk) - 1);
        else if (strcmp(v, "long")     == 0) strncat(sk, "zzzz",  sksz - strlen(sk) - 1);
        else if (strcmp(v, "shortOffset") == 0) strncat(sk, "O",   sksz - strlen(sk) - 1);
        else if (strcmp(v, "longOffset")  == 0) strncat(sk, "OOOO",sksz - strlen(sk) - 1);
    });
    APPEND_IF("dayPeriod", {
        if      (strcmp(v, "narrow") == 0) strncat(sk, "BBBBB", sksz - strlen(sk) - 1);
        else if (strcmp(v, "short")  == 0) strncat(sk, "B",     sksz - strlen(sk) - 1);
        else if (strcmp(v, "long")   == 0) strncat(sk, "BBBB",  sksz - strlen(sk) - 1);
    });

    /* fractionalSecondDigits: 1..3 -> S, SS, SSS */
    duk_get_prop_string(ctx, opts_idx, "fractionalSecondDigits");
    if (duk_is_number(ctx, -1)) {
        int n = (int)duk_get_int(ctx, -1);
        if (n >= 1 && n <= 3) {
            char tmp[5] = "SSS";
            tmp[n] = 0;
            strncat(sk, tmp, sksz - strlen(sk) - 1);
        }
    }
    duk_pop(ctx);

    /* hour12 / hourCycle handling: replace H with h if 12-hour.
       When dayPeriod is set, the spec implies 12-hour cycle (since
       dayPeriod is meaningless with 24h time).  Also: when hour=numeric
       + dayPeriod, the locale default for "en" is 12-hour format. */
    int twelve = -1;
    duk_get_prop_string(ctx, opts_idx, "hour12");
    if (duk_is_boolean(ctx, -1)) twelve = duk_get_boolean(ctx, -1) ? 1 : 0;
    duk_pop(ctx);
    duk_get_prop_string(ctx, opts_idx, "hourCycle");
    if (duk_is_string(ctx, -1))
    {
        const char *hc = duk_get_string(ctx, -1);
        if (strcmp(hc, "h11") == 0 || strcmp(hc, "h12") == 0) twelve = 1;
        else if (strcmp(hc, "h23") == 0 || strcmp(hc, "h24") == 0) twelve = 0;
    }
    duk_pop(ctx);
    /* If dayPeriod is requested and the user didn't explicitly pick a
       cycle, default to 12-hour — dayPeriod is the "am/pm/morning/night"
       string and only makes sense in 12-hour context. */
    if (twelve == -1) {
        duk_get_prop_string(ctx, opts_idx, "dayPeriod");
        if (duk_is_string(ctx, -1)) twelve = 1;
        duk_pop(ctx);
    }
    if (twelve == 1)
    {
        for (char *p = sk; *p; p++)
            if (*p == 'H') *p = 'h';
    }
#undef APPEND_IF
}

/* DateTimeFormat constructor: `new Intl.DateTimeFormat(locale, opts)`.
   ECMA-402 also defines `Intl.DateTimeFormat()` (no `new`) as legal —
   it should produce an instance equivalent to `new Intl.DateTimeFormat()`.
   Forward to `new` via the current function reference. */
/* Parse a UTC offset timezone string into canonical "+HH:MM" form.
   Accepts "+H[H][:MM]" / "+HHMM".  Rejects Unicode minus (U+2212),
   single-digit hour, out-of-range, anything that doesn't start with
   ASCII +/-.  Returns 0 on success, -1 on malformed.  On success
   writes canonical "+HH:MM" to canon (size 8+) and ICU-friendly
   "GMT+HH:MM" to icu_tz (size 12+).  Outputs `+00:00` for "-00" too
   (ECMA-402: negative zero is normalized to positive). */
static int normalize_offset_tz(const char *s, char *canon, size_t canon_sz,
                               char *icu_tz, size_t icu_sz)
{
    if (!s) return -1;
    /* Must start with ASCII + or - (not Unicode minus U+2212 = 0xE2 0x88 0x92). */
    if ((unsigned char)s[0] == 0xE2) return -1;
    if (s[0] != '+' && s[0] != '-') return -1;
    int sign = (s[0] == '-') ? -1 : 1;
    const char *p = s + 1;
    /* Hours: must be 2 digits. */
    if (!(p[0] >= '0' && p[0] <= '9') || !(p[1] >= '0' && p[1] <= '9'))
        return -1;
    int hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    int mm = 0;
    if (*p == ':') {
        p++;
        if (!(p[0] >= '0' && p[0] <= '9') || !(p[1] >= '0' && p[1] <= '9'))
            return -1;
        mm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    } else if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
        /* +HHMM form */
        mm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    }
    if (*p) return -1;
    if (hh > 23 || mm > 59) return -1;
    /* Normalize -00:00 → +00:00 per spec. */
    if (sign < 0 && hh == 0 && mm == 0) sign = 1;
    snprintf(canon, canon_sz, "%c%02d:%02d", sign < 0 ? '-' : '+', hh, mm);
    snprintf(icu_tz, icu_sz, "GMT%c%02d:%02d", sign < 0 ? '-' : '+', hh, mm);
    return 0;
}

static duk_ret_t dtf_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx)) {
        duk_idx_t n_args = duk_get_top(ctx);
        duk_push_current_function(ctx);
        duk_insert(ctx, 0);
        duk_new(ctx, (duk_idx_t)n_args);
        return 1;
    }

    const char *locale_in = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale_in = duk_get_string(ctx, 0);
    if (locale_in && *locale_in) validate_locale_tag(ctx, locale_in);
    if (!locale_in || !*locale_in) locale_in = uloc_getDefault();

    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }

    /* ECMA-402 enum validation for DateTimeFormat options.  Each
       throws RangeError on invalid values (incl. empty string). */
    static const char *DS_VALS[] = {"full","long","medium","short", NULL};
    static const char *YEAR_VALS[] = {"numeric","2-digit", NULL};
    static const char *MONTH_VALS[] = {"numeric","2-digit","long","short","narrow", NULL};
    static const char *WEEKDAY_VALS[] = {"long","short","narrow", NULL};
    static const char *TZN_VALS[] = {"short","long","shortOffset","longOffset","shortGeneric","longGeneric", NULL};
    static const char *DP_VALS[] = {"narrow","short","long", NULL};
    static const char *HC_VALS[] = {"h11","h12","h23","h24", NULL};
    static const char *LM_VALS[] = {"lookup","best fit", NULL};
    static const char *FM_VALS[] = {"basic","best fit", NULL};
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        if (validate_enum_opt(ctx, opts_idx, "dateStyle", DS_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "timeStyle", DS_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "year",      YEAR_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "month",     MONTH_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "day",       YEAR_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "weekday",   WEEKDAY_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "hour",      YEAR_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "minute",    YEAR_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "second",    YEAR_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "timeZoneName", TZN_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "dayPeriod", DP_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "hourCycle", HC_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "formatMatcher", FM_VALS, 1)) duk_pop(ctx);
        /* fractionalSecondDigits: number in [1..3], or undefined */
        duk_get_prop_string(ctx, opts_idx, "fractionalSecondDigits");
        if (!duk_is_undefined(ctx, -1)) {
            double v = duk_to_number(ctx, -1);
            if (!(v >= 1 && v <= 3) || v != (int)v)
                RP_RANGE_THROW(ctx, "fractionalSecondDigits must be 1, 2 or 3");
        }
        duk_pop(ctx);
        /* calendar / numberingSystem: must be non-empty and valid BCP47 type */
        duk_get_prop_string(ctx, opts_idx, "calendar");
        if (!duk_is_undefined(ctx, -1)) {
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid calendar option: empty string");
        }
        duk_pop(ctx);
        duk_get_prop_string(ctx, opts_idx, "numberingSystem");
        if (!duk_is_undefined(ctx, -1)) {
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid numberingSystem option: empty string");
        }
        duk_pop(ctx);
        /* timeZone: non-empty if present.  Offset timezones (`+03`,
           `+03:00`, `+0300`) get canonicalized and reject Unicode minus
           sign.  Other strings go through unchanged and ICU validates. */
        duk_get_prop_string(ctx, opts_idx, "timeZone");
        if (!duk_is_undefined(ctx, -1) && !duk_is_null(ctx, -1)) {
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid timeZone option: empty string");
            /* Reject Unicode minus sign U+2212 (ECMA-402 §6.5.5). */
            if ((unsigned char)v[0] == 0xE2 && (unsigned char)v[1] == 0x88 && (unsigned char)v[2] == 0x92)
                RP_RANGE_THROW(ctx, "Invalid timeZone: Unicode minus sign not allowed");
            if (v[0] == '+' || v[0] == '-') {
                char canon[16], icu_tz[20];
                if (normalize_offset_tz(v, canon, sizeof(canon), icu_tz, sizeof(icu_tz)) != 0)
                    RP_RANGE_THROW(ctx, "Invalid offset timeZone: %s", v);
                /* Replace the option value with the canonical form so
                   downstream reads see "+HH:MM" instead of "+HHMM". */
                duk_push_string(ctx, canon);
                duk_put_prop_string(ctx, opts_idx, "timeZone");
            }
        }
        duk_pop(ctx);

        /* dateStyle/timeStyle MUST NOT be combined with explicit
           component options (year/month/day/...) per ECMA-402 §11.1.1. */
        duk_get_prop_string(ctx, opts_idx, "dateStyle");
        int has_ds = !duk_is_undefined(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, opts_idx, "timeStyle");
        int has_ts = !duk_is_undefined(ctx, -1);
        duk_pop(ctx);
        if (has_ds || has_ts) {
            static const char *COMPONENTS[] = {
                "weekday","era","year","month","day","hour","minute","second",
                "dayPeriod","fractionalSecondDigits","timeZoneName", NULL
            };
            for (int i = 0; COMPONENTS[i]; i++) {
                duk_get_prop_string(ctx, opts_idx, COMPONENTS[i]);
                int has = !duk_is_undefined(ctx, -1);
                duk_pop(ctx);
                if (has) RP_TYPE_THROW(ctx,
                    "DateTimeFormat: %s and %s cannot both be set",
                    has_ds ? "dateStyle" : "timeStyle", COMPONENTS[i]);
            }
        }
    }

    /* Convert BCP 47 → ICU locale id so Unicode extensions
       (`-u-nu-arab`, `-u-ca-buddhist`, `-u-hc-h23`) become ICU
       keywords (`@numbers=arab;calendar=buddhist;hours=h23`) that
       udat_open / udatpg_open honor.  Then bake any explicit option
       overrides (calendar, numberingSystem) on top via
       uloc_setKeywordValue. */
    char locale_buf[256];
    {
        UErrorCode e2 = U_ZERO_ERROR;
        int32_t n = uloc_forLanguageTag(locale_in, locale_buf,
                                        sizeof(locale_buf), NULL, &e2);
        if (U_FAILURE(e2) || n <= 0) {
            strncpy(locale_buf, locale_in, sizeof(locale_buf) - 1);
            locale_buf[sizeof(locale_buf) - 1] = 0;
        }
    }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        const char *cal = opt_get_string(ctx, opts_idx, "calendar");
        if (cal) {
            /* Canonicalize legacy calendar names (e.g. "islamicc" →
               "islamic-civil", "gregorian" → "gregory") so
               resolvedOptions() reports the modern Unicode locale
               type that ECMA-402 mandates. */
            const char *can = uloc_toUnicodeLocaleType("calendar", cal);
            UErrorCode e2 = U_ZERO_ERROR;
            uloc_setKeywordValue("calendar", can ? can : cal,
                                 locale_buf, sizeof(locale_buf), &e2);
            duk_pop(ctx);
        }
        const char *ns = opt_get_string(ctx, opts_idx, "numberingSystem");
        if (ns) {
            const char *can = uloc_toUnicodeLocaleType("numbers", ns);
            UErrorCode e2 = U_ZERO_ERROR;
            uloc_setKeywordValue("numbers", can ? can : ns,
                                 locale_buf, sizeof(locale_buf), &e2);
            duk_pop(ctx);
        }
    }
    const char *locale = locale_buf;

    /* dateStyle/timeStyle vs. component skeleton — they're mutually
       exclusive in ECMA-402; if either style is present we use
       udat_open(timeStyle, dateStyle); otherwise we build a skeleton. */
    const char *ds = NULL, *ts = NULL;
    if (opts_idx != DUK_INVALID_INDEX)
    {
        ds = opt_get_string(ctx, opts_idx, "dateStyle"); if (ds) duk_pop(ctx);
        ts = opt_get_string(ctx, opts_idx, "timeStyle"); if (ts) duk_pop(ctx);
    }

    /* timeZone */
    const char *tz_utf8 = NULL;
    if (opts_idx != DUK_INVALID_INDEX)
    {
        tz_utf8 = opt_get_string(ctx, opts_idx, "timeZone"); /* leaves on stack */
    }
    UChar *tz_uchar = NULL;
    int32_t tz_uchar_len = 0;
    if (tz_utf8)
    {
        /* If it's an offset, ICU needs the "GMT" prefix to recognize it. */
        const char *icu_in = tz_utf8;
        char icu_tz[20];
        if ((tz_utf8[0] == '+' || tz_utf8[0] == '-')
            && tz_utf8[1] >= '0' && tz_utf8[1] <= '9') {
            char canon[16];
            if (normalize_offset_tz(tz_utf8, canon, sizeof(canon),
                                    icu_tz, sizeof(icu_tz)) == 0)
                icu_in = icu_tz;
        }
        tz_uchar = utf8_to_uchar(icu_in, (int32_t)strlen(icu_in), &tz_uchar_len);
        duk_pop(ctx); /* the tz property */
    }

    UErrorCode err = U_ZERO_ERROR;
    UDateFormat *udat = NULL;
    char skeleton[128]; skeleton[0] = 0;  /* saved on `this` for formatRange */

    if (ds || ts)
    {
        UDateFormatStyle d_st = ds_to_icu(ds, UDAT_NONE);
        UDateFormatStyle t_st = ds_to_icu(ts, UDAT_NONE);
        udat = udat_open(t_st, d_st, locale, tz_uchar, tz_uchar_len, NULL, -1, &err);
        /* Derive a reasonable skeleton from dateStyle/timeStyle for
           range formatting (udtitvfmt requires a skeleton, not a style). */
        if (ds && ts) strcpy(skeleton, "yMMMdjm");
        else if (ds)  strcpy(skeleton, "yMMMd");
        else if (ts)  strcpy(skeleton, "jm");
    }
    else
    {
        build_skeleton(ctx, opts_idx, skeleton, sizeof(skeleton));
        if (!skeleton[0])
        {
            /* Default: year + month + day (matches node's empty-options
               behavior — `new Intl.DateTimeFormat().format(new Date())`
               returns "M/D/YYYY"). */
            strcpy(skeleton, "yMd");
        }
        /* Convert skeleton UTF-8 → UChar */
        UChar sk_u[128];
        int32_t sk_u_len = 0;
        u_strFromUTF8(sk_u, 128, &sk_u_len, skeleton, -1, &err);
        if (U_FAILURE(err))
        {
            if (tz_uchar) free(tz_uchar);
            RP_THROW(ctx, "Intl.DateTimeFormat: bad skeleton");
        }
        /* Best-match pattern */
        UDateTimePatternGenerator *gen = udatpg_open(locale, &err);
        if (U_FAILURE(err))
        {
            if (tz_uchar) free(tz_uchar);
            RP_THROW(ctx, "Intl.DateTimeFormat: udatpg_open failed: %s", u_errorName(err));
        }
        UChar pattern[256];
        int32_t pattern_len = udatpg_getBestPattern(gen, sk_u, sk_u_len, pattern, 256, &err);
        udatpg_close(gen);
        if (U_FAILURE(err))
        {
            if (tz_uchar) free(tz_uchar);
            RP_THROW(ctx, "Intl.DateTimeFormat: getBestPattern failed: %s", u_errorName(err));
        }
        udat = udat_open(UDAT_PATTERN, UDAT_PATTERN, locale, tz_uchar, tz_uchar_len,
                         pattern, pattern_len, &err);
    }

    if (U_FAILURE(err) || !udat)
    {
        if (tz_uchar) free(tz_uchar);
        RP_THROW(ctx, "Intl.DateTimeFormat: udat_open failed: %s",
                 U_FAILURE(err) ? u_errorName(err) : "(null)");
    }

    /* Push the constructed `this`. Stash the UDateFormat* pointer as
       a buffer property; record locale + timeZone for resolvedOptions. */
    duk_push_this(ctx);

    void *buf = duk_push_fixed_buffer(ctx, sizeof(UDateFormat *));
    *(UDateFormat **)buf = udat;
    duk_put_prop_string(ctx, -2, K_UDAT);

    duk_push_string(ctx, locale);
    duk_put_prop_string(ctx, -2, K_LOC);

    if (tz_utf8) {
        duk_push_string(ctx, tz_utf8);
        duk_put_prop_string(ctx, -2, K_TZ);
    }
    if (skeleton[0]) {
        duk_push_string(ctx, skeleton);
        duk_put_prop_string(ctx, -2, K_SKEL);
    }
    /* Stash the original opts for resolvedOptions to echo back the
       component options the user requested in spec-defined order. */
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        duk_dup(ctx, opts_idx);
        duk_put_prop_string(ctx, -2, K_OPTS);
    }

    if (tz_uchar) free(tz_uchar);
    return 0; /* constructor: leave `this` on stack via auto-return */
}

/* Finalizer: close ICU handle when the JS wrapper is GC'd. */
static duk_ret_t dtf_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_UDAT);
    if (duk_is_buffer_data(ctx, -1))
    {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UDateFormat *))
        {
            UDateFormat *udat = *(UDateFormat **)p;
            if (udat) udat_close(udat);
            *(UDateFormat **)p = NULL;
        }
    }
    duk_pop(ctx);
    /* Close the optional interval-format handle as well. */
    duk_get_prop_string(ctx, 0, "\xff" "udti");
    if (duk_is_buffer_data(ctx, -1))
    {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UDateIntervalFormat *))
        {
            UDateIntervalFormat *fmt = *(UDateIntervalFormat **)p;
            if (fmt) udtitvfmt_close(fmt);
            *(UDateIntervalFormat **)p = NULL;
        }
    }
    return 0;
}

/* Helper: extract the UDateFormat* + the date-number from a `this`
   reference plus the arg. Returns the formatter or throws. */
static UDateFormat *dtf_get(duk_context *ctx, UDate *out_date)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UDAT);
    duk_size_t n = 0;
    void *p = duk_get_buffer_data(ctx, -1, &n);
    duk_pop_2(ctx);
    if (!p || n != sizeof(UDateFormat *))
        RP_TYPE_THROW(ctx, "Intl.DateTimeFormat: invoked on non-DateTimeFormat");
    UDateFormat *udat = *(UDateFormat **)p;
    if (!udat)
        RP_THROW(ctx, "Intl.DateTimeFormat: handle closed");

    /* arg: Date or millis (number) — both accepted.
       ECMA-262 TimeClip: truncate toward zero, reject ±Infinity / NaN
       (NaN must throw RangeError per ECMA-402).  trunc(-0.9) == -0
       which is equivalent to 0; `format(-0.9)` should match `format(0)`. */
    double raw;
    if (duk_get_top(ctx) > 0 && !duk_is_undefined(ctx, 0))
    {
        if (duk_is_number(ctx, 0))
            raw = duk_get_number(ctx, 0);
        else if (duk_is_object(ctx, 0))
        {
            duk_get_prop_string(ctx, 0, "getTime");
            duk_dup(ctx, 0);
            duk_call_method(ctx, 0);
            raw = duk_get_number(ctx, -1);
            duk_pop(ctx);
        }
        else
            raw = duk_to_number(ctx, 0);
    }
    else
    {
        /* No arg: format `now` (matches V8 behavior). */
        duk_eval_string(ctx, "Date.now()");
        raw = duk_get_number(ctx, -1);
        duk_pop(ctx);
    }
    if (raw != raw) /* NaN */
        RP_RANGE_THROW(ctx, "Intl.DateTimeFormat.format: invalid time value (NaN)");
    if (raw > 8.64e15 || raw < -8.64e15)
        RP_RANGE_THROW(ctx, "Intl.DateTimeFormat.format: time value out of range");
    /* TimeClip: trunc toward zero. */
    UDate t = (UDate)(raw < 0 ? -((double)(int64_t)(-raw)) : (double)(int64_t)raw);
    *out_date = t;
    return udat;
}

/* .format(date) → string */
static duk_ret_t dtf_format(duk_context *ctx)
{
    UDate t;
    UDateFormat *udat = dtf_get(ctx, &t);

    UErrorCode err = U_ZERO_ERROR;
    UChar out[256];
    int32_t out_len = udat_format(udat, t, out, 256, NULL, &err);
    if (err == U_BUFFER_OVERFLOW_ERROR)
    {
        err = U_ZERO_ERROR;
        UChar *big = malloc(sizeof(UChar) * (out_len + 1));
        out_len = udat_format(udat, t, big, out_len + 1, NULL, &err);
        if (U_FAILURE(err)) { free(big); RP_THROW(ctx, "udat_format: %s", u_errorName(err)); }
        uchar_push_utf8(ctx, big, out_len);
        free(big);
        return 1;
    }
    if (U_FAILURE(err))
        RP_THROW(ctx, "udat_format: %s", u_errorName(err));
    uchar_push_utf8(ctx, out, out_len);
    return 1;
}

/* Map ICU UDateFormatField -> ECMA-402 part type name. */
static const char *udat_field_type(int32_t f)
{
    switch (f)
    {
        case UDAT_ERA_FIELD:                       return "era";
        case UDAT_YEAR_FIELD:                      return "year";
        case UDAT_YEAR_WOY_FIELD:                  return "year";
        case UDAT_RELATED_YEAR_FIELD:              return "relatedYear";
        case UDAT_YEAR_NAME_FIELD:                 return "yearName";
        case UDAT_MONTH_FIELD:                     return "month";
        case UDAT_DATE_FIELD:                      return "day";
        case UDAT_HOUR_OF_DAY1_FIELD:              return "hour";
        case UDAT_HOUR_OF_DAY0_FIELD:              return "hour";
        case UDAT_MINUTE_FIELD:                    return "minute";
        case UDAT_SECOND_FIELD:                    return "second";
        case UDAT_FRACTIONAL_SECOND_FIELD:         return "fractionalSecond";
        case UDAT_DAY_OF_WEEK_FIELD:               return "weekday";
        case UDAT_DOW_LOCAL_FIELD:                 return "weekday";
        case UDAT_AM_PM_FIELD:                     return "dayPeriod";
        case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:       return "dayPeriod";
        case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:       return "dayPeriod";
        case UDAT_HOUR1_FIELD:                     return "hour";
        case UDAT_HOUR0_FIELD:                     return "hour";
        case UDAT_TIMEZONE_FIELD:                  return "timeZoneName";
        case UDAT_TIMEZONE_RFC_FIELD:              return "timeZoneName";
        case UDAT_TIMEZONE_GENERIC_FIELD:          return "timeZoneName";
        case UDAT_TIMEZONE_SPECIAL_FIELD:          return "timeZoneName";
        case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD: return "timeZoneName";
        case UDAT_TIMEZONE_ISO_FIELD:              return "timeZoneName";
        case UDAT_TIMEZONE_ISO_LOCAL_FIELD:        return "timeZoneName";
        default:                                    return "literal";
    }
}

/* .formatToParts(date) → [{type, value}, …] */
static duk_ret_t dtf_format_to_parts(duk_context *ctx)
{
    UDate t;
    UDateFormat *udat = dtf_get(ctx, &t);

    UErrorCode err = U_ZERO_ERROR;
    UFieldPositionIterator *fpi = ufieldpositer_open(&err);
    if (U_FAILURE(err))
        RP_THROW(ctx, "ufieldpositer_open: %s", u_errorName(err));

    /* Generous initial buffer; grow if needed. */
    UChar buf[512];
    int32_t out_len = udat_formatForFields(udat, t, buf, 512, fpi, &err);
    UChar *out = buf;
    int allocated = 0;
    if (err == U_BUFFER_OVERFLOW_ERROR)
    {
        err = U_ZERO_ERROR;
        out = malloc(sizeof(UChar) * (out_len + 1));
        allocated = 1;
        out_len = udat_formatForFields(udat, t, out, out_len + 1, fpi, &err);
    }
    if (U_FAILURE(err))
    {
        if (allocated) free(out);
        ufieldpositer_close(fpi);
        RP_THROW(ctx, "udat_formatForFields: %s", u_errorName(err));
    }

    /* Walk the fields in order, emitting [literal slice, field, literal slice, …]. */
    duk_push_array(ctx);
    duk_uarridx_t idx = 0;
    int32_t cursor = 0;
    int32_t field, fstart, fend;
    while ((field = ufieldpositer_next(fpi, &fstart, &fend)) >= 0)
    {
        if (fstart > cursor)
        {
            duk_push_object(ctx);
            duk_push_string(ctx, "literal");
            duk_put_prop_string(ctx, -2, "type");
            uchar_push_utf8(ctx, out + cursor, fstart - cursor);
            duk_put_prop_string(ctx, -2, "value");
            duk_put_prop_index(ctx, -2, idx++);
        }
        duk_push_object(ctx);
        duk_push_string(ctx, udat_field_type(field));
        duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, out + fstart, fend - fstart);
        duk_put_prop_string(ctx, -2, "value");
        duk_put_prop_index(ctx, -2, idx++);
        cursor = fend;
    }
    if (cursor < out_len)
    {
        duk_push_object(ctx);
        duk_push_string(ctx, "literal");
        duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, out + cursor, out_len - cursor);
        duk_put_prop_string(ctx, -2, "value");
        duk_put_prop_index(ctx, -2, idx++);
    }
    if (allocated) free(out);
    ufieldpositer_close(fpi);
    return 1;
}

/* .resolvedOptions() — minimal: { locale, timeZone, calendar:"gregory", numberingSystem:"latn" } */
/* Coerce a JS argument (Date or number) to a UDate millisecond value.
   Throws RangeError on NaN per ECMA-402 §11.5.5. */
static UDate dtf_arg_to_udate(duk_context *ctx, duk_idx_t idx, const char *which)
{
    double t;
    if (duk_is_number(ctx, idx)) {
        t = duk_get_number(ctx, idx);
    } else if (duk_is_object(ctx, idx)) {
        /* Date: call getTime() */
        duk_get_prop_string(ctx, idx, "getTime");
        if (!duk_is_callable(ctx, -1)) {
            duk_pop(ctx);
            RP_TYPE_THROW(ctx, "Intl.DateTimeFormat.formatRange: %s not a Date or number", which);
        }
        duk_dup(ctx, idx);
        duk_call_method(ctx, 0);
        t = duk_get_number(ctx, -1);
        duk_pop(ctx);
    } else {
        RP_TYPE_THROW(ctx, "Intl.DateTimeFormat.formatRange: %s not a Date or number", which);
    }
    if (t != t || t == HUGE_VAL || t == -HUGE_VAL)
        RP_RANGE_THROW(ctx, "Intl.DateTimeFormat.formatRange: %s is non-finite", which);
    return (UDate)t;
}

/* Open a UDateIntervalFormat for the current instance.  Lazy / cached
   on a hidden buffer slot to avoid re-creating per call. */
static UDateIntervalFormat *dtf_get_interval_fmt(duk_context *ctx)
{
    duk_get_prop_string(ctx, -1, "\xff" "udti");
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        duk_pop(ctx);
        if (p && n == sizeof(UDateIntervalFormat *)) {
            UDateIntervalFormat *r = *(UDateIntervalFormat **)p;
            if (r) return r;
        }
    } else {
        duk_pop(ctx);
    }

    /* Build from stored skeleton + locale + timezone. */
    duk_get_prop_string(ctx, -1, K_SKEL);
    const char *skel = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : "yMd";
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : uloc_getDefault();
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TZ);
    const char *tz = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : NULL;
    duk_pop(ctx);

    int32_t skel_len = (int32_t)strlen(skel);
    UChar sk_u[128]; int32_t sk_u_len = 0;
    UErrorCode err = U_ZERO_ERROR;
    u_strFromUTF8(sk_u, 128, &sk_u_len, skel, skel_len, &err);
    UChar *tz_u = NULL; int32_t tz_u_len = 0;
    if (tz) tz_u = utf8_to_uchar(tz, (int32_t)strlen(tz), &tz_u_len);

    err = U_ZERO_ERROR;
    UDateIntervalFormat *r = udtitvfmt_open(loc, sk_u, sk_u_len, tz_u, tz_u_len, &err);
    if (tz_u) free(tz_u);
    if (U_FAILURE(err) || !r)
        RP_THROW(ctx, "Intl.DateTimeFormat.formatRange: open failed: %s", u_errorName(err));

    /* Cache. */
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UDateIntervalFormat *));
    *(UDateIntervalFormat **)buf = r;
    duk_put_prop_string(ctx, -2, "\xff" "udti");
    return r;
}

static duk_ret_t dtf_format_range(duk_context *ctx)
{
    if (duk_get_top(ctx) < 2)
        RP_TYPE_THROW(ctx, "Intl.DateTimeFormat.formatRange requires 2 arguments");
    UDate a = dtf_arg_to_udate(ctx, 0, "startDate");
    UDate b = dtf_arg_to_udate(ctx, 1, "endDate");

    duk_push_this(ctx);
    require_internal_slot(ctx, K_UDAT, "DateTimeFormat");
    UDateIntervalFormat *fmt = dtf_get_interval_fmt(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedDateInterval *fr = udtitvfmt_openResult(&err);
    if (!fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    udtitvfmt_formatToResult(fmt, a, b, fr, &err);
    if (U_FAILURE(err)) { udtitvfmt_closeResult(fr); RP_THROW(ctx, "formatToResult: %s", u_errorName(err)); }

    const UFormattedValue *fv = udtitvfmt_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *s = ufmtval_getString(fv, &slen, &err);
    if (U_SUCCESS(err) && s) uchar_push_utf8(ctx, s, slen);
    else                     duk_push_string(ctx, "");
    udtitvfmt_closeResult(fr);
    return 1;
}

static duk_ret_t dtf_format_range_to_parts(duk_context *ctx)
{
    if (duk_get_top(ctx) < 2)
        RP_TYPE_THROW(ctx, "Intl.DateTimeFormat.formatRangeToParts requires 2 arguments");
    UDate a = dtf_arg_to_udate(ctx, 0, "startDate");
    UDate b = dtf_arg_to_udate(ctx, 1, "endDate");

    duk_push_this(ctx);
    require_internal_slot(ctx, K_UDAT, "DateTimeFormat");
    UDateIntervalFormat *fmt = dtf_get_interval_fmt(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedDateInterval *fr = udtitvfmt_openResult(&err);
    if (!fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    udtitvfmt_formatToResult(fmt, a, b, fr, &err);
    if (U_FAILURE(err)) { udtitvfmt_closeResult(fr); RP_THROW(ctx, "formatToResult: %s", u_errorName(err)); }

    const UFormattedValue *fv = udtitvfmt_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *str = ufmtval_getString(fv, &slen, &err);

    /* Build coverage + source map.  We track both the field category
       (DATE → type) and the SPAN (which date the field came from →
       source: "startRange"|"endRange"|"shared"). */
    int *cov_field = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    int *cov_src   = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) { cov_field[i] = -1; cov_src[i] = -1; }

    UConstrainedFieldPosition *cfp = ucfpos_open(&err);
    while (ufmtval_nextPosition(fv, cfp, &err) && U_SUCCESS(err)) {
        int32_t cat = ucfpos_getCategory(cfp, &err);
        int32_t fld = ucfpos_getField(cfp, &err);
        int32_t bs = 0, be = 0;
        ucfpos_getIndexes(cfp, &bs, &be, &err);
        if (bs < 0 || be > slen || bs >= be) continue;
        if (cat == UFIELD_CATEGORY_DATE_INTERVAL_SPAN) {
            /* fld is 0 for startRange, 1 for endRange. */
            for (int32_t i = bs; i < be; i++) if (cov_src[i] == -1) cov_src[i] = fld;
        } else if (cat == UFIELD_CATEGORY_DATE) {
            for (int32_t i = bs; i < be; i++) if (cov_field[i] == -1) cov_field[i] = fld;
        }
    }
    ucfpos_close(cfp);

    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    int32_t i = 0;
    while (i < slen) {
        int f = cov_field[i], s_ = cov_src[i];
        int32_t j = i + 1;
        while (j < slen && cov_field[j] == f && cov_src[j] == s_) j++;
        const char *type = (f >= 0) ? "literal" : "literal";
        /* Map UDateFormatField to ECMA-402 part type. */
        switch (f) {
            case UDAT_ERA_FIELD: type = "era"; break;
            case UDAT_YEAR_FIELD: type = "year"; break;
            case UDAT_MONTH_FIELD: type = "month"; break;
            case UDAT_DATE_FIELD: type = "day"; break;
            case UDAT_HOUR_OF_DAY1_FIELD: case UDAT_HOUR_OF_DAY0_FIELD:
            case UDAT_HOUR1_FIELD: case UDAT_HOUR0_FIELD: type = "hour"; break;
            case UDAT_MINUTE_FIELD: type = "minute"; break;
            case UDAT_SECOND_FIELD: type = "second"; break;
            case UDAT_FRACTIONAL_SECOND_FIELD: type = "fractionalSecond"; break;
            case UDAT_DAY_OF_WEEK_FIELD: case UDAT_STANDALONE_DAY_FIELD: type = "weekday"; break;
            case UDAT_AM_PM_FIELD: case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
            case UDAT_FLEXIBLE_DAY_PERIOD_FIELD: type = "dayPeriod"; break;
            case UDAT_TIMEZONE_FIELD: case UDAT_TIMEZONE_RFC_FIELD:
            case UDAT_TIMEZONE_GENERIC_FIELD: case UDAT_TIMEZONE_SPECIAL_FIELD:
                type = "timeZoneName"; break;
            default: type = "literal"; break;
        }
        duk_push_object(ctx);
        duk_push_string(ctx, type); duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, str + i, j - i); duk_put_prop_string(ctx, -2, "value");
        const char *source = "shared";
        if (s_ == 0)      source = "startRange";
        else if (s_ == 1) source = "endRange";
        duk_push_string(ctx, source); duk_put_prop_string(ctx, -2, "source");
        duk_put_prop_index(ctx, -2, outi++);
        i = j;
    }
    free(cov_field); free(cov_src);
    udtitvfmt_closeResult(fr);
    return 1;
}

static duk_ret_t dtf_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_UDAT, "DateTimeFormat");
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : uloc_getDefault();
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, K_TZ);
    const char *tz = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : NULL;
    duk_pop(ctx);
    duk_pop(ctx); /* this */

    /* Pull stored opts to echo back the user-supplied component
       options.  Spec requires a precise key order in resolvedOptions. */
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_OPTS);
    duk_idx_t opts_idx = -1;
    int has_opts = duk_is_object(ctx, -1);
    if (has_opts) opts_idx = duk_get_top_index(ctx);

    duk_push_object(ctx);
    /* ECMA-402 §11.5.5 requires keys in this exact order:
       locale, calendar, numberingSystem, timeZone, hourCycle, hour12,
       weekday, era, year, month, day, dayPeriod, hour, minute, second,
       fractionalSecondDigits, timeZoneName, dateStyle, timeStyle. */
    duk_push_string(ctx, icu_to_bcp47(loc));
    duk_put_prop_string(ctx, -2, "locale");
    {
        char calbuf[64]; UErrorCode e = U_ZERO_ERROR;
        int32_t cn = uloc_getKeywordValue(loc, "calendar", calbuf, sizeof(calbuf), &e);
        duk_push_string(ctx, (U_SUCCESS(e) && cn > 0) ? calbuf : "gregory");
        duk_put_prop_string(ctx, -2, "calendar");
    }
    {
        char nbuf[64]; UErrorCode e = U_ZERO_ERROR;
        int32_t nn = uloc_getKeywordValue(loc, "numbers", nbuf, sizeof(nbuf), &e);
        duk_push_string(ctx, (U_SUCCESS(e) && nn > 0) ? nbuf : "latn");
        duk_put_prop_string(ctx, -2, "numberingSystem");
    }
    if (tz) {
        duk_push_string(ctx, tz);
    } else {
        UErrorCode err = U_ZERO_ERROR;
        UChar tzbuf[128];
        int32_t n = ucal_getDefaultTimeZone(tzbuf, 128, &err);
        if (U_SUCCESS(err))
            uchar_push_utf8(ctx, tzbuf, n);
        else
            duk_push_string(ctx, "UTC");
    }
    duk_put_prop_string(ctx, -2, "timeZone");

    /* Component options.  Spec key order:
         hourCycle, hour12, weekday, era, year, month, day, dayPeriod,
         hour, minute, second, fractionalSecondDigits, timeZoneName,
         dateStyle, timeStyle.
       hourCycle/hour12 must be synthesized when the formatter involves
       an hour (either user-set or implied by timeStyle), even if the
       user didn't pass hour12/hourCycle directly. */
    int has_hour = 0, has_time_style = 0;
    if (has_opts) {
        duk_get_prop_string(ctx, opts_idx, "hour");      has_hour = !duk_is_undefined(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, opts_idx, "timeStyle"); has_time_style = !duk_is_undefined(ctx, -1); duk_pop(ctx);
    }
    if (has_hour || has_time_style) {
        /* hourCycle: read user's choice or derive from locale default
           (en → h12, en-GB → h23, etc.).  ICU exposes default via
           uloc_keywordValue or we can look at the pattern. */
        const char *hc = NULL;
        int hc_is_12 = 1;  /* en default */
        if (has_opts) {
            duk_get_prop_string(ctx, opts_idx, "hourCycle");
            if (duk_is_string(ctx, -1)) hc = duk_get_string(ctx, -1);
            duk_pop(ctx);
        }
        if (!hc) {
            /* Derive: en/en-US → h12; other locales vary.  Quick
               heuristic: ask ICU for the preferred. */
            char hcbuf[16]; UErrorCode e = U_ZERO_ERROR;
            int32_t hn = uloc_getKeywordValue(loc, "hours", hcbuf, sizeof(hcbuf), &e);
            if (U_SUCCESS(e) && hn > 0) hc = (strstr(hcbuf, "h11") ? "h11" :
                                              strstr(hcbuf, "h12") ? "h12" :
                                              strstr(hcbuf, "h23") ? "h23" :
                                              strstr(hcbuf, "h24") ? "h24" : "h12");
            else {
                /* Fallback by region: US → h12, otherwise → h23. */
                hc = (strstr(loc, "_US") || strstr(loc, "-US")) ? "h12" : "h23";
            }
        }
        hc_is_12 = (!strcmp(hc, "h11") || !strcmp(hc, "h12"));
        duk_push_string(ctx, hc); duk_put_prop_string(ctx, -2, "hourCycle");
        duk_push_boolean(ctx, hc_is_12); duk_put_prop_string(ctx, -2, "hour12");
    }
    if (has_opts) {
        /* String-valued component keys + the one integer key. */
        static const char *STR_KEYS[] = {
            "weekday", "era", "year", "month", "day", "dayPeriod",
            "hour", "minute", "second",
            "timeZoneName", "dateStyle", "timeStyle",
            NULL
        };
        for (int i = 0; STR_KEYS[i]; i++) {
            duk_get_prop_string(ctx, opts_idx, STR_KEYS[i]);
            if (duk_is_undefined(ctx, -1)) { duk_pop(ctx); continue; }
            /* ECMA-402 GetOption(...,"string",...) → ToString'd value.
               The user may have passed an object with toString/valueOf;
               resolvedOptions reports the coerced string, not the raw
               object. */
            duk_to_string(ctx, -1);
            duk_put_prop_string(ctx, -2, STR_KEYS[i]);
        }
        /* fractionalSecondDigits: integer in [1,3]. */
        duk_get_prop_string(ctx, opts_idx, "fractionalSecondDigits");
        if (!duk_is_undefined(ctx, -1)) {
            duk_int_t n = duk_to_int(ctx, -1);
            duk_pop(ctx);
            duk_push_int(ctx, n);
            duk_put_prop_string(ctx, -2, "fractionalSecondDigits");
        } else {
            duk_pop(ctx);
        }
    }
    return 1;
}

/* ---------------- NumberFormat ---------------- */

/* NumberFormat is backed by UNumberFormatter (ICU 60+ skeleton API)
   so we get notation/unit/signDisplay/etc. for free. Build a skeleton
   string from the ECMA-402 options, then format via unumf_formatDouble. */

static void nf_skel_append(char *skel, size_t cap, const char *s)
{
    size_t n = strlen(skel);
    if (n && n + 1 < cap) skel[n++] = ' ';
    size_t r = cap > n ? cap - n - 1 : 0;
    if (r) { strncpy(skel + n, s, r); skel[cap - 1] = 0; }
}

static duk_ret_t nf_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx)) {
        duk_idx_t n_args = duk_get_top(ctx);
        duk_push_current_function(ctx);
        duk_insert(ctx, 0);
        duk_new(ctx, (duk_idx_t)n_args);
        return 1;
    }

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    int have_opts = (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx));

    /* ECMA-402 enum validation for NumberFormat options. */
    if (have_opts) {
        static const char *STYLE_VALS[] = {"decimal","percent","currency","unit", NULL};
        static const char *NOTATION_VALS[] = {"standard","scientific","engineering","compact", NULL};
        static const char *CD_VALS[] = {"short","long", NULL};
        static const char *SIGN_VALS[] = {"auto","always","never","exceptZero","negative", NULL};
        static const char *CURD_VALS[] = {"code","symbol","narrowSymbol","name", NULL};
        static const char *CURS_VALS[] = {"standard","accounting", NULL};
        static const char *UD_VALS[] = {"short","narrow","long", NULL};
        static const char *LM_VALS[] = {"lookup","best fit", NULL};
        static const char *TZD_VALS[] = {"auto","stripIfInteger", NULL};
        static const char *RP_VALS[] = {"auto","morePrecision","lessPrecision", NULL};
        static const char *RM_VALS[] = {
            "ceil","floor","expand","trunc",
            "halfCeil","halfFloor","halfExpand","halfTrunc","halfEven", NULL
        };
        static const char *UG_STR_VALS[] = {"auto","always","min2","false","true", NULL};
        if (validate_enum_opt(ctx, opts_idx, "style",         STYLE_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "notation",      NOTATION_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "compactDisplay",CD_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "signDisplay",   SIGN_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "currencyDisplay",CURD_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "currencySign",  CURS_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "unitDisplay",   UD_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "trailingZeroDisplay", TZD_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "roundingPriority", RP_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "roundingMode",  RM_VALS, 1)) duk_pop(ctx);
        /* useGrouping: bool or string */
        duk_get_prop_string(ctx, opts_idx, "useGrouping");
        if (!duk_is_undefined(ctx, -1) && duk_is_string(ctx, -1)) {
            const char *v = duk_get_string(ctx, -1);
            int ok = 0;
            for (int i = 0; UG_STR_VALS[i]; i++) if (!strcmp(UG_STR_VALS[i], v)) { ok = 1; break; }
            if (!ok) RP_RANGE_THROW(ctx, "Invalid useGrouping: %s", v);
        }
        duk_pop(ctx);
        /* Numeric digit-count ranges */
        struct { const char *name; double lo; double hi; } digit_ranges[] = {
            {"minimumIntegerDigits", 1, 21},
            {"minimumFractionDigits", 0, 100},
            {"maximumFractionDigits", 0, 100},
            {"minimumSignificantDigits", 1, 21},
            {"maximumSignificantDigits", 1, 21},
            {NULL, 0, 0}
        };
        for (int i = 0; digit_ranges[i].name; i++) {
            duk_get_prop_string(ctx, opts_idx, digit_ranges[i].name);
            if (!duk_is_undefined(ctx, -1)) {
                double v = duk_to_number(ctx, -1);
                if (!(v >= digit_ranges[i].lo && v <= digit_ranges[i].hi) || v != (int)v)
                    RP_RANGE_THROW(ctx, "%s must be in [%g, %g]",
                                   digit_ranges[i].name, digit_ranges[i].lo, digit_ranges[i].hi);
            }
            duk_pop(ctx);
        }
        /* currency: required and 3-letter ASCII if style=currency. */
        duk_get_prop_string(ctx, opts_idx, "style");
        int is_cur = duk_is_string(ctx, -1) && !strcmp(duk_get_string(ctx, -1), "currency");
        duk_pop(ctx);
        if (is_cur) {
            duk_get_prop_string(ctx, opts_idx, "currency");
            if (duk_is_undefined(ctx, -1))
                RP_TYPE_THROW(ctx, "currency option is required when style is 'currency'");
            const char *cur = duk_to_string(ctx, -1);
            int clen = (int)strlen(cur);
            if (clen != 3) RP_RANGE_THROW(ctx, "Invalid currency: %s", cur);
            for (int i = 0; i < 3; i++) {
                char c = cur[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                    RP_RANGE_THROW(ctx, "Invalid currency: %s", cur);
            }
            /* ECMA-402 §15.5.1: currency code is upper-cased before
               resolution.  `usd` and `USD` are both valid input but
               resolvedOptions().currency must return "USD". */
            char upper[4] = {0};
            for (int i = 0; i < 3; i++) {
                char c = cur[i];
                upper[i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
            }
            duk_pop(ctx);
            duk_push_string(ctx, upper);
            duk_put_prop_string(ctx, opts_idx, "currency");
        }
        /* style=unit requires unit */
        duk_get_prop_string(ctx, opts_idx, "style");
        int is_unit = duk_is_string(ctx, -1) && !strcmp(duk_get_string(ctx, -1), "unit");
        duk_pop(ctx);
        if (is_unit) {
            duk_get_prop_string(ctx, opts_idx, "unit");
            if (duk_is_undefined(ctx, -1))
                RP_TYPE_THROW(ctx, "unit option is required when style is 'unit'");
            duk_pop(ctx);
        }
        /* `unit`, when supplied, must be a valid sanctioned identifier
           — even if style isn't "unit", per the spec.  Validate format. */
        duk_get_prop_string(ctx, opts_idx, "unit");
        if (!duk_is_undefined(ctx, -1)) {
            const char *u = duk_to_string(ctx, -1);
            if (!*u) RP_RANGE_THROW(ctx, "Invalid unit option: empty string");
            /* Accept single sanctioned ids or "A-per-B" compound form.
               Sanctioned ids are lowercase ASCII with optional hyphens
               within a single component. */
            int ok = 1;
            const char *p = u;
            int in_comp = 0;
            for (; *p; p++) {
                char c = *p;
                if ((c >= 'a' && c <= 'z')) { in_comp = 1; }
                else if (c == '-') { in_comp = 0; }
                else { ok = 0; break; }
            }
            if (!ok || !in_comp)
                RP_RANGE_THROW(ctx, "Invalid unit option: %s", u);
        }
        duk_pop(ctx);
    }

    char skel[512]; skel[0] = 0;

    /* style + currency + unit */
    const char *style = "decimal";
    if (have_opts) {
        const char *s = opt_get_string(ctx, opts_idx, "style");
        if (s) { style = s; duk_pop(ctx); }
    }
    if (!strcmp(style, "percent")) {
        nf_skel_append(skel, sizeof(skel), "percent scale/100");
    } else if (!strcmp(style, "currency")) {
        const char *cur = have_opts ? opt_get_string(ctx, opts_idx, "currency") : NULL;
        if (!cur) cur = "USD";
        char part[64];
        snprintf(part, sizeof(part), "currency/%s", cur);
        nf_skel_append(skel, sizeof(skel), part);
        if (have_opts && cur) duk_pop(ctx);
    } else if (!strcmp(style, "unit")) {
        const char *un = have_opts ? opt_get_string(ctx, opts_idx, "unit") : NULL;
        if (un) {
            char part[96];
            /* ECMA-402 allows compound "per" units like "meter-per-second";
               skeleton uses "measure-unit/<u> per-measure-unit/<u>". */
            const char *per = strstr(un, "-per-");
            if (per) {
                char numu[48], denu[48];
                size_t nl = (size_t)(per - un);
                if (nl >= sizeof(numu)) nl = sizeof(numu) - 1;
                memcpy(numu, un, nl); numu[nl] = 0;
                strncpy(denu, per + 5, sizeof(denu) - 1); denu[sizeof(denu)-1] = 0;
                snprintf(part, sizeof(part), "measure-unit/length-%s per-measure-unit/duration-%s", numu, denu);
            } else {
                /* ICU skeleton wants typed unit ids like "length-meter".
                   Map the bare ECMA name to a plausible type prefix. */
                static const struct { const char *name; const char *typed; } map[] = {
                    {"acre","area-acre"},{"hectare","area-hectare"},
                    {"bit","digital-bit"},{"byte","digital-byte"},
                    {"kilobit","digital-kilobit"},{"kilobyte","digital-kilobyte"},
                    {"megabit","digital-megabit"},{"megabyte","digital-megabyte"},
                    {"gigabit","digital-gigabit"},{"gigabyte","digital-gigabyte"},
                    {"terabit","digital-terabit"},{"terabyte","digital-terabyte"},
                    {"petabyte","digital-petabyte"},
                    {"celsius","temperature-celsius"},{"fahrenheit","temperature-fahrenheit"},
                    {"centimeter","length-centimeter"},{"millimeter","length-millimeter"},
                    {"meter","length-meter"},{"kilometer","length-kilometer"},
                    {"inch","length-inch"},{"foot","length-foot"},
                    {"yard","length-yard"},{"mile","length-mile"},
                    {"mile-scandinavian","length-mile-scandinavian"},
                    {"liter","volume-liter"},{"milliliter","volume-milliliter"},
                    {"gallon","volume-gallon"},{"fluid-ounce","volume-fluid-ounce"},
                    {"gram","mass-gram"},{"kilogram","mass-kilogram"},
                    {"ounce","mass-ounce"},{"pound","mass-pound"},{"stone","mass-stone"},
                    {"degree","angle-degree"},
                    {"nanosecond","duration-nanosecond"},{"microsecond","duration-microsecond"},
                    {"millisecond","duration-millisecond"},{"second","duration-second"},
                    {"minute","duration-minute"},{"hour","duration-hour"},
                    {"day","duration-day"},{"week","duration-week"},
                    {"month","duration-month"},{"year","duration-year"},
                    {"percent","concentr-percent"},
                    {NULL,NULL}
                };
                const char *typed = un;
                for (int i = 0; map[i].name; i++)
                    if (!strcmp(map[i].name, un)) { typed = map[i].typed; break; }
                snprintf(part, sizeof(part), "measure-unit/%s", typed);
            }
            nf_skel_append(skel, sizeof(skel), part);
            duk_pop(ctx);

            const char *ud = opt_get_string(ctx, opts_idx, "unitDisplay");
            if (ud) {
                if      (!strcmp(ud, "short"))  nf_skel_append(skel, sizeof(skel), "unit-width-short");
                else if (!strcmp(ud, "narrow")) nf_skel_append(skel, sizeof(skel), "unit-width-narrow");
                else                            nf_skel_append(skel, sizeof(skel), "unit-width-full-name");
                duk_pop(ctx);
            }
        }
    }

    /* notation */
    int is_compact = 0;
    if (have_opts) {
        const char *nt = opt_get_string(ctx, opts_idx, "notation");
        if (nt) {
            if      (!strcmp(nt, "scientific"))  nf_skel_append(skel, sizeof(skel), "scientific");
            else if (!strcmp(nt, "engineering")) nf_skel_append(skel, sizeof(skel), "engineering");
            else if (!strcmp(nt, "compact")) {
                const char *cd = opt_get_string(ctx, opts_idx, "compactDisplay");
                nf_skel_append(skel, sizeof(skel),
                               (cd && !strcmp(cd, "long")) ? "compact-long" : "compact-short");
                if (cd) duk_pop(ctx);
                is_compact = 1;
            }
            duk_pop(ctx);
        }
    }

    /* signDisplay + currencySign */
    if (have_opts) {
        const char *sd = opt_get_string(ctx, opts_idx, "signDisplay");
        const char *cs = opt_get_string(ctx, opts_idx, "currencySign");
        int accounting = (cs && !strcmp(cs, "accounting"));
        if (cs) duk_pop(ctx);

        if (sd) {
            if      (!strcmp(sd, "auto"))         nf_skel_append(skel, sizeof(skel), accounting ? "sign-accounting" : "sign-auto");
            else if (!strcmp(sd, "always"))       nf_skel_append(skel, sizeof(skel), accounting ? "sign-accounting-always" : "sign-always");
            else if (!strcmp(sd, "exceptZero"))   nf_skel_append(skel, sizeof(skel), accounting ? "sign-accounting-except-zero" : "sign-except-zero");
            else if (!strcmp(sd, "negative"))     nf_skel_append(skel, sizeof(skel), accounting ? "sign-accounting-negative" : "sign-negative");
            else if (!strcmp(sd, "never"))        nf_skel_append(skel, sizeof(skel), "sign-never");
            duk_pop(ctx);
        } else if (accounting) {
            nf_skel_append(skel, sizeof(skel), "sign-accounting");
        }
    }

    /* currencyDisplay (only meaningful for currency style) */
    if (have_opts && !strcmp(style, "currency")) {
        const char *cd = opt_get_string(ctx, opts_idx, "currencyDisplay");
        if (cd) {
            if      (!strcmp(cd, "code"))         nf_skel_append(skel, sizeof(skel), "unit-width-iso-code");
            else if (!strcmp(cd, "narrowSymbol")) nf_skel_append(skel, sizeof(skel), "unit-width-narrow");
            else if (!strcmp(cd, "name"))         nf_skel_append(skel, sizeof(skel), "unit-width-full-name");
            else                                  nf_skel_append(skel, sizeof(skel), "unit-width-short");
            duk_pop(ctx);
        }
    }

    /* useGrouping */
    if (have_opts) {
        duk_get_prop_string(ctx, opts_idx, "useGrouping");
        if (duk_is_boolean(ctx, -1)) {
            nf_skel_append(skel, sizeof(skel),
                           duk_get_boolean(ctx, -1) ? "group-auto" : "group-off");
        } else if (duk_is_string(ctx, -1)) {
            const char *ug = duk_get_string(ctx, -1);
            if      (!strcmp(ug, "auto"))    nf_skel_append(skel, sizeof(skel), "group-auto");
            else if (!strcmp(ug, "always"))  nf_skel_append(skel, sizeof(skel), "group-on-aligned");
            else if (!strcmp(ug, "min2"))    nf_skel_append(skel, sizeof(skel), "group-min2");
            else                             nf_skel_append(skel, sizeof(skel), "group-off");
        }
        duk_pop(ctx);
    }

    /* precision: significant digits take priority over fraction digits
       per ECMA-402 default behavior.  Style determines defaults when
       no precision is given: decimal=>0..3 fractions, percent=>0..0,
       currency=>uses currency's natural digit count (handled by ICU
       skeleton when no .X is supplied — but we want to match node's
       rounded-percent default). */
    int have_sig = 0;
    int min_sig = 1, max_sig = 21;
    if (have_opts) {
        duk_get_prop_string(ctx, opts_idx, "minimumSignificantDigits");
        if (duk_is_number(ctx, -1)) { min_sig = (int)duk_get_int(ctx, -1); have_sig = 1; }
        duk_pop(ctx);
        duk_get_prop_string(ctx, opts_idx, "maximumSignificantDigits");
        if (duk_is_number(ctx, -1)) { max_sig = (int)duk_get_int(ctx, -1); have_sig = 1; }
        duk_pop(ctx);
    }
    if (have_sig) {
        /* Build "@@..##.." with min '@' followed by (max-min) '#' for the
           ICU significant-digits skeleton. */
        char buf[64]; size_t bi = 0;
        for (int k = 0; k < min_sig && bi < sizeof(buf)-1; k++) buf[bi++] = '@';
        for (int k = 0; k < max_sig - min_sig && bi < sizeof(buf)-1; k++) buf[bi++] = '#';
        buf[bi] = 0;
        nf_skel_append(skel, sizeof(skel), buf);
    } else if (have_opts) {
        int have_frac = 0;
        int min_frac = -1, max_frac = -1;
        duk_get_prop_string(ctx, opts_idx, "minimumFractionDigits");
        if (duk_is_number(ctx, -1)) { min_frac = (int)duk_get_int(ctx, -1); have_frac = 1; }
        duk_pop(ctx);
        duk_get_prop_string(ctx, opts_idx, "maximumFractionDigits");
        if (duk_is_number(ctx, -1)) { max_frac = (int)duk_get_int(ctx, -1); have_frac = 1; }
        duk_pop(ctx);
        if (have_frac) {
            if (min_frac < 0) min_frac = 0;
            if (max_frac < 0) max_frac = min_frac > 3 ? min_frac : 3;
            char buf[80]; size_t bi = 0;
            buf[bi++] = '.';
            for (int k = 0; k < min_frac && bi < sizeof(buf)-1; k++) buf[bi++] = '0';
            for (int k = 0; k < max_frac - min_frac && bi < sizeof(buf)-1; k++) buf[bi++] = '#';
            buf[bi] = 0;
            nf_skel_append(skel, sizeof(skel), buf);
        }
        duk_get_prop_string(ctx, opts_idx, "minimumIntegerDigits");
        if (duk_is_number(ctx, -1)) {
            int mi = (int)duk_get_int(ctx, -1);
            char buf[24];
            snprintf(buf, sizeof(buf), "integer-width/*%.*s",
                     mi, "0000000000000000");
            /* the *N notation in skeletons is "at least N integer digits" — use "+0…" */
            char buf2[32];
            int n2 = 0;
            buf2[n2++] = '+';
            for (int k = 0; k < mi && n2 < (int)sizeof(buf2) - 1; k++) buf2[n2++] = '0';
            buf2[n2] = 0;
            snprintf(buf, sizeof(buf), "integer-width/%s", buf2);
            nf_skel_append(skel, sizeof(skel), buf);
        }
        duk_pop(ctx);
    }

    /* roundingMode */
    if (have_opts) {
        const char *rm = opt_get_string(ctx, opts_idx, "roundingMode");
        if (rm) {
            static const struct { const char *e; const char *s; } map[] = {
                {"ceil","rounding-mode-ceiling"}, {"floor","rounding-mode-floor"},
                {"expand","rounding-mode-up"},    {"trunc","rounding-mode-down"},
                {"halfCeil","rounding-mode-half-ceiling"},
                {"halfFloor","rounding-mode-half-floor"},
                {"halfExpand","rounding-mode-half-up"},
                {"halfTrunc","rounding-mode-half-down"},
                {"halfEven","rounding-mode-half-even"},
                {NULL,NULL}
            };
            for (int i = 0; map[i].e; i++)
                if (!strcmp(map[i].e, rm)) { nf_skel_append(skel, sizeof(skel), map[i].s); break; }
            duk_pop(ctx);
        }
    }

    /* If roundingIncrement is supplied, the precision-increment
       skeleton stem replaces any `.XX` fraction-digit hint we already
       appended (ICU rejects the combination as a syntax error).
       Strip any existing dot-precision before appending. */
    /* roundingIncrement: round to nearest multiple of N at the
       maximumFractionDigits scale.  Spec-valid values: 1, 2, 5, 10,
       20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000.  Skip
       1 (the identity).  ICU skeleton: `precision-increment/<value>`
       with `value` = increment * 10^-mxfd. */
    if (have_opts) {
        duk_get_prop_string(ctx, opts_idx, "roundingIncrement");
        if (!duk_is_undefined(ctx, -1)) {
            double rinc = duk_to_number(ctx, -1);
            int valid[] = {1,2,5,10,20,25,50,100,200,250,500,1000,2000,2500,5000,0};
            int ok = 0;
            for (int k = 0; valid[k]; k++) if (rinc == valid[k]) { ok = 1; break; }
            if (!ok || rinc != (int)rinc)
                RP_RANGE_THROW(ctx, "Invalid roundingIncrement: %g", rinc);
            if ((int)rinc != 1) {
                /* Strip existing `.XX` precision stem from skeleton. */
                char *dot = strstr(skel, " .");
                if (dot) {
                    char *space = strchr(dot + 1, ' ');
                    if (space) memmove(dot, space, strlen(space) + 1);
                    else *dot = 0;
                } else if (skel[0] == '.') {
                    char *space = strchr(skel, ' ');
                    if (space) memmove(skel, space + 1, strlen(space + 1) + 1);
                    else skel[0] = 0;
                }
                /* Re-read mxfd if user supplied; default mxfd=3 for
                   decimal, =0 for percent. */
                int mxfd_eff = 3;
                if (!strcmp(style, "percent")) mxfd_eff = 0;
                duk_get_prop_string(ctx, opts_idx, "maximumFractionDigits");
                if (duk_is_number(ctx, -1)) mxfd_eff = (int)duk_get_int(ctx, -1);
                duk_pop(ctx);
                /* Build value = rinc * 10^-mxfd_eff as a fixed-point
                   decimal string ("50" + mxfd=2 → "0.50"). */
                char buf[64];
                int n = snprintf(buf, sizeof(buf), "%d", (int)rinc);
                /* Insert decimal point mxfd_eff digits from the right. */
                if (mxfd_eff <= 0) {
                    /* integer increment: 50 → "50" */
                    snprintf(buf, sizeof(buf), "precision-increment/%d", (int)rinc);
                } else if (n <= mxfd_eff) {
                    char pad[32]; int p = 0;
                    pad[p++] = '0'; pad[p++] = '.';
                    for (int k = 0; k < mxfd_eff - n; k++) pad[p++] = '0';
                    pad[p] = 0;
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "precision-increment/%s%d", pad, (int)rinc);
                    strncpy(buf, tmp, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
                } else {
                    /* digits longer than fraction part: insert dot. */
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "%.*s.%s",
                             n - mxfd_eff, buf, buf + (n - mxfd_eff));
                    char prefix[64];
                    snprintf(prefix, sizeof(prefix), "precision-increment/%s", tmp);
                    strncpy(buf, prefix, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
                }
                nf_skel_append(skel, sizeof(skel), buf);
            }
        }
        duk_pop(ctx);
    }

    /* numberingSystem — non-empty BCP47 type per spec. */
    if (have_opts) {
        duk_get_prop_string(ctx, opts_idx, "numberingSystem");
        if (!duk_is_undefined(ctx, -1)) {
            const char *ns = duk_to_string(ctx, -1);
            if (!*ns)
                RP_RANGE_THROW(ctx, "Invalid numberingSystem option: empty string");
            /* Strict-ish: numbering system tags are [a-z0-9]+ 3-8 chars. */
            size_t nl = strlen(ns);
            if (nl < 3 || nl > 8)
                RP_RANGE_THROW(ctx, "Invalid numberingSystem option: %s", ns);
            for (size_t i = 0; i < nl; i++) {
                char c = ns[i];
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
                    RP_RANGE_THROW(ctx, "Invalid numberingSystem option: %s", ns);
            }
            char part[48];
            snprintf(part, sizeof(part), "numbering-system/%s", ns);
            nf_skel_append(skel, sizeof(skel), part);
        }
        duk_pop(ctx);
    }

    /* If no precision was specified, apply ECMA-402 defaults that
       vary by notation:
       - compact: locale's natural compact pattern (don't override —
         ICU/CLDR knows the right precision per scale per locale)
       - percent: precision-integer
       - currency: ICU's currency-default precision (don't override)
       - decimal/unit/scientific/engineering: mnfd=0, mxfd=3 (".###")
       Without these, ICU keeps every fractional digit of the input. */
    if (!have_sig && !strstr(skel, ".") && !strstr(skel, "precision-")
                  && !strstr(skel, "@") && !is_compact) {
        if (!strcmp(style, "percent"))
            nf_skel_append(skel, sizeof(skel), "precision-integer");
        else if (strcmp(style, "currency") != 0)
            nf_skel_append(skel, sizeof(skel), ".###");
    }

    /* Convert ASCII skeleton -> UChar and open the formatter. */
    int32_t slen = (int32_t)strlen(skel);
    UChar *uskel = (UChar *)malloc(sizeof(UChar) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) uskel[i] = (UChar)skel[i];
    uskel[slen] = 0;

    UErrorCode err = U_ZERO_ERROR;
    UNumberFormatter *unf = unumf_openForSkeletonAndLocale(uskel, slen, locale, &err);
    free(uskel);
    if (U_FAILURE(err) || !unf)
        RP_THROW(ctx, "Intl.NumberFormat: skeleton '%s': %s", skel, u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UNumberFormatter *));
    *(UNumberFormatter **)buf = unf;
    duk_put_prop_string(ctx, -2, K_UNUM);
    duk_push_string(ctx, locale); duk_put_prop_string(ctx, -2, K_LOC);
    /* Stash the skeleton — formatRange opens a separate
       UNumberRangeFormatter lazily and reuses it. */
    duk_push_string(ctx, skel);
    duk_put_prop_string(ctx, -2, K_SKEL);
    /* Stash the original opts object for resolvedOptions read-back. */
    if (have_opts) {
        duk_dup(ctx, opts_idx);
        duk_put_prop_string(ctx, -2, K_OPTS);
    }
    duk_push_string(ctx, style); duk_put_prop_string(ctx, -2, K_TYPE);
    return 0;
}

static duk_ret_t nf_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_UNUM);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UNumberFormatter *)) {
            UNumberFormatter *u = *(UNumberFormatter **)p;
            if (u) unumf_close(u);
            *(UNumberFormatter **)p = NULL;
        }
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "\xff" "unrf");
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UNumberRangeFormatter *)) {
            UNumberRangeFormatter *r = *(UNumberRangeFormatter **)p;
            if (r) unumrf_close(r);
            *(UNumberRangeFormatter **)p = NULL;
        }
    }
    return 0;
}

static duk_ret_t nf_format(duk_context *ctx)
{
    double val = duk_to_number(ctx, 0);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UNUM);
    duk_size_t n = 0;
    void *p = duk_get_buffer_data(ctx, -1, &n);
    duk_pop_2(ctx);
    if (!p || n != sizeof(UNumberFormatter *))
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.format: invalid this");
    UNumberFormatter *unf = *(UNumberFormatter **)p;
    if (!unf) RP_THROW(ctx, "Intl.NumberFormat: handle closed");

    UErrorCode err = U_ZERO_ERROR;
    UFormattedNumber *fr = unumf_openResult(&err);
    if (U_FAILURE(err) || !fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    unumf_formatDouble(unf, val, fr, &err);
    if (U_FAILURE(err)) { unumf_closeResult(fr); RP_THROW(ctx, "formatDouble: %s", u_errorName(err)); }

    UChar out[256];
    int32_t out_len = unumf_resultToString(fr, out, 256, &err);
    if (err == U_BUFFER_OVERFLOW_ERROR) {
        err = U_ZERO_ERROR;
        UChar *big = (UChar *)malloc(sizeof(UChar) * (size_t)(out_len + 1));
        out_len = unumf_resultToString(fr, big, out_len + 1, &err);
        if (U_SUCCESS(err)) uchar_push_utf8(ctx, big, out_len);
        else                duk_push_string(ctx, "");
        free(big);
    } else if (U_SUCCESS(err)) {
        uchar_push_utf8(ctx, out, out_len);
    } else {
        duk_push_string(ctx, "");
    }
    unumf_closeResult(fr);
    return 1;
}

/* Map a UNumberFormatFields code to its ECMA-402 part type string.
   `sign_neg` distinguishes minusSign from plusSign / exponentMinus vs
   exponentPlus by looking at the actual character. */
static const char *nf_field_type(int field, int is_minus)
{
    switch (field) {
        case UNUM_INTEGER_FIELD:             return "integer";
        case UNUM_FRACTION_FIELD:            return "fraction";
        case UNUM_DECIMAL_SEPARATOR_FIELD:   return "decimal";
        case UNUM_GROUPING_SEPARATOR_FIELD:  return "group";
        case UNUM_CURRENCY_FIELD:            return "currency";
        case UNUM_PERCENT_FIELD:             return "percentSign";
        case UNUM_SIGN_FIELD:                return is_minus ? "minusSign" : "plusSign";
        case UNUM_EXPONENT_SYMBOL_FIELD:     return "exponentSeparator";
        case UNUM_EXPONENT_SIGN_FIELD:       return is_minus ? "exponentMinusSign" : "exponentPlusSign";
        case UNUM_EXPONENT_FIELD:            return "exponentInteger";
        case UNUM_MEASURE_UNIT_FIELD:        return "unit";
        case UNUM_COMPACT_FIELD:             return "compact";
        case UNUM_PERMILL_FIELD:             return "permille";
        case UNUM_APPROXIMATELY_SIGN_FIELD:  return "approximatelySign";
        default:                             return "literal";
    }
}

static duk_ret_t nf_format_to_parts(duk_context *ctx)
{
    double val = duk_to_number(ctx, 0);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UNUM);
    duk_size_t n = 0;
    void *p = duk_get_buffer_data(ctx, -1, &n);
    duk_pop_2(ctx);
    if (!p || n != sizeof(UNumberFormatter *))
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.formatToParts: invalid this");
    UNumberFormatter *unf = *(UNumberFormatter **)p;
    if (!unf) RP_THROW(ctx, "Intl.NumberFormat: handle closed");

    UErrorCode err = U_ZERO_ERROR;
    UFormattedNumber *fr = unumf_openResult(&err);
    if (U_FAILURE(err) || !fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    unumf_formatDouble(unf, val, fr, &err);
    if (U_FAILURE(err)) { unumf_closeResult(fr); RP_THROW(ctx, "formatDouble: %s", u_errorName(err)); }

    /* Get the full string + a per-UChar coverage map. */
    UChar buf[256];
    int32_t blen = unumf_resultToString(fr, buf, 256, &err);
    UChar *out = buf;
    UChar *heap = NULL;
    if (err == U_BUFFER_OVERFLOW_ERROR) {
        err = U_ZERO_ERROR;
        heap = (UChar *)malloc(sizeof(UChar) * (size_t)(blen + 1));
        blen = unumf_resultToString(fr, heap, blen + 1, &err);
        out = heap;
    }
    if (U_FAILURE(err)) {
        if (heap) free(heap);
        unumf_closeResult(fr);
        RP_THROW(ctx, "resultToString: %s", u_errorName(err));
    }

    /* coverage[i] = ICU field code, or -1 for literal.  Iterate using
       ufmtval_nextPosition which yields ALL non-overlapping fields
       in the NUMBER category — unumf_resultNextFieldPosition only
       iterates occurrences of a single field type. */
    int *cov = (int *)malloc(sizeof(int) * (size_t)(blen + 1));
    for (int32_t i = 0; i < blen; i++) cov[i] = -1;
    UErrorCode e2 = U_ZERO_ERROR;
    const UFormattedValue *fv = unumf_resultAsValue(fr, &e2);
    if (U_SUCCESS(e2) && fv) {
        UConstrainedFieldPosition *cfp = ucfpos_open(&e2);
        if (U_SUCCESS(e2) && cfp) {
            ucfpos_constrainCategory(cfp, UFIELD_CATEGORY_NUMBER, &e2);
            while (ufmtval_nextPosition(fv, cfp, &e2) && U_SUCCESS(e2)) {
                int32_t bs2 = 0, be2 = 0;
                ucfpos_getIndexes(cfp, &bs2, &be2, &e2);
                int32_t fld = ucfpos_getField(cfp, &e2);
                if (bs2 < 0 || be2 > blen || bs2 >= be2) continue;
                /* First-wins: ICU yields specific fields (grouping
                   separator) before broader ones (integer) that
                   overlap, so we want the first occupant. */
                for (int32_t i = bs2; i < be2; i++)
                    if (cov[i] == -1) cov[i] = fld;
            }
            ucfpos_close(cfp);
        }
    }

    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    int32_t i = 0;
    while (i < blen) {
        int f = cov[i];
        int32_t j = i + 1;
        while (j < blen && cov[j] == f) j++;
        /* Sign disambiguation by looking at the actual char. */
        int is_minus = 0;
        if (f == UNUM_SIGN_FIELD || f == UNUM_EXPONENT_SIGN_FIELD) {
            UChar c = out[i];
            is_minus = (c == '-' || c == 0x2212);
        }
        const char *type = nf_field_type(f, is_minus);
        /* ICU doesn't surface dedicated UNUM_INFINITY_FIELD / NAN
           through ufmtval_nextPosition — the symbols show up tagged
           as integer.  Detect by content. */
        if (f == UNUM_INTEGER_FIELD && j - i >= 1) {
            UChar c0 = out[i];
            if (c0 == 0x221E)               /* ∞ */ type = "infinity";
            else if (j - i == 3
                     && out[i]   == 'N'
                     && out[i+1] == 'a'
                     && out[i+2] == 'N')    type = "nan";
        }
        duk_push_object(ctx);
        duk_push_string(ctx, type);
        duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, out + i, j - i);
        duk_put_prop_string(ctx, -2, "value");
        duk_put_prop_index(ctx, -2, outi++);
        i = j;
    }

    free(cov);
    if (heap) free(heap);
    unumf_closeResult(fr);
    return 1;
}

/* Lazy/cached UNumberRangeFormatter on the instance.  Built from the
   skeleton saved at construct time + locale. */
static UNumberRangeFormatter *nf_get_range_fmt(duk_context *ctx)
{
    duk_get_prop_string(ctx, -1, "\xff" "unrf");
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        duk_pop(ctx);
        if (p && n == sizeof(UNumberRangeFormatter *)) {
            UNumberRangeFormatter *r = *(UNumberRangeFormatter **)p;
            if (r) return r;
        }
    } else {
        duk_pop(ctx);
    }
    duk_get_prop_string(ctx, -1, K_SKEL);
    const char *skel = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : "";
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : uloc_getDefault();
    duk_pop(ctx);

    int32_t slen = (int32_t)strlen(skel);
    UChar *uskel = (UChar *)malloc(sizeof(UChar) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) uskel[i] = (UChar)skel[i];
    uskel[slen] = 0;

    UErrorCode err = U_ZERO_ERROR;
    UNumberRangeFormatter *r = unumrf_openForSkeletonWithCollapseAndIdentityFallback(
        uskel, slen, UNUM_RANGE_COLLAPSE_AUTO,
        UNUM_IDENTITY_FALLBACK_APPROXIMATELY, loc, NULL, &err);
    free(uskel);
    if (U_FAILURE(err) || !r)
        RP_THROW(ctx, "Intl.NumberFormat.formatRange: open failed: %s", u_errorName(err));
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UNumberRangeFormatter *));
    *(UNumberRangeFormatter **)buf = r;
    duk_put_prop_string(ctx, -2, "\xff" "unrf");
    return r;
}

static double nf_arg_to_number(duk_context *ctx, duk_idx_t idx, const char *which)
{
    double v = duk_to_number(ctx, idx);
    if (v != v) RP_RANGE_THROW(ctx, "Intl.NumberFormat.formatRange: %s is NaN", which);
    return v;
}

static duk_ret_t nf_format_range(duk_context *ctx)
{
    if (duk_get_top(ctx) < 2)
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.formatRange requires 2 arguments");
    if (duk_is_undefined(ctx, 0) || duk_is_undefined(ctx, 1))
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.formatRange: undefined argument");
    double a = nf_arg_to_number(ctx, 0, "start");
    double b = nf_arg_to_number(ctx, 1, "end");

    duk_push_this(ctx);
    require_internal_slot(ctx, K_UNUM, "NumberFormat");
    UNumberRangeFormatter *fmt = nf_get_range_fmt(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedNumberRange *fr = unumrf_openResult(&err);
    if (!fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    unumrf_formatDoubleRange(fmt, a, b, fr, &err);
    if (U_FAILURE(err)) { unumrf_closeResult(fr); RP_THROW(ctx, "formatDoubleRange: %s", u_errorName(err)); }

    const UFormattedValue *fv = unumrf_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *s = ufmtval_getString(fv, &slen, &err);
    if (U_SUCCESS(err) && s) uchar_push_utf8(ctx, s, slen);
    else                     duk_push_string(ctx, "");
    unumrf_closeResult(fr);
    return 1;
}

static duk_ret_t nf_format_range_to_parts(duk_context *ctx)
{
    if (duk_get_top(ctx) < 2)
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.formatRangeToParts requires 2 arguments");
    if (duk_is_undefined(ctx, 0) || duk_is_undefined(ctx, 1))
        RP_TYPE_THROW(ctx, "Intl.NumberFormat.formatRangeToParts: undefined argument");
    double a = nf_arg_to_number(ctx, 0, "start");
    double b = nf_arg_to_number(ctx, 1, "end");

    duk_push_this(ctx);
    require_internal_slot(ctx, K_UNUM, "NumberFormat");
    UNumberRangeFormatter *fmt = nf_get_range_fmt(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedNumberRange *fr = unumrf_openResult(&err);
    if (!fr) RP_THROW(ctx, "openResult: %s", u_errorName(err));
    unumrf_formatDoubleRange(fmt, a, b, fr, &err);
    if (U_FAILURE(err)) { unumrf_closeResult(fr); RP_THROW(ctx, "formatDoubleRange: %s", u_errorName(err)); }

    const UFormattedValue *fv = unumrf_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *str = ufmtval_getString(fv, &slen, &err);

    int *cov_field = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    int *cov_src   = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) { cov_field[i] = -1; cov_src[i] = -1; }

    UConstrainedFieldPosition *cfp = ucfpos_open(&err);
    while (ufmtval_nextPosition(fv, cfp, &err) && U_SUCCESS(err)) {
        int32_t cat = ucfpos_getCategory(cfp, &err);
        int32_t fld = ucfpos_getField(cfp, &err);
        int32_t bs = 0, be = 0;
        ucfpos_getIndexes(cfp, &bs, &be, &err);
        if (bs < 0 || be > slen || bs >= be) continue;
        if (cat == UFIELD_CATEGORY_NUMBER_RANGE_SPAN) {
            for (int32_t i = bs; i < be; i++) if (cov_src[i] == -1) cov_src[i] = fld;
        } else if (cat == UFIELD_CATEGORY_NUMBER) {
            for (int32_t i = bs; i < be; i++) if (cov_field[i] == -1) cov_field[i] = fld;
        }
    }
    ucfpos_close(cfp);

    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    int32_t i = 0;
    while (i < slen) {
        int f = cov_field[i], s_ = cov_src[i];
        int32_t j = i + 1;
        while (j < slen && cov_field[j] == f && cov_src[j] == s_) j++;
        UChar c0 = str[i];
        int is_minus = (c0 == '-' || c0 == 0x2212);
        const char *type = nf_field_type(f, is_minus);
        duk_push_object(ctx);
        duk_push_string(ctx, type); duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, str + i, j - i); duk_put_prop_string(ctx, -2, "value");
        const char *source = "shared";
        if (s_ == 0)      source = "startRange";
        else if (s_ == 1) source = "endRange";
        duk_push_string(ctx, source); duk_put_prop_string(ctx, -2, "source");
        duk_put_prop_index(ctx, -2, outi++);
        i = j;
    }
    free(cov_field); free(cov_src);
    unumrf_closeResult(fr);
    return 1;
}

static duk_ret_t nf_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_UNUM, "NumberFormat");
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : uloc_getDefault();
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE);
    const char *style = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : "decimal";
    duk_pop(ctx);

    /* ECMA-402 §15.5.5 mandates this key order:
       locale, numberingSystem, style, [currency*]/[unit*],
       minimumIntegerDigits, minimumFractionDigits,
       maximumFractionDigits, minimumSignificantDigits,
       maximumSignificantDigits, useGrouping, notation,
       compactDisplay, signDisplay, roundingMode, roundingIncrement,
       roundingPriority, trailingZeroDisplay. */
    /* Stack layout: [..., this, result_obj, opts(maybe undefined)]. */
    duk_push_object(ctx);
    /* idx of result_obj on stack — captured before we push opts. */
    duk_idx_t res = duk_get_top_index(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc));
    duk_put_prop_string(ctx, res, "locale");

    duk_get_prop_string(ctx, -2, K_OPTS);
    duk_idx_t opts = duk_get_top_index(ctx);

    /* numberingSystem: from opts if present, else "latn". */
    if (duk_is_object(ctx, opts)) {
        duk_get_prop_string(ctx, opts, "numberingSystem");
        if (!duk_is_undefined(ctx, -1)) duk_put_prop_string(ctx, res, "numberingSystem");
        else {
            duk_pop(ctx);
            duk_push_string(ctx, "latn");
            duk_put_prop_string(ctx, res, "numberingSystem");
        }
    } else {
        duk_push_string(ctx, "latn");
        duk_put_prop_string(ctx, res, "numberingSystem");
    }

    duk_push_string(ctx, style);
    duk_put_prop_string(ctx, res, "style");

    /* Echo back option keys in spec order, ToString- or ToInteger-
       coerced per ECMA-402 GetOption semantics — user might pass an
       object with custom toString/valueOf. */
    if (duk_is_object(ctx, opts)) {
        static const char *STR_KEYS[] = {
            "currency","currencyDisplay","currencySign","unit","unitDisplay",
            "notation","compactDisplay","signDisplay",
            NULL
        };
        static const char *INT_KEYS[] = {
            "minimumIntegerDigits","minimumFractionDigits","maximumFractionDigits",
            "minimumSignificantDigits","maximumSignificantDigits",
            NULL
        };
        for (int i = 0; STR_KEYS[i]; i++) {
            duk_get_prop_string(ctx, opts, STR_KEYS[i]);
            if (!duk_is_undefined(ctx, -1)) {
                duk_to_string(ctx, -1);
                duk_put_prop_string(ctx, res, STR_KEYS[i]);
            } else duk_pop(ctx);
        }
        for (int i = 0; INT_KEYS[i]; i++) {
            duk_get_prop_string(ctx, opts, INT_KEYS[i]);
            if (!duk_is_undefined(ctx, -1)) {
                duk_int_t n = duk_to_int(ctx, -1);
                duk_pop(ctx);
                duk_push_int(ctx, n);
                duk_put_prop_string(ctx, res, INT_KEYS[i]);
            } else duk_pop(ctx);
        }
        /* useGrouping: bool or string. */
        duk_get_prop_string(ctx, opts, "useGrouping");
        if (!duk_is_undefined(ctx, -1))
            duk_put_prop_string(ctx, res, "useGrouping");
        else duk_pop(ctx);
    }

    /* Spec defaults that must always be present in resolvedOptions: */
    static const struct { const char *key; const char *val; } DEFAULTS[] = {
        {"useGrouping",         "auto"},
        {"notation",            "standard"},
        {"signDisplay",         "auto"},
        {"roundingMode",        "halfExpand"},
        {"roundingPriority",    "auto"},
        {"trailingZeroDisplay", "auto"},
        {NULL, NULL}
    };
    static const struct { const char *key; int val; } DEF_INTS[] = {
        {"roundingIncrement", 1},
        {NULL, 0}
    };
    for (int i = 0; DEFAULTS[i].key; i++) {
        duk_get_prop_string(ctx, res, DEFAULTS[i].key);
        int present = !duk_is_undefined(ctx, -1);
        duk_pop(ctx);
        if (!present) {
            duk_push_string(ctx, DEFAULTS[i].val);
            duk_put_prop_string(ctx, res, DEFAULTS[i].key);
        }
    }
    for (int i = 0; DEF_INTS[i].key; i++) {
        duk_get_prop_string(ctx, res, DEF_INTS[i].key);
        int present = !duk_is_undefined(ctx, -1);
        duk_pop(ctx);
        if (!present) {
            duk_push_int(ctx, DEF_INTS[i].val);
            duk_put_prop_string(ctx, res, DEF_INTS[i].key);
        }
    }

    duk_pop(ctx);  /* opts */
    return 1;
}

/* ---------------- Collator ---------------- */

static duk_ret_t col_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx)) {
        duk_idx_t n_args = duk_get_top(ctx);
        duk_push_current_function(ctx);
        duk_insert(ctx, 0);
        duk_new(ctx, (duk_idx_t)n_args);
        return 1;
    }

    const char *locale_in = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale_in = duk_get_string(ctx, 0);
    if (locale_in && *locale_in) validate_locale_tag(ctx, locale_in);
    if (!locale_in || !*locale_in) locale_in = uloc_getDefault();

    /* Convert BCP 47 → ICU locale id so Unicode extension keywords
       like `-u-kn-true`, `-u-co-phonebk` reach ucol_open as
       `colnumeric=yes`, `collation=phonebook`, etc.  Also: usage=search
       uses a different collation tailoring; bake the
       collation=search keyword into the locale before ucol_open. */
    char locale_buf[256];
    {
        UErrorCode e2 = U_ZERO_ERROR;
        int32_t n = uloc_forLanguageTag(locale_in, locale_buf,
                                        sizeof(locale_buf), NULL, &e2);
        if (U_FAILURE(e2) || n <= 0) {
            strncpy(locale_buf, locale_in, sizeof(locale_buf) - 1);
            locale_buf[sizeof(locale_buf) - 1] = 0;
        }
    }
    duk_idx_t opts_idx_pre = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    const char *usage = "sort";
    if (opts_idx_pre != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx_pre)) {
        /* ECMA-402 enum validation for Collator options. */
        static const char *USAGE_VALS[] = {"sort","search", NULL};
        static const char *LM_VALS[] = {"lookup","best fit", NULL};
        static const char *SENS_VALS[] = {"base","accent","case","variant", NULL};
        static const char *CF_VALS[] = {"upper","lower","false", NULL};
        if (validate_enum_opt(ctx, opts_idx_pre, "usage",         USAGE_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx_pre, "localeMatcher", LM_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx_pre, "sensitivity",   SENS_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx_pre, "caseFirst",     CF_VALS, 1)) duk_pop(ctx);
        /* collation: non-empty if present (validated as a BCP47 type) */
        duk_get_prop_string(ctx, opts_idx_pre, "collation");
        if (!duk_is_undefined(ctx, -1)) {
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid collation option: empty string");
        }
        duk_pop(ctx);

        const char *u = opt_get_string(ctx, opts_idx_pre, "usage");
        if (u) {
            usage = u;
            if (!strcmp(u, "search")) {
                UErrorCode e2 = U_ZERO_ERROR;
                uloc_setKeywordValue("collation", "search",
                                     locale_buf, sizeof(locale_buf), &e2);
            }
            duk_pop(ctx);
        }
        const char *col = opt_get_string(ctx, opts_idx_pre, "collation");
        if (col) {
            UErrorCode e2 = U_ZERO_ERROR;
            uloc_setKeywordValue("collation", col,
                                 locale_buf, sizeof(locale_buf), &e2);
            duk_pop(ctx);
        }
    }
    const char *locale = locale_buf;

    UErrorCode err = U_ZERO_ERROR;
    UCollator *uc = ucol_open(locale, &err);
    if (U_FAILURE(err) || !uc)
        RP_THROW(ctx, "Intl.Collator: ucol_open: %s", u_errorName(err));
    /* ECMA-402 §11.1 requires Intl.Collator to treat canonically
       equivalent strings as equal (e.g. precomposed "ä" and "a"+
       combining diaeresis).  ICU's collator defaults to no-norm on
       most locales — turn it on globally. */
    {
        UErrorCode e2 = U_ZERO_ERROR;
        ucol_setAttribute(uc, UCOL_NORMALIZATION_MODE, UCOL_ON, &e2);
    }
    (void)usage;

    /* ECMA-402 sensitivity → ICU strength.
         base      → primary    (a == ä == A)
         accent    → secondary  (a == A, a != ä)
         case      → primary + caseLevel on (a != A, a == ä)
         variant   → tertiary   (default; all distinct)
       Plus `ignorePunctuation` (alternate=shifted) and
       `caseFirst` ("upper"|"lower"|"false"). */
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx))
    {
        const char *sens = opt_get_string(ctx, opts_idx, "sensitivity");
        if (sens) {
            UErrorCode e2 = U_ZERO_ERROR;
            if (strcmp(sens, "base") == 0) {
                ucol_setStrength(uc, UCOL_PRIMARY);
            } else if (strcmp(sens, "accent") == 0) {
                ucol_setStrength(uc, UCOL_SECONDARY);
            } else if (strcmp(sens, "case") == 0) {
                ucol_setStrength(uc, UCOL_PRIMARY);
                ucol_setAttribute(uc, UCOL_CASE_LEVEL, UCOL_ON, &e2);
            } else if (strcmp(sens, "variant") == 0) {
                ucol_setStrength(uc, UCOL_TERTIARY);
            }
            duk_pop(ctx);
        }
        /* `ignorePunctuation`: ToBoolean per spec — any truthy/falsy
           value accepted, not just booleans. */
        duk_get_prop_string(ctx, opts_idx, "ignorePunctuation");
        if (!duk_is_undefined(ctx, -1) && duk_to_boolean(ctx, -1)) {
            UErrorCode e2 = U_ZERO_ERROR;
            ucol_setAttribute(uc, UCOL_ALTERNATE_HANDLING, UCOL_SHIFTED, &e2);
        }
        duk_pop(ctx);
        const char *cf = opt_get_string(ctx, opts_idx, "caseFirst");
        if (cf) {
            UErrorCode e2 = U_ZERO_ERROR;
            if (strcmp(cf, "upper") == 0)
                ucol_setAttribute(uc, UCOL_CASE_FIRST, UCOL_UPPER_FIRST, &e2);
            else if (strcmp(cf, "lower") == 0)
                ucol_setAttribute(uc, UCOL_CASE_FIRST, UCOL_LOWER_FIRST, &e2);
            else if (strcmp(cf, "false") == 0)
                ucol_setAttribute(uc, UCOL_CASE_FIRST, UCOL_OFF, &e2);
            duk_pop(ctx);
        }
        /* `numeric`: ToBoolean per spec. */
        duk_get_prop_string(ctx, opts_idx, "numeric");
        if (!duk_is_undefined(ctx, -1) && duk_to_boolean(ctx, -1)) {
            UErrorCode e2 = U_ZERO_ERROR;
            ucol_setAttribute(uc, UCOL_NUMERIC_COLLATION, UCOL_ON, &e2);
        }
        duk_pop(ctx);
    }

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UCollator *));
    *(UCollator **)buf = uc;
    duk_put_prop_string(ctx, -2, K_UCOL);
    duk_push_string(ctx, locale);
    duk_put_prop_string(ctx, -2, K_LOC);
    return 0;
}

static duk_ret_t col_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_UCOL);
    if (duk_is_buffer_data(ctx, -1))
    {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UCollator *)) {
            UCollator *u = *(UCollator **)p;
            if (u) ucol_close(u);
            *(UCollator **)p = NULL;
        }
    }
    return 0;
}

static duk_ret_t col_compare(duk_context *ctx)
{
    const char *a = duk_to_string(ctx, 0);
    const char *b = duk_to_string(ctx, 1);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UCOL);
    duk_size_t n = 0;
    void *p = duk_get_buffer_data(ctx, -1, &n);
    duk_pop_2(ctx);
    if (!p || n != sizeof(UCollator *))
        RP_THROW(ctx, "Intl.Collator.compare: invoked on non-Collator");
    UCollator *uc = *(UCollator **)p;
    if (!uc)
        RP_THROW(ctx, "Intl.Collator: handle closed");

    int32_t a_len = 0, b_len = 0;
    UChar *au = utf8_to_uchar(a, (int32_t)strlen(a), &a_len);
    UChar *bu = utf8_to_uchar(b, (int32_t)strlen(b), &b_len);
    UCollationResult r = ucol_strcoll(uc, au, a_len, bu, b_len);
    free(au); free(bu);
    duk_push_int(ctx, r == UCOL_LESS ? -1 : (r == UCOL_GREATER ? 1 : 0));
    return 1;
}

static duk_ret_t col_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_UCOL, "Collator");
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : uloc_getDefault();
    /* Keep `this` on the stack — we read K_UCOL from it below. */
    duk_pop(ctx);

    /* If the user specified collation=search via opts.usage, it shows
       up as the "collation" keyword in our stored locale. Pull it out
       to surface as usage/collation correctly. */
    char colval[64] = "default";
    UErrorCode e = U_ZERO_ERROR;
    int32_t cl = uloc_getKeywordValue(loc, "collation", colval, sizeof(colval), &e);
    const char *usage = "sort";
    if (U_SUCCESS(e) && cl > 0 && !strcmp(colval, "search")) usage = "search";

    /* sensitivity + caseFirst + numeric + ignorePunctuation: read
       from the actual ucol_* settings on the open Collator handle so
       the surface matches both user options and locale defaults. */
    UCollator *uc = NULL;
    duk_get_prop_string(ctx, -1, K_UCOL);
    duk_size_t ucbsz = 0;
    void *ucp = duk_get_buffer_data(ctx, -1, &ucbsz);
    if (ucp && ucbsz == sizeof(UCollator *)) uc = *(UCollator **)ucp;
    duk_pop_2(ctx); /* pop K_UCOL value + `this` */

    const char *sensitivity = "variant";
    const char *caseFirst = "false";
    int numeric = 0;
    int ignorePunct = 0;
    if (uc) {
        UErrorCode e2 = U_ZERO_ERROR;
        int strength = ucol_getAttribute(uc, UCOL_STRENGTH, &e2);
        int caseLevel = ucol_getAttribute(uc, UCOL_CASE_LEVEL, &e2);
        if (strength == UCOL_PRIMARY) {
            sensitivity = (caseLevel == UCOL_ON) ? "case" : "base";
        } else if (strength == UCOL_SECONDARY) {
            sensitivity = "accent";
        } else {
            sensitivity = "variant";
        }
        int cf = ucol_getAttribute(uc, UCOL_CASE_FIRST, &e2);
        if      (cf == UCOL_UPPER_FIRST) caseFirst = "upper";
        else if (cf == UCOL_LOWER_FIRST) caseFirst = "lower";
        else                             caseFirst = "false";
        numeric = (ucol_getAttribute(uc, UCOL_NUMERIC_COLLATION, &e2) == UCOL_ON);
        ignorePunct = (ucol_getAttribute(uc, UCOL_ALTERNATE_HANDLING, &e2) == UCOL_SHIFTED);
    }

    /* ECMA-402 §10.5.3 dictates this exact key order:
       locale, usage, sensitivity, ignorePunctuation, collation,
       numeric, caseFirst. */
    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc));  duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, usage);              duk_put_prop_string(ctx, -2, "usage");
    duk_push_string(ctx, sensitivity);        duk_put_prop_string(ctx, -2, "sensitivity");
    duk_push_boolean(ctx, ignorePunct);       duk_put_prop_string(ctx, -2, "ignorePunctuation");
    duk_push_string(ctx, (cl > 0) ? colval : "default");
    duk_put_prop_string(ctx, -2, "collation");
    duk_push_boolean(ctx, numeric);           duk_put_prop_string(ctx, -2, "numeric");
    duk_push_string(ctx, caseFirst);          duk_put_prop_string(ctx, -2, "caseFirst");
    return 1;
}

/* ---------------- top-level Intl functions ---------------- */

/* Structural BCP 47 validation: rejects empty, double-hyphens, illegal
   chars, subtags out of length range, invalid primary language, and
   singleton extensions without a value.  Lenient inside -x- private
   use sections (subtags can be 1-8). */
static int is_structurally_valid_language_tag(const char *t)
{
    if (!t || !*t) return 0;
    size_t n = strlen(t);
    if (n > 100) return 0;
    if (t[0] == '-' || t[n-1] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
           || (c >= '0' && c <= '9') || c == '-'))
            return 0;
        if (c == '-' && i + 1 < n && t[i+1] == '-') return 0;
    }
    /* Walk subtags; track context (primary lang, post-singleton, in -x-). */
    const char *p = t;
    int seg_idx = 0;
    int in_priv = 0;       /* inside -x- private use */
    char last_singleton = 0;
    int singleton_subtag_count = 0;
    while (*p) {
        const char *seg = p;
        while (*p && *p != '-') p++;
        size_t sl = (size_t)(p - seg);
        if (sl < 1 || sl > 8) return 0;

        if (sl == 1 && !in_priv) {
            /* New singleton extension marker. */
            if (seg[0] == 'x' || seg[0] == 'X') in_priv = 1;
            if (last_singleton && singleton_subtag_count == 0) return 0;  /* previous singleton empty */
            last_singleton = seg[0];
            singleton_subtag_count = 0;
        } else if (last_singleton && !in_priv) {
            /* Value subtag of a singleton extension: 2-8 chars. */
            if (sl < 2) return 0;
            singleton_subtag_count++;
        } else if (in_priv) {
            /* Private use: 1-8 alnum allowed; no further constraint. */
        } else if (seg_idx == 0) {
            /* Primary language: 2-3 alpha or 5-8 alpha. */
            if (sl == 4) return 0;
            for (size_t k = 0; k < sl; k++) {
                char c = seg[k];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                    return 0;
            }
        }
        seg_idx++;
        if (*p == '-') p++;
    }
    /* Trailing singleton with no value. */
    if (last_singleton && singleton_subtag_count == 0 && !in_priv) return 0;
    return 1;
}

/* Push one canonicalized tag onto out_arr (at the given absolute
   stack index), if not already present.  Throws RangeError on
   structurally invalid tags. */
static void push_canonical_unique(duk_context *ctx, const char *tag,
                                  duk_idx_t out_arr, duk_uarridx_t *out_idx)
{
    if (!is_structurally_valid_language_tag(tag))
        RP_RANGE_THROW(ctx, "Incorrect locale information provided: %s", tag);
    char icu[256], canon[256];
    UErrorCode err = U_ZERO_ERROR;
    uloc_forLanguageTag(tag, icu, sizeof(icu), NULL, &err);
    if (U_FAILURE(err) || !*icu)
        RP_RANGE_THROW(ctx, "Incorrect locale information provided: %s", tag);
    err = U_ZERO_ERROR;
    int32_t n = uloc_toLanguageTag(icu, canon, sizeof(canon), 0, &err);
    if (U_FAILURE(err) || n <= 0)
        RP_RANGE_THROW(ctx, "Incorrect locale information provided: %s", tag);
    canon[n] = 0;
    apply_cldr_aliases(canon, sizeof(canon));
    /* Dedupe. */
    duk_size_t cur = duk_get_length(ctx, out_arr);
    for (duk_size_t i = 0; i < cur; i++) {
        duk_get_prop_index(ctx, out_arr, (duk_uarridx_t)i);
        const char *prev = duk_get_string(ctx, -1);
        int same = (prev && !strcmp(prev, canon));
        duk_pop(ctx);
        if (same) return;
    }
    duk_push_string(ctx, canon);
    duk_put_prop_index(ctx, out_arr, (*out_idx)++);
}

static duk_ret_t intl_get_canonical_locales(duk_context *ctx)
{
    duk_push_array(ctx);
    duk_idx_t out_arr = duk_get_top_index(ctx);
    duk_uarridx_t out_idx = 0;

    /* Accept undefined (-> []). */
    if (duk_get_top(ctx) - 1 == 0 || duk_is_undefined(ctx, 0))
        return 1;

    /* String → single-element list. */
    if (duk_is_string(ctx, 0))
    {
        push_canonical_unique(ctx, duk_get_string(ctx, 0), out_arr, &out_idx);
        return 1;
    }

    /* ToObject coercion: numbers, booleans, symbols get boxed; null
       would normally TypeError but per spec only `undefined` short-
       circuits, and we already handled that. */
    if (duk_is_null(ctx, 0))
        RP_TYPE_THROW(ctx, "Cannot convert null to object");
    if (!duk_is_object(ctx, 0)) {
        duk_to_object(ctx, 0);
    }

    /* Array-like: ToLength(length), iterate by integer index, ToString. */
    duk_get_prop_string(ctx, 0, "length");
    double lenf = duk_to_number(ctx, -1);
    duk_pop(ctx);
    if (lenf != lenf || lenf < 0) lenf = 0;
    if (lenf > 4294967295.0) lenf = 4294967295.0;
    duk_size_t len = (duk_size_t)lenf;
    for (duk_size_t i = 0; i < len; i++) {
        /* HasProperty + Get to support sparse arrays. */
        duk_push_uint(ctx, (duk_uint_t)i);
        if (!duk_has_prop(ctx, 0)) continue;
        duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
        /* Type check per spec: must be String or Object. */
        if (!duk_is_string(ctx, -1) && !duk_is_object(ctx, -1)) {
            duk_pop(ctx);
            RP_TYPE_THROW(ctx, "Locale element must be a string or Intl.Locale");
        }
        /* If it's an Intl.Locale instance, read its [[Locale]] slot. */
        const char *tag = NULL;
        if (duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, K_LOC)) {
            duk_get_prop_string(ctx, -1, K_LOC);
            tag = duk_get_string(ctx, -1);
            push_canonical_unique(ctx, tag, out_arr, &out_idx);
            duk_pop_2(ctx);
            continue;
        }
        tag = duk_to_string(ctx, -1);
        push_canonical_unique(ctx, tag, out_arr, &out_idx);
        duk_pop(ctx);
    }
    return 1;
}

static duk_ret_t intl_supported_values_of(duk_context *ctx)
{
    const char *key = duk_to_string(ctx, 0);
    UErrorCode err = U_ZERO_ERROR;
    duk_push_array(ctx);
    duk_uarridx_t idx = 0;

    if (strcmp(key, "timeZone") == 0)
    {
        UEnumeration *e = ucal_openTimeZones(&err);
        if (U_FAILURE(err) || !e) return 1;
        const char *id;
        int32_t id_len;
        while ((id = uenum_next(e, &id_len, &err)) != NULL && U_SUCCESS(err)) {
            duk_push_lstring(ctx, id, id_len);
            duk_put_prop_index(ctx, -2, idx++);
        }
        uenum_close(e);
    }
    else if (strcmp(key, "calendar") == 0)
    {
        /* All calendar types known to ICU.  Passing a real locale
           (not NULL) is required by ICU for the lookup to succeed. */
        UEnumeration *e = ucal_getKeywordValuesForLocale("calendar", "und", 0, &err);
        if (U_FAILURE(err) || !e) return 1;
        const char *id; int32_t id_len;
        while ((id = uenum_next(e, &id_len, &err)) != NULL && U_SUCCESS(err)) {
            duk_push_lstring(ctx, id, id_len);
            duk_put_prop_index(ctx, -2, idx++);
        }
        uenum_close(e);
    }
    else if (strcmp(key, "collation") == 0)
    {
        UEnumeration *e = ucol_getKeywordValues("collation", &err);
        if (U_FAILURE(err) || !e) return 1;
        const char *id; int32_t id_len;
        while ((id = uenum_next(e, &id_len, &err)) != NULL && U_SUCCESS(err)) {
            /* Spec excludes "standard" and "search" — they're per-locale
               algorithms, not selectable values. */
            if (id_len == 8 && !memcmp(id, "standard", 8)) continue;
            if (id_len == 6 && !memcmp(id, "search",   6)) continue;
            duk_push_lstring(ctx, id, id_len);
            duk_put_prop_index(ctx, -2, idx++);
        }
        uenum_close(e);
    }
    else if (strcmp(key, "currency") == 0)
    {
        UEnumeration *e = ucurr_openISOCurrencies(UCURR_ALL, &err);
        if (U_FAILURE(err) || !e) return 1;
        const char *id; int32_t id_len;
        while ((id = uenum_next(e, &id_len, &err)) != NULL && U_SUCCESS(err)) {
            duk_push_lstring(ctx, id, id_len);
            duk_put_prop_index(ctx, -2, idx++);
        }
        uenum_close(e);
    }
    else if (strcmp(key, "numberingSystem") == 0)
    {
        /* No direct enumeration in ICU public API — return a curated
           list matching node's Intl-output here. */
        static const char *ns[] = {
            "adlm","ahom","arab","arabext","bali","beng","bhks","brah","cakm","cham",
            "deva","diak","fullwide","gong","gonm","gujr","guru","hanidec","hmng",
            "hmnp","java","kali","khmr","knda","lana","lanatham","laoo","latn","lepc",
            "limb","mathbold","mathdbl","mathmono","mathsanb","mathsans","mlym","modi",
            "mong","mroo","mtei","mymr","mymrshan","mymrtlng","newa","nkoo","olck",
            "orya","osma","rohg","saur","segment","shrd","sind","sinh","sora","sund",
            "takr","talu","tamldec","telu","thai","tibt","tirh","vaii","wara","wcho",
            NULL
        };
        for (int i = 0; ns[i]; i++) {
            duk_push_string(ctx, ns[i]);
            duk_put_prop_index(ctx, -2, idx++);
        }
    }
    else if (strcmp(key, "unit") == 0)
    {
        /* ECMA-402 sanctioned simple units; ICU MeasureUnit covers
           many more but spec only requires this fixed set. */
        static const char *units[] = {
            "acre","bit","byte","celsius","centimeter","day","degree","fahrenheit",
            "fluid-ounce","foot","gallon","gigabit","gigabyte","gram","hectare","hour",
            "inch","kilobit","kilobyte","kilogram","kilometer","liter","megabit",
            "megabyte","meter","microsecond","mile","mile-scandinavian","millimeter",
            "milliliter","millisecond","minute","month","nanosecond","ounce","percent",
            "petabyte","pound","second","stone","terabit","terabyte","week","yard","year",
            NULL
        };
        for (int i = 0; units[i]; i++) {
            duk_push_string(ctx, units[i]);
            duk_put_prop_index(ctx, -2, idx++);
        }
    }
    return 1;
}

/* ================================================================
 * PluralRules
 * ============================================================== */

static duk_ret_t plr_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.PluralRules: must be called with new");

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    const char *type = "cardinal";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        static const char *TYPE_VALS[] = {"cardinal","ordinal", NULL};
        static const char *LM_VALS[]   = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS,   1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "type",          TYPE_VALS, 1)) duk_pop(ctx);
        const char *t = opt_get_string(ctx, opts_idx, "type");
        if (t) { type = t; duk_pop(ctx); }
    }

    UPluralType utype = (strcmp(type, "ordinal") == 0) ? UPLURAL_TYPE_ORDINAL : UPLURAL_TYPE_CARDINAL;

    UErrorCode err = U_ZERO_ERROR;
    UPluralRules *pr = uplrules_openForType(locale, utype, &err);
    if (U_FAILURE(err) || !pr)
        RP_THROW(ctx, "Intl.PluralRules: open failed: %s", u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UPluralRules *));
    *(UPluralRules **)buf = pr;
    duk_put_prop_string(ctx, -2, K_UPRL);
    duk_push_string(ctx, locale); duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, utype == UPLURAL_TYPE_ORDINAL ? "ordinal" : "cardinal");
    duk_put_prop_string(ctx, -2, K_TYPE);
    return 0;
}

static duk_ret_t plr_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_UPRL);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UPluralRules *)) {
            UPluralRules *pr = *(UPluralRules **)p;
            if (pr) uplrules_close(pr);
            *(UPluralRules **)p = NULL;
        }
    }
    return 0;
}

static duk_ret_t plr_select(duk_context *ctx)
{
    double n = duk_to_number(ctx, 0);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UPRL);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(UPluralRules *))
        RP_TYPE_THROW(ctx, "Intl.PluralRules: select: invalid this");
    UPluralRules *pr = *(UPluralRules **)p;
    duk_pop_2(ctx);

    UChar out[64]; UErrorCode err = U_ZERO_ERROR;
    int32_t olen = uplrules_select(pr, n, out, 64, &err);
    if (U_FAILURE(err))
        RP_THROW(ctx, "Intl.PluralRules: select: %s", u_errorName(err));
    uchar_push_utf8(ctx, out, olen);
    return 1;
}

static duk_ret_t plr_select_range(duk_context *ctx)
{
    double a = duk_to_number(ctx, 0);
    double b = duk_to_number(ctx, 1);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UPRL);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(UPluralRules *))
        RP_TYPE_THROW(ctx, "Intl.PluralRules: selectRange: invalid this");
    UPluralRules *pr = *(UPluralRules **)p;
    (void)pr;
    duk_pop_2(ctx);
    /* ICU has no direct selectRange on UPluralRules; spec says use
       end's category as a reasonable approximation when ranges
       collapse. Fall back to selecting the end value. */
    UChar out[64]; UErrorCode err = U_ZERO_ERROR;
    int32_t olen = uplrules_select(pr, b, out, 64, &err);
    if (U_FAILURE(err))
        RP_THROW(ctx, "Intl.PluralRules: selectRange: %s", u_errorName(err));
    (void)a;
    uchar_push_utf8(ctx, out, olen);
    return 1;
}

static duk_ret_t plr_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_UPRL, "PluralRules");
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE);
    const char *type = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_UPRL);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    UPluralRules *pr = (p && bsz == sizeof(UPluralRules *)) ? *(UPluralRules **)p : NULL;
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc));  duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, type); duk_put_prop_string(ctx, -2, "type");

    /* pluralCategories array via uplrules_getKeywords */
    duk_push_array(ctx);
    duk_uarridx_t idx = 0;
    if (pr) {
        UErrorCode err = U_ZERO_ERROR;
        UEnumeration *e = uplrules_getKeywords(pr, &err);
        if (U_SUCCESS(err) && e) {
            const char *kw; int32_t klen;
            while ((kw = uenum_next(e, &klen, &err)) != NULL && U_SUCCESS(err)) {
                duk_push_lstring(ctx, kw, (duk_size_t)klen);
                duk_put_prop_index(ctx, -2, idx++);
            }
            uenum_close(e);
        }
    }
    duk_put_prop_string(ctx, -2, "pluralCategories");

    duk_push_int(ctx, 1); duk_put_prop_string(ctx, -2, "minimumIntegerDigits");
    duk_push_int(ctx, 0); duk_put_prop_string(ctx, -2, "minimumFractionDigits");
    duk_push_int(ctx, 3); duk_put_prop_string(ctx, -2, "maximumFractionDigits");

    return 1;
}

/* ================================================================
 * RelativeTimeFormat
 * ============================================================== */

static URelativeDateTimeUnit rt_unit_from_str(const char *s)
{
    if (!s) return UDAT_REL_UNIT_SECOND;
    if (!strcmp(s,"year")    || !strcmp(s,"years"))    return UDAT_REL_UNIT_YEAR;
    if (!strcmp(s,"quarter") || !strcmp(s,"quarters")) return UDAT_REL_UNIT_QUARTER;
    if (!strcmp(s,"month")   || !strcmp(s,"months"))   return UDAT_REL_UNIT_MONTH;
    if (!strcmp(s,"week")    || !strcmp(s,"weeks"))    return UDAT_REL_UNIT_WEEK;
    if (!strcmp(s,"day")     || !strcmp(s,"days"))     return UDAT_REL_UNIT_DAY;
    if (!strcmp(s,"hour")    || !strcmp(s,"hours"))    return UDAT_REL_UNIT_HOUR;
    if (!strcmp(s,"minute")  || !strcmp(s,"minutes"))  return UDAT_REL_UNIT_MINUTE;
    return UDAT_REL_UNIT_SECOND;
}

static UDateRelativeDateTimeFormatterStyle rt_style_from_str(const char *s)
{
    if (!s) return UDAT_STYLE_LONG;
    if (!strcmp(s, "short"))  return UDAT_STYLE_SHORT;
    if (!strcmp(s, "narrow")) return UDAT_STYLE_NARROW;
    return UDAT_STYLE_LONG;
}

static duk_ret_t rtf_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.RelativeTimeFormat: must be called with new");

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    const char *style_s = "long";
    const char *numeric_s = "always";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        static const char *STYLE_VALS[] = {"long","short","narrow", NULL};
        static const char *NUM_VALS[]   = {"always","auto", NULL};
        static const char *LM_VALS[]    = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS,    1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "numeric",       NUM_VALS,   1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "style",         STYLE_VALS, 1)) duk_pop(ctx);
        const char *s = opt_get_string(ctx, opts_idx, "style");
        if (s) { style_s = s; duk_pop(ctx); }
        const char *n = opt_get_string(ctx, opts_idx, "numeric");
        if (n) { numeric_s = n; duk_pop(ctx); }
    }

    UErrorCode err = U_ZERO_ERROR;
    URelativeDateTimeFormatter *rtf =
        ureldatefmt_open(locale, NULL, rt_style_from_str(style_s),
                         UDISPCTX_CAPITALIZATION_NONE, &err);
    if (U_FAILURE(err) || !rtf)
        RP_THROW(ctx, "Intl.RelativeTimeFormat: open: %s", u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(URelativeDateTimeFormatter *));
    *(URelativeDateTimeFormatter **)buf = rtf;
    duk_put_prop_string(ctx, -2, K_URLT);
    duk_push_string(ctx, locale);    duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, style_s);   duk_put_prop_string(ctx, -2, K_STYLE);
    duk_push_string(ctx, numeric_s); duk_put_prop_string(ctx, -2, K_TYPE);
    return 0;
}

static duk_ret_t rtf_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_URLT);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(URelativeDateTimeFormatter *)) {
            URelativeDateTimeFormatter *r = *(URelativeDateTimeFormatter **)p;
            if (r) ureldatefmt_close(r);
            *(URelativeDateTimeFormatter **)p = NULL;
        }
    }
    return 0;
}

/* Pull this->rtf and this->numeric from the prototype `this`. */
static URelativeDateTimeFormatter *rtf_self(duk_context *ctx, int *numeric_auto_out)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_URLT);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(URelativeDateTimeFormatter *))
        RP_TYPE_THROW(ctx, "Intl.RelativeTimeFormat: invalid this");
    URelativeDateTimeFormatter *r = *(URelativeDateTimeFormatter **)p;
    duk_pop(ctx);
    if (numeric_auto_out) {
        duk_get_prop_string(ctx, -1, K_TYPE);
        const char *n = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : "always";
        *numeric_auto_out = (strcmp(n, "auto") == 0) ? 1 : 0;
        duk_pop(ctx);
    }
    duk_pop(ctx);
    return r;
}

static duk_ret_t rtf_format(duk_context *ctx)
{
    double v = duk_to_number(ctx, 0);
    const char *u = duk_to_string(ctx, 1);
    int auto_num = 0;
    URelativeDateTimeFormatter *r = rtf_self(ctx, &auto_num);
    URelativeDateTimeUnit unit = rt_unit_from_str(u);

    UChar out[256]; UErrorCode err = U_ZERO_ERROR;
    int32_t olen;
    if (auto_num)
        olen = ureldatefmt_format(r, v, unit, out, 256, &err);
    else
        olen = ureldatefmt_formatNumeric(r, v, unit, out, 256, &err);
    if (U_FAILURE(err))
        RP_THROW(ctx, "Intl.RelativeTimeFormat: format: %s", u_errorName(err));
    uchar_push_utf8(ctx, out, olen);
    return 1;
}

static duk_ret_t rtf_format_to_parts(duk_context *ctx)
{
    double v = duk_to_number(ctx, 0);
    const char *u = duk_to_string(ctx, 1);
    int auto_num = 0;
    URelativeDateTimeFormatter *r = rtf_self(ctx, &auto_num);
    URelativeDateTimeUnit unit = rt_unit_from_str(u);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedRelativeDateTime *fr = ureldatefmt_openResult(&err);
    if (!fr) RP_THROW(ctx, "ureldatefmt_openResult: %s", u_errorName(err));
    if (auto_num) ureldatefmt_formatToResult(r, v, unit, fr, &err);
    else          ureldatefmt_formatNumericToResult(r, v, unit, fr, &err);
    if (U_FAILURE(err)) {
        ureldatefmt_closeResult(fr);
        RP_THROW(ctx, "Intl.RelativeTimeFormat: formatToParts: %s", u_errorName(err));
    }

    const UFormattedValue *fv = ureldatefmt_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *str = ufmtval_getString(fv, &slen, &err);

    /* Coverage map: -1 = literal, 0 = numeric (digits + group seps),
       1+ = number-category subfield.  ICU yields RELATIVE_DATETIME
       (LITERAL/NUMERIC) and nested NUMBER fields for the numeric
       portion.  ECMA-402 wants `integer`/`group`/etc. for the digits
       and `literal` for the surrounding text — when present, prefer
       the inner NUMBER field. */
    int *cov_kind  = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    int *cov_nfld  = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) { cov_kind[i] = -1; cov_nfld[i] = -1; }

    UConstrainedFieldPosition *cfp = ucfpos_open(&err);
    while (ufmtval_nextPosition(fv, cfp, &err) && U_SUCCESS(err)) {
        int32_t cat = ucfpos_getCategory(cfp, &err);
        int32_t fld = ucfpos_getField(cfp, &err);
        int32_t bs = 0, be = 0;
        ucfpos_getIndexes(cfp, &bs, &be, &err);
        if (bs < 0 || be > slen || bs >= be) continue;
        if (cat == UFIELD_CATEGORY_RELATIVE_DATETIME) {
            for (int32_t i = bs; i < be; i++) if (cov_kind[i] == -1) cov_kind[i] = fld;
        } else if (cat == UFIELD_CATEGORY_NUMBER) {
            for (int32_t i = bs; i < be; i++) if (cov_nfld[i] == -1) cov_nfld[i] = fld;
        }
    }
    ucfpos_close(cfp);

    /* Collapse to ECMA-402 part shape: any non-NUMERIC byte is
       "literal"; consecutive literal bytes coalesce into one part. */
    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    int32_t i = 0;
    while (i < slen) {
        int kind_i = cov_kind[i];
        int is_num_i = (kind_i == UDAT_REL_NUMERIC_FIELD);
        int32_t j = i + 1;
        if (is_num_i) {
            int nfld = cov_nfld[i];
            while (j < slen
                   && cov_kind[j] == UDAT_REL_NUMERIC_FIELD
                   && cov_nfld[j] == nfld) j++;
            const char *type = (nfld >= 0) ? nf_field_type(nfld, 0) : "integer";
            duk_push_object(ctx);
            duk_push_string(ctx, type);            duk_put_prop_string(ctx, -2, "type");
            uchar_push_utf8(ctx, str + i, j - i);  duk_put_prop_string(ctx, -2, "value");
            duk_push_string(ctx, u);               duk_put_prop_string(ctx, -2, "unit");
            duk_put_prop_index(ctx, -2, outi++);
        } else {
            /* Consume the whole literal run regardless of its
               internal RELATIVE_DATETIME subfield. */
            while (j < slen && cov_kind[j] != UDAT_REL_NUMERIC_FIELD) j++;
            duk_push_object(ctx);
            duk_push_string(ctx, "literal");       duk_put_prop_string(ctx, -2, "type");
            uchar_push_utf8(ctx, str + i, j - i);  duk_put_prop_string(ctx, -2, "value");
            duk_put_prop_index(ctx, -2, outi++);
        }
        i = j;
    }

    free(cov_kind); free(cov_nfld);
    ureldatefmt_closeResult(fr);
    return 1;
}

static duk_ret_t rtf_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_URLT, "RelativeTimeFormat");
    duk_get_prop_string(ctx, -1, K_LOC);   const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_STYLE); const char *st  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE);  const char *nu  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc)); duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, st);  duk_put_prop_string(ctx, -2, "style");
    duk_push_string(ctx, nu);  duk_put_prop_string(ctx, -2, "numeric");
    duk_push_string(ctx, "latn"); duk_put_prop_string(ctx, -2, "numberingSystem");
    return 1;
}

/* ================================================================
 * ListFormat
 * ============================================================== */

static __attribute__((unused)) UListFormatterType
lf_type_from_str(const char *s)
{
    if (!s) return ULISTFMT_TYPE_AND;
    if (!strcmp(s, "or"))    return ULISTFMT_TYPE_OR;
    if (!strcmp(s, "unit"))  return ULISTFMT_TYPE_UNITS;
    return ULISTFMT_TYPE_AND;
}

static UListFormatterWidth lf_style_from_str(const char *s)
{
    if (!s) return ULISTFMT_WIDTH_WIDE;
    if (!strcmp(s, "short"))  return ULISTFMT_WIDTH_SHORT;
    if (!strcmp(s, "narrow")) return ULISTFMT_WIDTH_NARROW;
    return ULISTFMT_WIDTH_WIDE;
}

static duk_ret_t lf_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.ListFormat: must be called with new");

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    const char *type_s = "conjunction", *style_s = "long";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        /* Spec order: localeMatcher → type → style (matters for option
           accessors / toString side-effects). */
        static const char *TYPE_VALS[]  = {"conjunction","disjunction","unit", NULL};
        static const char *STYLE_VALS[] = {"long","short","narrow", NULL};
        static const char *LM_VALS[]    = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS,    1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "type",          TYPE_VALS,  1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "style",         STYLE_VALS, 1)) duk_pop(ctx);
        const char *t = opt_get_string(ctx, opts_idx, "type");
        if (t) { type_s = t; duk_pop(ctx); }
        const char *s = opt_get_string(ctx, opts_idx, "style");
        if (s) { style_s = s; duk_pop(ctx); }
    }

    /* ECMA-402 type values are "conjunction"|"disjunction"|"unit",
       ICU types are AND/OR/UNITS — map. */
    UListFormatterType ut;
    if      (!strcmp(type_s, "disjunction")) ut = ULISTFMT_TYPE_OR;
    else if (!strcmp(type_s, "unit"))        ut = ULISTFMT_TYPE_UNITS;
    else                                     ut = ULISTFMT_TYPE_AND;
    UListFormatterWidth uw = lf_style_from_str(style_s);

    UErrorCode err = U_ZERO_ERROR;
    UListFormatter *lf = ulistfmt_openForType(locale, ut, uw, &err);
    if (U_FAILURE(err) || !lf)
        RP_THROW(ctx, "Intl.ListFormat: open: %s", u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UListFormatter *));
    *(UListFormatter **)buf = lf;
    duk_put_prop_string(ctx, -2, K_ULST);
    duk_push_string(ctx, locale);  duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, type_s);  duk_put_prop_string(ctx, -2, K_TYPE);
    duk_push_string(ctx, style_s); duk_put_prop_string(ctx, -2, K_STYLE);
    return 0;
}

static duk_ret_t lf_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_ULST);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UListFormatter *)) {
            UListFormatter *l = *(UListFormatter **)p;
            if (l) ulistfmt_close(l);
            *(UListFormatter **)p = NULL;
        }
    }
    return 0;
}

/* Convert array-of-strings on stack idx 0 to parallel UChar* + length
   arrays. Returns count; caller must free each entry of `strs`. */
static int32_t lf_marshal_array(duk_context *ctx, duk_idx_t arr_idx,
                                UChar ***strs_out, int32_t **lens_out)
{
    /* ECMA-402 StringListFromIterable: iterate step-by-step, reject
       on first non-string with TypeError, never call iterator past
       the failure point.  We can't just `Array.from` because that
       exhausts the iterator before checking types. */
    duk_size_t cap = 8;
    UChar **strs = (UChar **)calloc(cap + 1, sizeof(UChar *));
    int32_t *lens = (int32_t *)calloc(cap + 1, sizeof(int32_t));
    if (!strs || !lens) { free(strs); free(lens); *strs_out = NULL; *lens_out = NULL; return 0; }
    int32_t count = 0;

    if (duk_is_array(ctx, arr_idx)) {
        /* Fast path: index over the array directly. */
        duk_size_t n = duk_get_length(ctx, arr_idx);
        for (duk_size_t i = 0; i < n; i++) {
            duk_get_prop_index(ctx, arr_idx, (duk_uarridx_t)i);
            if (!duk_is_string(ctx, -1)) {
                duk_pop(ctx);
                for (int32_t k = 0; k < count; k++) free(strs[k]);
                free(strs); free(lens);
                *strs_out = NULL; *lens_out = NULL;
                RP_TYPE_THROW(ctx, "Iterable element must be a string");
            }
            const char *s = duk_get_string(ctx, -1);
            int32_t ul = 0;
            UChar *u = utf8_to_uchar(s, (int32_t)strlen(s), &ul);
            duk_pop(ctx);
            if ((duk_size_t)count >= cap) {
                cap *= 2;
                strs = (UChar **)realloc(strs, (cap + 1) * sizeof(UChar *));
                lens = (int32_t *)realloc(lens, (cap + 1) * sizeof(int32_t));
            }
            strs[count] = u;
            lens[count] = ul;
            if (u) count++;
        }
        *strs_out = strs; *lens_out = lens;
        return count;
    }

    /* General iterable: arg[Symbol.iterator]().  Loop until done; each
       yielded value must be a string. */
    duk_get_prop_string(ctx, arr_idx, "");  /* dummy push so stack is balanced */
    duk_pop(ctx);
    duk_get_global_string(ctx, "Symbol");
    duk_get_prop_string(ctx, -1, "iterator");
    duk_remove(ctx, -2);                    /* sym Symbol.iterator on top */
    duk_get_prop(ctx, arr_idx);             /* iter-fn from arg[sym] */
    if (!duk_is_callable(ctx, -1)) {
        duk_pop(ctx);
        free(strs); free(lens);
        *strs_out = NULL; *lens_out = NULL;
        RP_TYPE_THROW(ctx, "Intl.ListFormat: argument is not iterable");
    }
    duk_dup(ctx, arr_idx);
    duk_call_method(ctx, 0);                /* iter object */
    duk_idx_t iter_idx = duk_get_top_index(ctx);

    for (;;) {
        duk_get_prop_string(ctx, iter_idx, "next");
        duk_dup(ctx, iter_idx);
        duk_call_method(ctx, 0);            /* {value,done} */
        duk_get_prop_string(ctx, -1, "done");
        int done = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
        if (done) { duk_pop(ctx); break; }
        duk_get_prop_string(ctx, -1, "value");
        if (!duk_is_string(ctx, -1)) {
            /* Reject with TypeError BEFORE consuming any more iterator
               steps. */
            duk_pop_2(ctx);                 /* value, result */
            duk_pop(ctx);                   /* iter */
            for (int32_t k = 0; k < count; k++) free(strs[k]);
            free(strs); free(lens);
            *strs_out = NULL; *lens_out = NULL;
            RP_TYPE_THROW(ctx, "Iterable element must be a string");
        }
        const char *s = duk_get_string(ctx, -1);
        int32_t ul = 0;
        UChar *u = utf8_to_uchar(s, (int32_t)strlen(s), &ul);
        duk_pop_2(ctx);                     /* value, result */
        if ((duk_size_t)count >= cap) {
            cap *= 2;
            strs = (UChar **)realloc(strs, (cap + 1) * sizeof(UChar *));
            lens = (int32_t *)realloc(lens, (cap + 1) * sizeof(int32_t));
        }
        strs[count] = u;
        lens[count] = ul;
        if (u) count++;
    }
    duk_pop(ctx);                           /* iter */
    *strs_out = strs; *lens_out = lens;
    return count;
}

static duk_ret_t lf_format(duk_context *ctx)
{
    UChar **strs = NULL; int32_t *lens = NULL;
    int32_t count = lf_marshal_array(ctx, 0, &strs, &lens);

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_ULST);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(UListFormatter *)) {
        for (int32_t i = 0; i < count; i++) free(strs[i]);
        free(strs); free(lens);
        RP_TYPE_THROW(ctx, "Intl.ListFormat: format: invalid this");
    }
    UListFormatter *lf = *(UListFormatter **)p;
    duk_pop_2(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UChar out[1024];
    int32_t olen = ulistfmt_format(lf, (const UChar *const *)strs, lens, count, out, 1024, &err);
    if (err == U_BUFFER_OVERFLOW_ERROR) {
        err = U_ZERO_ERROR;
        UChar *big = (UChar *)malloc(sizeof(UChar) * (size_t)(olen + 1));
        if (big) {
            olen = ulistfmt_format(lf, (const UChar *const *)strs, lens, count, big, olen + 1, &err);
            if (U_SUCCESS(err)) uchar_push_utf8(ctx, big, olen);
            free(big);
        } else {
            duk_push_string(ctx, "");
        }
    } else if (U_SUCCESS(err)) {
        uchar_push_utf8(ctx, out, olen);
    } else {
        duk_push_string(ctx, "");
    }
    for (int32_t i = 0; i < count; i++) free(strs[i]);
    free(strs); free(lens);
    return 1;
}

static duk_ret_t lf_format_to_parts(duk_context *ctx)
{
    /* Coerce input to array via Array.from to accept iterables. */
    UChar **strs = NULL; int32_t *lens = NULL;
    int32_t count = lf_marshal_array(ctx, 0, &strs, &lens);

    duk_push_this(ctx);
    require_internal_slot(ctx, K_ULST, "ListFormat");
    duk_get_prop_string(ctx, -1, K_ULST);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(UListFormatter *)) {
        for (int32_t i = 0; i < count; i++) free(strs[i]);
        free(strs); free(lens);
        RP_TYPE_THROW(ctx, "Intl.ListFormat: formatToParts: invalid this");
    }
    UListFormatter *lf = *(UListFormatter **)p;
    duk_pop_2(ctx);

    UErrorCode err = U_ZERO_ERROR;
    UFormattedList *fr = ulistfmt_openResult(&err);
    if (!fr) {
        for (int32_t i = 0; i < count; i++) free(strs[i]);
        free(strs); free(lens);
        RP_THROW(ctx, "openResult: %s", u_errorName(err));
    }
    ulistfmt_formatStringsToResult(lf, (const UChar *const *)strs, lens, count, fr, &err);
    if (U_FAILURE(err)) {
        for (int32_t i = 0; i < count; i++) free(strs[i]);
        free(strs); free(lens);
        ulistfmt_closeResult(fr);
        RP_THROW(ctx, "formatToResult: %s", u_errorName(err));
    }
    const UFormattedValue *fv = ulistfmt_resultAsValue(fr, &err);
    int32_t slen = 0;
    const UChar *str = ufmtval_getString(fv, &slen, &err);

    /* Coverage map: for each char, which field (literal=0, element=1, or
       -1 if unset).  ICU yields fields in document order via ufmtval. */
    int *cov = (int *)malloc(sizeof(int) * (size_t)(slen + 1));
    for (int32_t i = 0; i < slen; i++) cov[i] = -1;
    UConstrainedFieldPosition *cfp = ucfpos_open(&err);
    ucfpos_constrainCategory(cfp, UFIELD_CATEGORY_LIST, &err);
    while (ufmtval_nextPosition(fv, cfp, &err) && U_SUCCESS(err)) {
        int32_t fld = ucfpos_getField(cfp, &err);
        int32_t bs = 0, be = 0;
        ucfpos_getIndexes(cfp, &bs, &be, &err);
        if (bs < 0 || be > slen || bs >= be) continue;
        for (int32_t i = bs; i < be; i++) if (cov[i] == -1) cov[i] = fld;
    }
    ucfpos_close(cfp);

    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    int32_t i = 0;
    while (i < slen) {
        int f = cov[i];
        int32_t j = i + 1;
        while (j < slen && cov[j] == f) j++;
        const char *type = (f == ULISTFMT_ELEMENT_FIELD) ? "element" : "literal";
        duk_push_object(ctx);
        duk_push_string(ctx, type);            duk_put_prop_string(ctx, -2, "type");
        uchar_push_utf8(ctx, str + i, j - i);  duk_put_prop_string(ctx, -2, "value");
        duk_put_prop_index(ctx, -2, outi++);
        i = j;
    }

    free(cov);
    for (int32_t i2 = 0; i2 < count; i2++) free(strs[i2]);
    free(strs); free(lens);
    ulistfmt_closeResult(fr);
    return 1;
}

static duk_ret_t lf_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_ULST, "ListFormat");
    duk_get_prop_string(ctx, -1, K_LOC);   const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE);  const char *tp  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_STYLE); const char *st  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc)); duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, tp);  duk_put_prop_string(ctx, -2, "type");
    duk_push_string(ctx, st);  duk_put_prop_string(ctx, -2, "style");
    return 1;
}

/* ================================================================
 * DisplayNames
 * ============================================================== */

static duk_ret_t dn_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.DisplayNames: must be called with new");

    /* Locale arg: string or array.  Per ECMA-402 CanonicalizeLocaleList,
       arrays pick the first ICU-available element. */
    const char *locale = NULL;
    if (duk_get_top(ctx) > 0) {
        if (duk_is_string(ctx, 0)) {
            locale = duk_get_string(ctx, 0);
        } else if (duk_is_array(ctx, 0)) {
            duk_size_t llen = duk_get_length(ctx, 0);
            for (duk_size_t i = 0; i < llen && !locale; i++) {
                duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
                if (duk_is_string(ctx, -1)) locale = duk_get_string(ctx, -1);
                /* Leave it on stack so the const char* stays live. */
                if (!locale) duk_pop(ctx);
            }
        }
    }
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    /* Per spec, the second arg is REQUIRED — but be lenient. */
    const char *type = "language", *style = "long";
    const char *fallback = "code", *langDisplay = "dialect";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        /* DisplayNames spec read order: localeMatcher, style, type,
           fallback, languageDisplay. */
        static const char *TYPE_VALS[]  = {"language","region","script","currency","calendar","dateTimeField", NULL};
        static const char *STYLE_VALS[] = {"long","short","narrow", NULL};
        static const char *FB_VALS[]    = {"code","none", NULL};
        static const char *LD_VALS[]    = {"dialect","standard", NULL};
        static const char *LM_VALS[]    = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher",   LM_VALS,    1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "style",           STYLE_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "type",            TYPE_VALS,  1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "fallback",        FB_VALS,    1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "languageDisplay", LD_VALS,    1)) duk_pop(ctx);
        const char *t = opt_get_string(ctx, opts_idx, "type");
        if (t) { type = t; duk_pop(ctx); }
        const char *s = opt_get_string(ctx, opts_idx, "style");
        if (s) { style = s; duk_pop(ctx); }
        const char *f = opt_get_string(ctx, opts_idx, "fallback");
        if (f) { fallback = f; duk_pop(ctx); }
        const char *ld = opt_get_string(ctx, opts_idx, "languageDisplay");
        if (ld) { langDisplay = ld; duk_pop(ctx); }
    }

    UErrorCode err = U_ZERO_ERROR;
    UDialectHandling dh = (strcmp(langDisplay, "standard") == 0)
                           ? ULDN_STANDARD_NAMES : ULDN_DIALECT_NAMES;
    ULocaleDisplayNames *dn = uldn_open(locale, dh, &err);
    if (U_FAILURE(err) || !dn)
        RP_THROW(ctx, "Intl.DisplayNames: open: %s", u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(ULocaleDisplayNames *));
    *(ULocaleDisplayNames **)buf = dn;
    duk_put_prop_string(ctx, -2, K_ULDN);
    duk_push_string(ctx, locale);      duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, type);        duk_put_prop_string(ctx, -2, K_TYPE);
    duk_push_string(ctx, style);       duk_put_prop_string(ctx, -2, K_STYLE);
    duk_push_string(ctx, fallback);    duk_put_prop_string(ctx, -2, "\xff" "fb");
    duk_push_string(ctx, langDisplay); duk_put_prop_string(ctx, -2, "\xff" "ld");
    return 0;
}

static duk_ret_t dn_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_ULDN);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(ULocaleDisplayNames *)) {
            ULocaleDisplayNames *d = *(ULocaleDisplayNames **)p;
            if (d) uldn_close(d);
            *(ULocaleDisplayNames **)p = NULL;
        }
    }
    return 0;
}

static duk_ret_t dn_of(duk_context *ctx)
{
    const char *code = duk_to_string(ctx, 0);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_ULDN);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(ULocaleDisplayNames *))
        RP_TYPE_THROW(ctx, "Intl.DisplayNames: of: invalid this");
    ULocaleDisplayNames *dn = *(ULocaleDisplayNames **)p;
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, K_TYPE);
    const char *type = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, "\xff" "fb");
    const char *fb = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    UChar out[256]; int32_t olen = 0; UErrorCode err = U_ZERO_ERROR;
    if (!strcmp(type, "language")) {
        /* localeDisplayName handles compound tags like "ja-Hira"; the
           narrower languageDisplayName only accepts the bare language
           subtag and falls through to code on anything else. */
        olen = uldn_localeDisplayName(dn, code, out, 256, &err);
    } else if (!strcmp(type, "region")) {
        olen = uldn_regionDisplayName(dn, code, out, 256, &err);
    } else if (!strcmp(type, "script")) {
        olen = uldn_scriptDisplayName(dn, code, out, 256, &err);
    } else if (!strcmp(type, "currency")) {
        /* Currency: use ucurr_getName. */
        UChar ccode[8] = {0};
        int32_t clen = (int32_t)strlen(code);
        if (clen >= 3) {
            for (int i = 0; i < 3 && code[i]; i++) ccode[i] = (UChar)code[i];
            int32_t len = 0;
            UBool isChoice = 0;
            const UChar *nm = ucurr_getName(ccode, loc, UCURR_LONG_NAME, &isChoice, &len, &err);
            if (U_SUCCESS(err) && nm) {
                uchar_push_utf8(ctx, nm, len);
                return 1;
            }
        }
        if (strcmp(fb, "none") == 0) { duk_push_undefined(ctx); return 1; }
        duk_push_string(ctx, code);  /* fallback: code */
        return 1;
    } else if (!strcmp(type, "calendar")) {
        /* ICU doesn't expose calendar display names via uldn; return code. */
        if (strcmp(fb, "none") == 0) { duk_push_undefined(ctx); return 1; }
        duk_push_string(ctx, code);
        return 1;
    } else if (!strcmp(type, "dateTimeField")) {
        /* Spec returns localized field names ("year", "month", …); ICU
           has no clean accessor — return code per fallback policy. */
        if (strcmp(fb, "none") == 0) { duk_push_undefined(ctx); return 1; }
        duk_push_string(ctx, code);
        return 1;
    } else {
        olen = uldn_localeDisplayName(dn, code, out, 256, &err);
    }

    if (U_SUCCESS(err) && olen > 0) {
        uchar_push_utf8(ctx, out, olen);
        return 1;
    }
    if (strcmp(fb, "none") == 0) { duk_push_undefined(ctx); return 1; }
    duk_push_string(ctx, code);
    return 1;
}

static duk_ret_t dn_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_ULDN, "DisplayNames");
    duk_get_prop_string(ctx, -1, K_LOC);   const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE);  const char *tp  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_STYLE); const char *st  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, "\xff" "fb"); const char *fb  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, "\xff" "ld"); const char *ld  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc)); duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, st);  duk_put_prop_string(ctx, -2, "style");
    duk_push_string(ctx, tp);  duk_put_prop_string(ctx, -2, "type");
    duk_push_string(ctx, fb);  duk_put_prop_string(ctx, -2, "fallback");
    duk_push_string(ctx, ld);  duk_put_prop_string(ctx, -2, "languageDisplay");
    return 1;
}

/* ================================================================
 * Locale
 * ============================================================== */

/* Helper: pull a single uloc keyword value (returns malloc'd string or NULL) */
static char *uloc_keyword_get(const char *locale, const char *kw)
{
    UErrorCode err = U_ZERO_ERROR;
    char buf[64];
    int32_t n = uloc_getKeywordValue(locale, kw, buf, 64, &err);
    if (U_FAILURE(err) || n <= 0) return NULL;
    char *r = (char *)malloc((size_t)n + 1);
    if (!r) return NULL;
    memcpy(r, buf, (size_t)n); r[n] = 0;
    return r;
}

static void put_str_prop(duk_context *ctx, const char *key, const char *val)
{
    if (val && *val) duk_push_string(ctx, val);
    else             duk_push_undefined(ctx);
    duk_put_prop_string(ctx, -2, key);
}

static duk_ret_t loc_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.Locale: must be called with new");

    const char *tag = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        tag = duk_get_string(ctx, 0);
    if (!tag || !*tag)
        RP_TYPE_THROW(ctx, "Intl.Locale: requires locale tag");

    /* Convert BCP 47 tag -> ICU locale id (replaces -u-… with @keys).
       ICU's forLanguageTag returns clen=0 for tags like "und" — that's
       a valid undetermined locale, not an error.  Use it as-is. */
    char canon[256]; UErrorCode err = U_ZERO_ERROR;
    int32_t clen = uloc_forLanguageTag(tag, canon, 256, NULL, &err);
    if (U_FAILURE(err))
        RP_RANGE_THROW(ctx, "Intl.Locale: bad tag: %s", tag);
    if (clen <= 0) {
        strncpy(canon, tag, sizeof(canon) - 1);
        canon[sizeof(canon) - 1] = 0;
    }

    /* opts: optional override of calendar/collation/hourCycle/etc. */
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        /* Validate enums + reject empty strings. */
        static const char *HC_VALS[] = {"h11","h12","h23","h24", NULL};
        static const char *CF_VALS[] = {"upper","lower","false", NULL};
        if (validate_enum_opt(ctx, opts_idx, "hourCycle", HC_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "caseFirst", CF_VALS, 1)) duk_pop(ctx);
        /* BCP 47 subtag validation per option key.  Each function
           checks the spec-mandated character class and length range
           for that subtag form. */
        struct {
            const char *key;
            int min_len, max_len;
            int alpha_only;   /* 1 = letters only; 0 = letters+digits */
            int allow_compound; /* 1 = allow hyphen subtags (Unicode locale type) */
            int region_form;  /* 1 = also accept exactly 3 digits */
        } SUB[] = {
            {"language",        2, 3, 1, 0, 0},
            {"script",          4, 4, 1, 0, 0},
            {"region",          2, 3, 1, 0, 1},
            {"calendar",        3, 8, 0, 1, 0},
            {"collation",       3, 8, 0, 1, 0},
            {"numberingSystem", 3, 8, 0, 1, 0},
            {NULL, 0, 0, 0, 0, 0}
        };
        for (int i = 0; SUB[i].key; i++) {
            duk_get_prop_string(ctx, opts_idx, SUB[i].key);
            if (duk_is_undefined(ctx, -1)) { duk_pop(ctx); continue; }
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid %s option: empty string", SUB[i].key);
            /* Region special: 3 digits. */
            size_t vl = strlen(v);
            int all_digits = 1;
            for (size_t k = 0; k < vl; k++)
                if (!(v[k] >= '0' && v[k] <= '9')) { all_digits = 0; break; }
            if (SUB[i].region_form && vl == 3 && all_digits) { duk_pop(ctx); continue; }
            /* Validate subtag(s). */
            const char *p = v;
            int bad = 0;
            while (*p && !bad) {
                const char *seg = p;
                while (*p && *p != '-') p++;
                size_t sl = (size_t)(p - seg);
                if (sl < (size_t)SUB[i].min_len || sl > (size_t)SUB[i].max_len) {
                    bad = 1; break;
                }
                for (size_t k = 0; k < sl; k++) {
                    char c = seg[k];
                    int is_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                    int is_digit  = (c >= '0' && c <= '9');
                    if (SUB[i].alpha_only) { if (!is_letter) { bad = 1; break; } }
                    else                   { if (!is_letter && !is_digit) { bad = 1; break; } }
                }
                if (*p == '-') {
                    if (!SUB[i].allow_compound) { bad = 1; break; }
                    p++;
                }
            }
            if (bad) RP_RANGE_THROW(ctx, "Invalid %s option: %s", SUB[i].key, v);
            duk_pop(ctx);
        }
        const char *override[][2] = {
            {"calendar",        "calendar"},
            {"collation",       "collation"},
            {"hourCycle",       "hours"},
            {"caseFirst",       "colcasefirst"},
            {"numberingSystem", "numbers"},
            {NULL, NULL}
        };
        for (int i = 0; override[i][0]; i++) {
            const char *v = opt_get_string(ctx, opts_idx, override[i][0]);
            if (v) {
                /* Canonicalize legacy → modern Unicode locale types
                   (e.g. "islamicc"→"islamic-civil", "gregorian"→"gregory").
                   The ICU keyword name passed in is the legacy form
                   (`calendar`, `numbers`); the keyword VALUE is what
                   needs canonicalization, so we look up by the
                   BCP 47 key name (override[i][0]).  uloc_toUnicodeLocaleType
                   expects the modern keyword name. */
                static const char *KEY_LEGACY_TO_MODERN[][2] = {
                    {"calendar",        "ca"},
                    {"collation",       "co"},
                    {"hourCycle",       "hc"},
                    {"caseFirst",       "kf"},
                    {"numberingSystem", "nu"},
                    {NULL, NULL}
                };
                const char *modern_key = NULL;
                for (int k = 0; KEY_LEGACY_TO_MODERN[k][0]; k++)
                    if (!strcmp(KEY_LEGACY_TO_MODERN[k][0], override[i][0]))
                        { modern_key = KEY_LEGACY_TO_MODERN[k][1]; break; }
                const char *can = modern_key
                                  ? uloc_toUnicodeLocaleType(modern_key, v)
                                  : NULL;
                UErrorCode e2 = U_ZERO_ERROR;
                int32_t n2 = uloc_setKeywordValue(override[i][1],
                                                  can ? can : v,
                                                  canon, 256, &e2);
                if (U_SUCCESS(e2) && n2 > 0) {
                    /* canon mutated in place by setKeywordValue */
                }
                duk_pop(ctx);
            }
        }
        /* numeric (bool) */
        duk_get_prop_string(ctx, opts_idx, "numeric");
        if (duk_is_boolean(ctx, -1)) {
            UErrorCode e2 = U_ZERO_ERROR;
            uloc_setKeywordValue("colnumeric",
                                 duk_get_boolean(ctx, -1) ? "yes" : "no",
                                 canon, 256, &e2);
        }
        duk_pop(ctx);
        const char *lang = opt_get_string(ctx, opts_idx, "language");
        const char *script = opt_get_string(ctx, opts_idx, "script");
        const char *region = opt_get_string(ctx, opts_idx, "region");
        /* override base subtags is rarely useful; skip composite rewrite */
        (void)lang; (void)script; (void)region;
        if (lang)   duk_pop(ctx);
        if (script) duk_pop(ctx);
        if (region) duk_pop(ctx);
    }

    /* Re-emit as BCP 47 for baseName / toString. */
    char tag2[256]; err = U_ZERO_ERROR;
    int32_t tlen = uloc_toLanguageTag(canon, tag2, 256, 0, &err);
    if (U_FAILURE(err)) {
        strncpy(tag2, tag, 255); tag2[255] = 0; tlen = (int32_t)strlen(tag2);
    }
    tag2[tlen] = 0;
    apply_cldr_aliases(tag2, sizeof(tag2));
    tlen = (int32_t)strlen(tag2);

    duk_push_this(ctx);
    duk_push_lstring(ctx, tag2, (duk_size_t)tlen); duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, canon);                   duk_put_prop_string(ctx, -2, "\xff" "icu");
    return 0;
}

static const char *loc_get_icu(duk_context *ctx)
{
    duk_get_prop_string(ctx, -1, "\xff" "icu");
    const char *r = duk_get_string(ctx, -1);
    duk_pop(ctx);
    return r;
}

static duk_ret_t loc_to_string(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    duk_get_prop_string(ctx, -1, K_LOC);
    return 1;
}

static duk_ret_t loc_maximize(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    const char *icu = loc_get_icu(ctx);
    char out[256]; UErrorCode err = U_ZERO_ERROR;
    uloc_addLikelySubtags(icu, out, 256, &err);
    duk_pop(ctx);
    if (U_FAILURE(err))
        RP_THROW(ctx, "Intl.Locale.maximize: %s", u_errorName(err));
    char tag[256]; err = U_ZERO_ERROR;
    uloc_toLanguageTag(out, tag, 256, 0, &err);
    /* Construct a new Locale via require('rampart-intl').Locale */
    duk_get_global_string(ctx, "Intl");
    duk_get_prop_string(ctx, -1, "Locale");
    duk_push_string(ctx, tag);
    duk_new(ctx, 1);
    duk_remove(ctx, -2);
    return 1;
}

static duk_ret_t loc_minimize(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    const char *icu = loc_get_icu(ctx);
    char out[256]; UErrorCode err = U_ZERO_ERROR;
    uloc_minimizeSubtags(icu, out, 256, &err);
    duk_pop(ctx);
    if (U_FAILURE(err))
        RP_THROW(ctx, "Intl.Locale.minimize: %s", u_errorName(err));
    char tag[256]; err = U_ZERO_ERROR;
    uloc_toLanguageTag(out, tag, 256, 0, &err);
    duk_get_global_string(ctx, "Intl");
    duk_get_prop_string(ctx, -1, "Locale");
    duk_push_string(ctx, tag);
    duk_new(ctx, 1);
    duk_remove(ctx, -2);
    return 1;
}

/* Build resolvedOptions-shaped object as the JS "view" of a Locale. */
static void loc_push_view(duk_context *ctx, const char *icu)
{
    char buf[64]; UErrorCode err;

    duk_push_object(ctx);

    /* baseName */
    err = U_ZERO_ERROR; uloc_getBaseName(icu, buf, 64, &err);
    char tag[128]; err = U_ZERO_ERROR;
    uloc_toLanguageTag(buf, tag, 128, 0, &err);
    put_str_prop(ctx, "baseName", U_SUCCESS(err) ? tag : "");

    err = U_ZERO_ERROR; uloc_getLanguage(icu, buf, 64, &err);
    put_str_prop(ctx, "language", U_SUCCESS(err) ? buf : "");
    err = U_ZERO_ERROR; uloc_getScript(icu, buf, 64, &err);
    put_str_prop(ctx, "script", U_SUCCESS(err) ? buf : "");
    err = U_ZERO_ERROR; uloc_getCountry(icu, buf, 64, &err);
    put_str_prop(ctx, "region", U_SUCCESS(err) ? buf : "");

    char *cal  = uloc_keyword_get(icu, "calendar");
    char *coll = uloc_keyword_get(icu, "collation");
    char *hc   = uloc_keyword_get(icu, "hours");
    char *cf   = uloc_keyword_get(icu, "colcasefirst");
    char *ns   = uloc_keyword_get(icu, "numbers");
    char *kn   = uloc_keyword_get(icu, "colnumeric");

    /* ICU stores keyword values in its internal (sometimes alias)
       form — e.g. "ethiopic-amete-alem".  ECMA-402 wants the BCP 47
       Unicode locale type, e.g. "ethioaa".  Round-trip through
       uloc_toUnicodeLocaleType which knows the alias→canonical map. */
    const char *cal_b = cal  ? uloc_toUnicodeLocaleType("ca", cal)  : NULL;
    const char *col_b = coll ? uloc_toUnicodeLocaleType("co", coll) : NULL;
    const char *hc_b  = hc   ? uloc_toUnicodeLocaleType("hc", hc)   : NULL;
    const char *cf_b  = cf   ? uloc_toUnicodeLocaleType("kf", cf)   : NULL;
    const char *ns_b  = ns   ? uloc_toUnicodeLocaleType("nu", ns)   : NULL;
    put_str_prop(ctx, "calendar",        cal_b ? cal_b : cal);
    put_str_prop(ctx, "collation",       col_b ? col_b : coll);
    put_str_prop(ctx, "hourCycle",       hc_b  ? hc_b  : hc);
    put_str_prop(ctx, "caseFirst",       cf_b  ? cf_b  : cf);
    put_str_prop(ctx, "numberingSystem", ns_b  ? ns_b  : ns);
    duk_push_boolean(ctx, kn && !strcmp(kn, "yes"));
    duk_put_prop_string(ctx, -2, "numeric");

    free(cal); free(coll); free(hc); free(cf); free(ns); free(kn);
}

/* Locale property accessors.  Each is a getter that reads from
   `this`'s stored ICU locale id, builds a fresh view object, and
   returns the named field.  Dispatched by `magic` so we can register
   all ten with a single C function. */
static const char *LOC_FIELDS[] = {
    "baseName", "language", "script", "region",
    "calendar", "collation", "hourCycle", "numberingSystem",
    "caseFirst", "numeric",
    NULL
};
static duk_ret_t loc_getter_by_magic(duk_context *ctx)
{
    duk_int_t m = duk_get_current_magic(ctx);
    if (m < 0 || m >= (int)(sizeof(LOC_FIELDS)/sizeof(LOC_FIELDS[0])) - 1)
        return 0;
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    const char *icu = loc_get_icu(ctx);
    loc_push_view(ctx, icu);
    duk_get_prop_string(ctx, -1, LOC_FIELDS[m]);
    return 1;
}

static __attribute__((unused)) duk_ret_t
loc_get_field(duk_context *ctx)
{
    /* Generic getter: field name passed via magic / wrapped via this->field */
    const char *field = duk_require_string(ctx, 0);
    duk_push_this(ctx);
    const char *icu = loc_get_icu(ctx);
    loc_push_view(ctx, icu);
    duk_get_prop_string(ctx, -1, field);
    return 1;
}

/* Locale.prototype.getCalendars / getCollations / getHourCycles /
   getNumberingSystems / getTimeZones — all return arrays of strings.
   getTextInfo, getWeekInfo return objects.  Dispatched by magic. */
enum LocMethod {
    LM_CALENDARS = 0, LM_COLLATIONS, LM_HOURCYCLES,
    LM_NUMSYS, LM_TIMEZONES, LM_TEXTINFO, LM_WEEKINFO
};

static duk_ret_t loc_method_by_magic(duk_context *ctx)
{
    duk_int_t m = duk_get_current_magic(ctx);
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    const char *icu = loc_get_icu(ctx);
    UErrorCode err = U_ZERO_ERROR;

    if (m == LM_CALENDARS) {
        /* If the locale has -u-ca-X, return [X]; else enumerate locale's
           preferred calendars via ucal_getKeywordValuesForLocale. */
        char *cal = uloc_keyword_get(icu, "calendar");
        duk_push_array(ctx);
        duk_uarridx_t k = 0;
        if (cal) {
            const char *can = uloc_toUnicodeLocaleType("ca", cal);
            duk_push_string(ctx, can ? can : cal);
            duk_put_prop_index(ctx, -2, k++);
            free(cal);
        } else {
            UEnumeration *e = ucal_getKeywordValuesForLocale("calendar", icu, 1, &err);
            if (U_SUCCESS(err) && e) {
                const char *id; int32_t id_len;
                while ((id = uenum_next(e, &id_len, &err)) && U_SUCCESS(err)) {
                    const char *can = uloc_toUnicodeLocaleType("ca", id);
                    duk_push_string(ctx, can ? can : id);
                    duk_put_prop_index(ctx, -2, k++);
                }
                uenum_close(e);
            }
        }
        duk_remove(ctx, -2);  /* this, leave array on top */
        return 1;
    }
    if (m == LM_COLLATIONS) {
        char *coll = uloc_keyword_get(icu, "collation");
        duk_push_array(ctx);
        duk_uarridx_t k = 0;
        if (coll) {
            const char *can = uloc_toUnicodeLocaleType("co", coll);
            duk_push_string(ctx, can ? can : coll);
            duk_put_prop_index(ctx, -2, k++);
            free(coll);
        } else {
            UEnumeration *e = ucol_getKeywordValuesForLocale("collation", icu, 1, &err);
            if (U_SUCCESS(err) && e) {
                const char *id; int32_t id_len;
                while ((id = uenum_next(e, &id_len, &err)) && U_SUCCESS(err)) {
                    if (id_len == 8 && !memcmp(id, "standard", 8)) continue;
                    if (id_len == 6 && !memcmp(id, "search", 6)) continue;
                    const char *can = uloc_toUnicodeLocaleType("co", id);
                    duk_push_string(ctx, can ? can : id);
                    duk_put_prop_index(ctx, -2, k++);
                }
                uenum_close(e);
            }
        }
        duk_remove(ctx, -2);  /* this */
        return 1;
    }
    if (m == LM_HOURCYCLES) {
        char *hc = uloc_keyword_get(icu, "hours");
        duk_push_array(ctx);
        duk_uarridx_t k = 0;
        if (hc) {
            const char *can = uloc_toUnicodeLocaleType("hc", hc);
            duk_push_string(ctx, can ? can : hc);
            duk_put_prop_index(ctx, -2, k++);
            free(hc);
        } else {
            /* Use locale's default DateTimePatternGenerator to derive
               which hour cycle is preferred. */
            UDateTimePatternGenerator *gen = udatpg_open(icu, &err);
            const char *cycle = "h23";
            if (U_SUCCESS(err) && gen) {
                UChar skel[] = { 'j', 0 };
                UChar pat[64];
                err = U_ZERO_ERROR;
                int32_t pn = udatpg_getBestPattern(gen, skel, 1, pat, 64, &err);
                if (U_SUCCESS(err)) {
                    int has_h = 0, has_H = 0, has_K = 0, has_k = 0;
                    for (int32_t j = 0; j < pn; j++) {
                        if (pat[j] == 'h') has_h = 1;
                        else if (pat[j] == 'H') has_H = 1;
                        else if (pat[j] == 'K') has_K = 1;
                        else if (pat[j] == 'k') has_k = 1;
                    }
                    if (has_h) cycle = "h12";
                    else if (has_H) cycle = "h23";
                    else if (has_K) cycle = "h11";
                    else if (has_k) cycle = "h24";
                }
                udatpg_close(gen);
            }
            duk_push_string(ctx, cycle);
            duk_put_prop_index(ctx, -2, k++);
        }
        duk_remove(ctx, -2);  /* this */
        return 1;
    }
    if (m == LM_NUMSYS) {
        char *ns = uloc_keyword_get(icu, "numbers");
        duk_push_array(ctx);
        duk_uarridx_t k = 0;
        if (ns) {
            const char *can = uloc_toUnicodeLocaleType("nu", ns);
            duk_push_string(ctx, can ? can : ns);
            duk_put_prop_index(ctx, -2, k++);
            free(ns);
        } else {
            UNumberingSystem *uns = unumsys_open(icu, &err);
            if (U_SUCCESS(err) && uns) {
                const char *name = unumsys_getName(uns);
                duk_push_string(ctx, name ? name : "latn");
                duk_put_prop_index(ctx, -2, k++);
                unumsys_close(uns);
            } else {
                duk_push_string(ctx, "latn");
                duk_put_prop_index(ctx, -2, k++);
            }
        }
        duk_remove(ctx, -2);  /* this */
        return 1;
    }
    if (m == LM_TIMEZONES) {
        /* Region-specific time zones; returns undefined if no region. */
        char region[8] = {0};
        err = U_ZERO_ERROR;
        int32_t rl = uloc_getCountry(icu, region, sizeof(region), &err);
        if (rl <= 0) {
            duk_pop(ctx);  /* this */
            duk_push_undefined(ctx);
            return 1;
        }
        duk_push_array(ctx);
        duk_uarridx_t k = 0;
        err = U_ZERO_ERROR;
        UEnumeration *e = ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_CANONICAL,
                                                          region, NULL, &err);
        if (U_SUCCESS(err) && e) {
            const char *id; int32_t id_len;
            /* Collect into vector then sort. */
            char **arr = NULL; size_t cap = 0, n = 0;
            while ((id = uenum_next(e, &id_len, &err)) && U_SUCCESS(err)) {
                if (n >= cap) {
                    cap = cap ? cap * 2 : 16;
                    arr = realloc(arr, cap * sizeof(char *));
                }
                arr[n] = strndup(id, id_len);
                n++;
            }
            uenum_close(e);
            for (size_t a = 0; a < n; a++)
                for (size_t b = a + 1; b < n; b++)
                    if (strcmp(arr[a], arr[b]) > 0) {
                        char *t = arr[a]; arr[a] = arr[b]; arr[b] = t;
                    }
            for (size_t a = 0; a < n; a++) {
                duk_push_string(ctx, arr[a]);
                duk_put_prop_index(ctx, -2, k++);
                free(arr[a]);
            }
            free(arr);
        }
        duk_remove(ctx, -2);  /* this, leave array */
        return 1;
    }
    if (m == LM_TEXTINFO) {
        duk_push_object(ctx);
        UScriptCode sc[8];
        int32_t scn = uscript_getCode(icu, sc, 8, &err);
        int rtl = 0;
        if (U_SUCCESS(err) && scn > 0) {
            rtl = uscript_isRightToLeft(sc[0]);
        }
        duk_push_string(ctx, rtl ? "rtl" : "ltr");
        duk_put_prop_string(ctx, -2, "direction");
        duk_remove(ctx, -2);  /* this */
        return 1;
    }
    if (m == LM_WEEKINFO) {
        duk_push_object(ctx);
        UCalendar *cal = ucal_open(NULL, 0, icu, UCAL_DEFAULT, &err);
        int firstDay = 1, minDays = 1;
        if (U_SUCCESS(err) && cal) {
            int fd = ucal_getAttribute(cal, UCAL_FIRST_DAY_OF_WEEK);
            /* ICU: 1=Sun..7=Sat.  ECMA-402: 1=Mon..7=Sun. */
            firstDay = (fd == 1) ? 7 : (fd - 1);
            minDays = ucal_getAttribute(cal, UCAL_MINIMAL_DAYS_IN_FIRST_WEEK);
            /* Build weekend array. */
            int wend[8]; int wc = 0;
            for (int d = 1; d <= 7; d++) {
                UCalendarWeekdayType t = ucal_getDayOfWeekType(cal, d, &err);
                if (t == UCAL_WEEKEND || t == UCAL_WEEKEND_ONSET || t == UCAL_WEEKEND_CEASE) {
                    int ecma = (d == 1) ? 7 : (d - 1);
                    wend[wc++] = ecma;
                }
            }
            duk_push_array(ctx);
            for (int i = 0; i < wc; i++) {
                duk_push_int(ctx, wend[i]);
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
            }
            duk_put_prop_string(ctx, -2, "weekend");
            ucal_close(cal);
        } else {
            duk_push_array(ctx);
            duk_put_prop_string(ctx, -2, "weekend");
        }
        duk_push_int(ctx, firstDay);
        duk_put_prop_string(ctx, -2, "firstDay");
        duk_push_int(ctx, minDays);
        duk_put_prop_string(ctx, -2, "minimalDays");
        duk_remove(ctx, -2);
        return 1;
    }
    return 0;
}

/* firstDayOfWeek getter: return ECMA-402 "mon"/"tue"/.../"sun" if the
   locale has -u-fw-X, else undefined. */
static duk_ret_t loc_first_day_of_week(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    const char *icu = loc_get_icu(ctx);
    char *fw = uloc_keyword_get(icu, "fw");
    if (fw) {
        const char *can = uloc_toUnicodeLocaleType("fw", fw);
        duk_push_string(ctx, can ? can : fw);
        free(fw);
    } else {
        duk_push_undefined(ctx);
    }
    duk_remove(ctx, -2);  /* this */
    return 1;
}

/* variants getter: split baseName, collect subtags that are variants
   (5-8 alphanumeric, or 4-char starting with digit).  Lowercased and
   joined with "-".  Undefined if none. */
static duk_ret_t loc_variants(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, "\xff" "icu", "Locale", 0);
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *tag = duk_get_string(ctx, -1);
    if (!tag) { duk_pop_2(ctx); duk_push_undefined(ctx); return 1; }
    char buf[256] = {0};
    /* Skip -u-/-t-/-x- extensions for variant search. */
    char base[256]; size_t bl = 0;
    for (const char *p = tag; *p && bl < sizeof(base) - 1; ) {
        if (p[0] == '-' && p[1] && p[2] == '-' &&
            ((p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z')) &&
             !(p[1] >= '0' && p[1] <= '9')) {
            break;
        }
        base[bl++] = *p++;
    }
    base[bl] = 0;
    /* Walk subtags of base; collect variants. */
    char *p = base, *seg;
    int seg_idx = 0;
    size_t ol = 0;
    while (*p) {
        seg = p;
        while (*p && *p != '-') p++;
        size_t sl = (size_t)(p - seg);
        if (*p == '-') *p++ = 0;
        seg_idx++;
        if (seg_idx <= 1) continue;  /* skip language */
        /* Skip script (4 alpha) and region (2 alpha or 3 digit). */
        if (sl == 4) {
            int all_alpha = 1;
            for (size_t k = 0; k < sl; k++)
                if (!((seg[k] >= 'a' && seg[k] <= 'z') || (seg[k] >= 'A' && seg[k] <= 'Z')))
                    { all_alpha = 0; break; }
            if (all_alpha) continue;
        }
        if (sl == 2 || sl == 3) {
            /* Could be region — check this is right after language/script. */
            int all_alpha = 1, all_digit = 1;
            for (size_t k = 0; k < sl; k++) {
                if (!((seg[k] >= 'a' && seg[k] <= 'z') || (seg[k] >= 'A' && seg[k] <= 'Z')))
                    all_alpha = 0;
                if (!(seg[k] >= '0' && seg[k] <= '9'))
                    all_digit = 0;
            }
            if ((sl == 2 && all_alpha) || (sl == 3 && all_digit)) continue;
        }
        /* It's a variant.  Lowercase it. */
        if (ol > 0 && ol < sizeof(buf) - 1) buf[ol++] = '-';
        for (size_t k = 0; k < sl && ol < sizeof(buf) - 1; k++) {
            char c = seg[k];
            buf[ol++] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        buf[ol] = 0;
    }
    duk_pop_2(ctx);  /* tag, this */
    if (ol > 0) duk_push_string(ctx, buf);
    else        duk_push_undefined(ctx);
    return 1;
}

/* ================================================================
 * Segmenter
 * ============================================================== */

static UBreakIteratorType sg_type_from_str(const char *s)
{
    if (!s) return UBRK_WORD;
    if (!strcmp(s, "grapheme")) return UBRK_CHARACTER;
    if (!strcmp(s, "sentence")) return UBRK_SENTENCE;
    return UBRK_WORD;
}

static duk_ret_t sg_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.Segmenter: must be called with new");

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    const char *gran = "grapheme";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        static const char *GR_VALS[] = {"grapheme","word","sentence", NULL};
        static const char *LM_VALS[] = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "granularity",   GR_VALS, 1)) duk_pop(ctx);
        const char *g = opt_get_string(ctx, opts_idx, "granularity");
        if (g) { gran = g; duk_pop(ctx); }
    }

    UBreakIteratorType bt = sg_type_from_str(gran);
    UErrorCode err = U_ZERO_ERROR;
    UBreakIterator *bi = ubrk_open(bt, locale, NULL, 0, &err);
    if (U_FAILURE(err) || !bi)
        RP_THROW(ctx, "Intl.Segmenter: open: %s", u_errorName(err));

    duk_push_this(ctx);
    void *buf = duk_push_fixed_buffer(ctx, sizeof(UBreakIterator *));
    *(UBreakIterator **)buf = bi;
    duk_put_prop_string(ctx, -2, K_UBRK);
    duk_push_string(ctx, locale); duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, gran);   duk_put_prop_string(ctx, -2, K_TYPE);
    return 0;
}

static duk_ret_t sg_finalizer(duk_context *ctx)
{
    duk_get_prop_string(ctx, 0, K_UBRK);
    if (duk_is_buffer_data(ctx, -1)) {
        duk_size_t n = 0;
        void *p = duk_get_buffer_data(ctx, -1, &n);
        if (p && n == sizeof(UBreakIterator *)) {
            UBreakIterator *b = *(UBreakIterator **)p;
            if (b) ubrk_close(b);
            *(UBreakIterator **)p = NULL;
        }
    }
    return 0;
}

/* segment(str) — returns an array of {segment, index, input, isWordLike?}.
   The spec returns an iterable Segments object; an array is iterable and
   indexable the same way for `for...of` and `Array.from`. */
/* Linear-scan `.containing(index)` for the Segments array.  Spec says
   non-finite/negative/out-of-range index returns undefined. */
static duk_ret_t sg_containing(duk_context *ctx)
{
    if (duk_get_top(ctx) == 0) duk_push_int(ctx, 0);
    double n = duk_to_number(ctx, 0);
    if (n != n) n = 0;
    int32_t idx = (int32_t)n;
    duk_push_this(ctx);
    if (!duk_is_array(ctx, -1)) {
        duk_push_undefined(ctx); return 1;
    }
    duk_size_t len = duk_get_length(ctx, -1);
    for (duk_size_t i = 0; i < len; i++) {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        duk_get_prop_string(ctx, -1, "index");
        int32_t s = (int32_t)duk_get_int(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, -1, "segment");
        const char *seg = duk_get_string(ctx, -1);
        int32_t e = s + (seg ? (int32_t)strlen(seg) : 0);  /* approx; for BMP correct */
        duk_pop(ctx);
        if (idx >= s && idx < e) {
            /* Return the segment data object. */
            return 1;
        }
        duk_pop(ctx);
    }
    duk_push_undefined(ctx);
    return 1;
}

static duk_ret_t sg_segment(duk_context *ctx)
{
    const char *input = duk_to_string(ctx, 0);

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_UBRK);
    duk_size_t bsz = 0;
    void *p = duk_get_buffer_data(ctx, -1, &bsz);
    if (!p || bsz != sizeof(UBreakIterator *))
        RP_TYPE_THROW(ctx, "Intl.Segmenter: segment: invalid this");
    UBreakIterator *bi = *(UBreakIterator **)p;
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, K_TYPE);
    const char *gran = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    int32_t ulen = 0;
    UChar *u = utf8_to_uchar(input, (int32_t)strlen(input), &ulen);
    if (!u) RP_THROW(ctx, "Intl.Segmenter: utf16 conv");

    UErrorCode err = U_ZERO_ERROR;
    ubrk_setText(bi, u, ulen, &err);
    if (U_FAILURE(err)) { free(u); RP_THROW(ctx, "Intl.Segmenter: setText: %s", u_errorName(err)); }

    duk_push_array(ctx);
    duk_uarridx_t arrlen = 0;
    int32_t start = ubrk_first(bi);
    int32_t end;
    int is_word = (strcmp(gran, "word") == 0);
    while ((end = ubrk_next(bi)) != UBRK_DONE) {
        duk_push_object(ctx);
        uchar_push_utf8(ctx, u + start, end - start);
        duk_put_prop_string(ctx, -2, "segment");
        /* Note: index is in UTF-16 code units (matches spec). */
        duk_push_int(ctx, start);
        duk_put_prop_string(ctx, -2, "index");
        duk_push_string(ctx, input);
        duk_put_prop_string(ctx, -2, "input");
        if (is_word) {
            int32_t status = ubrk_getRuleStatus(bi);
            duk_push_boolean(ctx, status >= UBRK_WORD_NONE_LIMIT);
            duk_put_prop_string(ctx, -2, "isWordLike");
        }
        duk_put_prop_index(ctx, -2, arrlen++);
        start = end;
    }
    free(u);
    /* Attach `.containing(index)` to the result array.  Per spec the
       return is a Segments object distinct from Array; we approximate
       by augmenting the array, which preserves iteration. */
    duk_push_c_function(ctx, sg_containing, 1);
    duk_put_prop_string(ctx, -2, "containing");
    return 1;
}

static duk_ret_t sg_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot(ctx, K_UBRK, "Segmenter");
    duk_get_prop_string(ctx, -1, K_LOC);  const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_TYPE); const char *gr  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc)); duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, gr);  duk_put_prop_string(ctx, -2, "granularity");
    return 1;
}

/* ================================================================
 * DurationFormat
 *
 * Combines NumberFormat + ListFormat per ECMA-402.  The duration is
 * an object with year/month/week/day/hour/minute/second/millisecond/
 * microsecond/nanosecond keys.  Each unit-style is:
 *   long    -> "1 hour, 30 minutes"
 *   short   -> "1 hr, 30 min"
 *   narrow  -> "1h 30m"
 *   digital -> "1:30:00"
 * Per-unit can be overridden via opts.hours = "numeric"|"2-digit"|"long"…
 * For v1 we honor `style` for narrow/digital/short/long and pass
 * "numeric" for hours when style="digital".
 * ============================================================== */

static const char *DUR_UNITS[] = {
    "years","months","weeks","days","hours","minutes",
    "seconds","milliseconds","microseconds","nanoseconds", NULL
};
static const char *DUR_UNIT_LABEL_LONG_PL[] = {
    "years","months","weeks","days","hours","minutes",
    "seconds","milliseconds","microseconds","nanoseconds"
};
static const char *DUR_UNIT_LABEL_LONG_SG[] = {
    "year","month","week","day","hour","minute",
    "second","millisecond","microsecond","nanosecond"
};
static const char *DUR_UNIT_LABEL_SHORT[] = {
    "yr","mo","wk","d","hr","min","sec","ms","μs","ns"
};
static const char *DUR_UNIT_LABEL_NARROW[] = {
    "y","mo","w","d","h","m","s","ms","μs","ns"
};

static duk_ret_t durf_construct(duk_context *ctx)
{
    if (!duk_is_constructor_call(ctx))
        RP_TYPE_THROW(ctx, "Intl.DurationFormat: must be called with new");

    const char *locale = NULL;
    if (duk_get_top(ctx) > 0 && duk_is_string(ctx, 0))
        locale = duk_get_string(ctx, 0);
    if (locale && *locale) validate_locale_tag(ctx, locale);
    if (!locale || !*locale) locale = uloc_getDefault();

    const char *style = "short";
    duk_idx_t opts_idx = (duk_get_top(ctx) >= 2) ? 1 : DUK_INVALID_INDEX;
    if (opts_idx != DUK_INVALID_INDEX) { if (duk_is_null(ctx, opts_idx)) RP_TYPE_THROW(ctx, "Intl options cannot be null"); else if (!duk_is_undefined(ctx, opts_idx) && !duk_is_object(ctx, opts_idx)) duk_to_object(ctx, opts_idx); }
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        static const char *STYLE_VALS[] = {"long","short","narrow","digital", NULL};
        static const char *UD_VALS[] = {"long","short","narrow","numeric","2-digit", NULL};
        static const char *FU_VALS[] = {"long","short","narrow","numeric", NULL};
        static const char *LM_VALS[] = {"lookup","best fit", NULL};
        if (validate_enum_opt(ctx, opts_idx, "style", STYLE_VALS, 1)) duk_pop(ctx);
        if (validate_enum_opt(ctx, opts_idx, "localeMatcher", LM_VALS, 1)) duk_pop(ctx);
        /* Per-unit display options. fractionalDigits gets numeric/2-digit
           on sub-second units only; we accept the broader list. */
        static const char *units[] = {
            "years","months","weeks","days","hours","minutes",
            "seconds","milliseconds","microseconds","nanoseconds", NULL
        };
        for (int i = 0; units[i]; i++) {
            const char *vals = (i < 6) ? "FU" : "UD";  /* not used directly */
            (void)vals;
            if (validate_enum_opt(ctx, opts_idx, units[i],
                                  i < 6 ? FU_VALS : UD_VALS, 1)) duk_pop(ctx);
        }
        /* fractionalDigits: integer 0..9 */
        duk_get_prop_string(ctx, opts_idx, "fractionalDigits");
        if (!duk_is_undefined(ctx, -1)) {
            double v = duk_to_number(ctx, -1);
            if (v != (int)v || v < 0 || v > 9)
                RP_RANGE_THROW(ctx, "fractionalDigits must be 0..9");
        }
        duk_pop(ctx);
        /* numberingSystem: non-empty */
        duk_get_prop_string(ctx, opts_idx, "numberingSystem");
        if (!duk_is_undefined(ctx, -1)) {
            const char *v = duk_to_string(ctx, -1);
            if (!*v) RP_RANGE_THROW(ctx, "Invalid numberingSystem option: empty string");
        }
        duk_pop(ctx);

        const char *s = opt_get_string(ctx, opts_idx, "style");
        if (s) { style = s; duk_pop(ctx); }
    }

    duk_push_this(ctx);
    duk_push_string(ctx, locale); duk_put_prop_string(ctx, -2, K_LOC);
    duk_push_string(ctx, style);  duk_put_prop_string(ctx, -2, K_STYLE);
    /* Stash full opts for resolvedOptions / per-unit overrides. */
    if (opts_idx != DUK_INVALID_INDEX && duk_is_object(ctx, opts_idx)) {
        duk_dup(ctx, opts_idx);
        duk_put_prop_string(ctx, -2, K_OPTS);
    }
    return 0;
}

static duk_ret_t durf_finalizer(duk_context *ctx)
{
    (void)ctx;
    return 0;
}

static duk_ret_t durf_format(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, K_STYLE, "DurationFormat", 0);
    duk_pop(ctx);
    if (!duk_is_object(ctx, 0))
        RP_TYPE_THROW(ctx, "Intl.DurationFormat.format: requires duration object");

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, K_LOC);
    const char *locale = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_STYLE);
    const char *style = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    int digital = !strcmp(style, "digital");
    int narrow  = !strcmp(style, "narrow");
    int short_  = !strcmp(style, "short");
    int long_   = !strcmp(style, "long");
    (void)narrow; (void)short_; (void)long_;

    /* For "long" the label varies with the value (1 hour vs 2 hours).
       short/narrow use the same abbreviation regardless. */
    const char *const *labels = DUR_UNIT_LABEL_SHORT;
    if (long_)   labels = DUR_UNIT_LABEL_LONG_PL;
    if (narrow)  labels = DUR_UNIT_LABEL_NARROW;

    if (digital) {
        /* Build "H:MM:SS[.fff]" using whatever combination of fields
           are present (hours/minutes/seconds). */
        int h = 0, m = 0, s = 0, ms = 0;
        duk_get_prop_string(ctx, 0, "hours");        if (duk_is_number(ctx, -1)) h  = (int)duk_get_int(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "minutes");      if (duk_is_number(ctx, -1)) m  = (int)duk_get_int(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "seconds");      if (duk_is_number(ctx, -1)) s  = (int)duk_get_int(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, 0, "milliseconds"); if (duk_is_number(ctx, -1)) ms = (int)duk_get_int(ctx, -1); duk_pop(ctx);
        char out[64];
        if (ms) snprintf(out, 64, "%d:%02d:%02d.%03d", h, m, s, ms);
        else    snprintf(out, 64, "%d:%02d:%02d",     h, m, s);
        duk_push_string(ctx, out);
        return 1;
    }

    /* Build a list of "<n> <unit>" parts and join via ListFormat. */
    duk_push_array(ctx);
    duk_uarridx_t arrlen = 0;
    UListFormatter *lf = NULL;
    UErrorCode err = U_ZERO_ERROR;
    lf = ulistfmt_openForType(locale, ULISTFMT_TYPE_UNITS,
                              narrow ? ULISTFMT_WIDTH_NARROW :
                              short_ ? ULISTFMT_WIDTH_SHORT  :
                                       ULISTFMT_WIDTH_WIDE, &err);
    UNumberFormat *nf = unum_open(UNUM_DECIMAL, NULL, 0, locale, NULL, &err);

    for (int i = 0; DUR_UNITS[i]; i++) {
        duk_get_prop_string(ctx, 0, DUR_UNITS[i]);
        int has = duk_is_number(ctx, -1);
        double v = has ? duk_get_number(ctx, -1) : 0.0;
        duk_pop(ctx);
        if (!has || v == 0.0) continue;

        UChar n_u[64]; int32_t n_l = 0;
        UErrorCode e2 = U_ZERO_ERROR;
        if (nf) n_l = unum_formatDouble(nf, v, n_u, 64, NULL, &e2);
        char ntext[64]; int32_t nbytes = 0;
        if (nf && U_SUCCESS(e2)) {
            e2 = U_ZERO_ERROR;
            u_strToUTF8(ntext, 64, &nbytes, n_u, n_l, &e2);
        } else {
            nbytes = (int32_t)snprintf(ntext, 64, "%g", v);
        }
        const char *label = labels[i];
        if (long_ && (v == 1.0 || v == -1.0))
            label = DUR_UNIT_LABEL_LONG_SG[i];
        char piece[128];
        snprintf(piece, 128, "%.*s %s", nbytes, ntext, label);
        duk_push_string(ctx, piece);
        duk_put_prop_index(ctx, -2, arrlen++);
    }
    if (arrlen == 0) {
        duk_pop(ctx);  /* discard empty array */
        duk_push_string(ctx, "");
        if (lf) ulistfmt_close(lf);
        if (nf) unum_close(nf);
        return 1;
    }

    /* Marshal array to UChar** for ulistfmt_format. */
    UChar **strs = NULL; int32_t *lens = NULL;
    int32_t count = lf_marshal_array(ctx, -1, &strs, &lens);
    duk_pop(ctx);  /* the array */

    UChar out_u[1024]; err = U_ZERO_ERROR;
    int32_t out_l = lf ? ulistfmt_format(lf, (const UChar *const *)strs, lens, count, out_u, 1024, &err)
                       : 0;
    if (U_SUCCESS(err) && lf) {
        uchar_push_utf8(ctx, out_u, out_l);
    } else if (count > 0) {
        /* Fallback: comma-join. */
        duk_push_string(ctx, ", ");
        for (int i = 0; i < count; i++) {
            char tmp[128]; int32_t tb = 0; UErrorCode e2 = U_ZERO_ERROR;
            u_strToUTF8(tmp, 128, &tb, strs[i], lens[i], &e2);
            duk_push_lstring(ctx, tmp, (duk_size_t)tb);
            if (i > 0) duk_insert(ctx, -2);
        }
        duk_join(ctx, count);
    } else {
        duk_push_string(ctx, "");
    }

    for (int i = 0; i < count; i++) free(strs[i]);
    free(strs); free(lens);
    if (lf) ulistfmt_close(lf);
    if (nf) unum_close(nf);
    return 1;
}

static duk_ret_t durf_format_to_parts(duk_context *ctx)
{
    durf_format(ctx);
    const char *s = duk_to_string(ctx, -1); duk_pop(ctx);
    duk_push_array(ctx);
    duk_push_object(ctx);
    duk_push_string(ctx, "literal"); duk_put_prop_string(ctx, -2, "type");
    duk_push_string(ctx, s);         duk_put_prop_string(ctx, -2, "value");
    duk_put_prop_index(ctx, -2, 0);
    return 1;
}

static duk_ret_t durf_resolved_options(duk_context *ctx)
{
    duk_push_this(ctx);
    require_internal_slot_kind(ctx, K_STYLE, "DurationFormat", 0);
    duk_get_prop_string(ctx, -1, K_LOC);   const char *loc = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, -1, K_STYLE); const char *st  = duk_get_string(ctx, -1); duk_pop(ctx);
    duk_pop(ctx);

    duk_push_object(ctx);
    duk_push_string(ctx, icu_to_bcp47(loc)); duk_put_prop_string(ctx, -2, "locale");
    duk_push_string(ctx, st);  duk_put_prop_string(ctx, -2, "style");
    duk_push_string(ctx, "latn"); duk_put_prop_string(ctx, -2, "numberingSystem");
    return 1;
}

/* ================================================================
 * Shared supportedLocalesOf
 *
 * Each Intl constructor exposes a static `supportedLocalesOf(locales,
 * opts?)` that returns the subset of `locales` for which the
 * constructor can produce locale-sensitive output.  ECMA-402's
 * implementation flexibility is generous; we use ICU's
 * uloc_getAvailable as the authoritative set, then BCP-47-canonicalize
 * the matches.
 * ============================================================== */

static int icu_locale_available(const char *tag)
{
    if (!tag || !*tag) return 0;
    /* Convert BCP 47 → ICU locale id for the membership check. */
    char icu[128]; UErrorCode err = U_ZERO_ERROR;
    int32_t n = uloc_forLanguageTag(tag, icu, sizeof(icu), NULL, &err);
    if (U_FAILURE(err) || n <= 0) return 0;
    /* Strip any keyword tail before comparing. */
    char *at = strchr(icu, '@'); if (at) *at = 0;
    int32_t cnt = uloc_countAvailable();
    /* Exact match first. */
    for (int32_t i = 0; i < cnt; i++) {
        const char *a = uloc_getAvailable(i);
        if (a && !strcmp(a, icu)) return 1;
    }
    /* Then language-only fallback (e.g. "fr" against "fr_FR" / "fr_CA"). */
    char lang[16]; UErrorCode e2 = U_ZERO_ERROR;
    uloc_getLanguage(icu, lang, sizeof(lang), &e2);
    if (U_SUCCESS(e2) && lang[0]) {
        size_t llen = strlen(lang);
        for (int32_t i = 0; i < cnt; i++) {
            const char *a = uloc_getAvailable(i);
            if (a && !strncmp(a, lang, llen)
                  && (a[llen] == 0 || a[llen] == '_')) return 1;
        }
    }
    return 0;
}

static duk_ret_t intl_supported_locales_of(duk_context *ctx)
{
    /* Optional 2nd arg is options — null is TypeError per ECMA-402. */
    if (duk_get_top(ctx) >= 2 && !duk_is_undefined(ctx, 1)) {
        if (duk_is_null(ctx, 1))
            RP_TYPE_THROW(ctx, "supportedLocalesOf options cannot be null");
        if (duk_is_object(ctx, 1)) {
            duk_get_prop_string(ctx, 1, "localeMatcher");
            if (!duk_is_undefined(ctx, -1)) {
                if (duk_is_null(ctx, -1))
                    RP_RANGE_THROW(ctx, "Invalid localeMatcher: null");
                const char *lm = duk_to_string(ctx, -1);
                if (strcmp(lm, "lookup") && strcmp(lm, "best fit"))
                    RP_RANGE_THROW(ctx, "Invalid localeMatcher: %s", lm);
            }
            duk_pop(ctx);
        }
    }
    /* CanonicalizeLocaleList on the first arg (string / array-like /
       coerced object).  Then filter to ICU-available locales. */
    duk_push_array(ctx);
    duk_idx_t list_idx = duk_get_top_index(ctx);
    duk_uarridx_t list_n = 0;

    if (!(duk_get_top(ctx) - 1 == 0 || duk_is_undefined(ctx, 0))) {
        if (duk_is_string(ctx, 0)) {
            push_canonical_unique(ctx, duk_get_string(ctx, 0), list_idx, &list_n);
        } else {
            if (duk_is_null(ctx, 0))
                RP_TYPE_THROW(ctx, "Cannot convert null to object");
            if (!duk_is_object(ctx, 0)) duk_to_object(ctx, 0);
            duk_get_prop_string(ctx, 0, "length");
            double lenf = duk_to_number(ctx, -1);
            duk_pop(ctx);
            if (lenf != lenf || lenf < 0) lenf = 0;
            if (lenf > 4294967295.0) lenf = 4294967295.0;
            duk_size_t len = (duk_size_t)lenf;
            for (duk_size_t i = 0; i < len; i++) {
                duk_push_uint(ctx, (duk_uint_t)i);
                if (!duk_has_prop(ctx, 0)) continue;
                duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
                if (!duk_is_string(ctx, -1) && !duk_is_object(ctx, -1)) {
                    duk_pop(ctx);
                    RP_TYPE_THROW(ctx, "Locale element must be a string or Intl.Locale");
                }
                const char *tag = NULL;
                if (duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, K_LOC)) {
                    duk_get_prop_string(ctx, -1, K_LOC);
                    tag = duk_get_string(ctx, -1);
                    push_canonical_unique(ctx, tag, list_idx, &list_n);
                    duk_pop_2(ctx);
                    continue;
                }
                tag = duk_to_string(ctx, -1);
                push_canonical_unique(ctx, tag, list_idx, &list_n);
                duk_pop(ctx);
            }
        }
    }
    /* Filter to ICU-available. */
    duk_push_array(ctx);
    duk_uarridx_t outi = 0;
    for (duk_uarridx_t i = 0; i < list_n; i++) {
        duk_get_prop_index(ctx, list_idx, i);
        const char *l = duk_get_string(ctx, -1);
        if (icu_locale_available(l)) {
            duk_put_prop_index(ctx, -2, outi++);
        } else {
            duk_pop(ctx);
        }
    }
    return 1;
}

/* ECMA-402 specifies `Intl.Collator.prototype.compare`,
   `Intl.NumberFormat.prototype.format`, and
   `Intl.DateTimeFormat.prototype.format` as accessor properties
   (getters) that return a function permanently bound to the
   instance.  Subsequent accesses on the same instance return the
   same bound function (cached on a hidden slot).
   This helper installs an accessor on the prototype that:
     - validates `this` carries the expected internal slot
     - lazily creates `unbound.bind(this)`, caches on the instance
     - returns the cached function

   The unbound function (`fn`) is provided by the caller (e.g.
   `col_compare`); it consumes `this` and its arguments. */
static __attribute__((unused)) const char *K_BOUND_FN = "\xff" "bound_fn";

static duk_ret_t bound_getter_impl(duk_context *ctx,
                                   const char *slot, duk_c_function fn,
                                   int fn_nargs, const char *err_kind)
{
    duk_push_this(ctx);
    /* Validate: must carry the internal slot (i.e. be an instance). */
    duk_get_prop_string(ctx, -1, slot);
    int has_slot = duk_is_buffer_data(ctx, -1);
    duk_pop(ctx);
    if (!has_slot)
        RP_TYPE_THROW(ctx, "Intl.%s: getter called on non-%s receiver", err_kind, err_kind);

    /* Use a hidden slot keyed by `slot` to cache per-method. */
    char cache_key[32];
    snprintf(cache_key, sizeof(cache_key), "%s_b", slot);
    duk_get_prop_string(ctx, -1, cache_key);
    if (duk_is_callable(ctx, -1)) {
        /* Already cached. */
        duk_remove(ctx, -2);
        return 1;
    }
    duk_pop(ctx);

    /* Build bound function: fn.bind(this).  Use Function.prototype.bind
       so the result is a proper bound function. */
    duk_push_c_function(ctx, fn, fn_nargs);
    duk_push_string(ctx, "bind");
    duk_dup(ctx, -3);  /* this collator/formatter */
    duk_call_prop(ctx, -3, 1);
    duk_remove(ctx, -2);  /* remove original fn */

    /* Cache on instance. */
    duk_dup_top(ctx);
    duk_put_prop_string(ctx, -3, cache_key);
    duk_remove(ctx, -2);  /* remove this */
    return 1;
}

static duk_ret_t col_compare_getter(duk_context *ctx)
    { return bound_getter_impl(ctx, K_UCOL, col_compare, 2, "Collator"); }
static duk_ret_t nf_format_getter(duk_context *ctx)
    { return bound_getter_impl(ctx, K_UNUM, nf_format, 1, "NumberFormat"); }
static duk_ret_t dtf_format_getter(duk_context *ctx)
    { return bound_getter_impl(ctx, K_UDAT, dtf_format, DUK_VARARGS, "DateTimeFormat"); }

/* Install a getter named `name` on the object at stack idx -1. */
static void install_bound_getter(duk_context *ctx, const char *name, duk_c_function getter)
{
    duk_push_string(ctx, name);
    duk_push_c_function(ctx, getter, 0);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_GETTER
                 | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
}

/* Set Symbol.toStringTag and proto.constructor on the prototype
   currently at stack top.  Stack layout assumed:
   [..., constructor, prototype].  Both are required by ECMA-402:
   - `Object.prototype.toString.call(new Intl.Foo()) === "[object Intl.Foo]"`
   - `Intl.Foo.prototype.constructor === Intl.Foo` */
static void set_to_string_tag(duk_context *ctx, const char *tag)
{
    /* Symbol.toStringTag */
    duk_get_global_string(ctx, "Symbol");
    duk_get_prop_string(ctx, -1, "toStringTag");
    duk_remove(ctx, -2);
    duk_push_string(ctx, tag);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_CONFIGURABLE
                 | DUK_DEFPROP_SET_CONFIGURABLE);
    /* proto.constructor = constructor — non-enumerable per spec. */
    duk_push_string(ctx, "constructor");
    duk_dup(ctx, -3);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE
                 | DUK_DEFPROP_HAVE_WRITABLE   | DUK_DEFPROP_SET_WRITABLE
                 | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
}

/* Walk own keys of the object at stack -1 and make each property
   non-enumerable.  Skips `length` / `prototype` / `name` (immutable
   built-ins).  ECMA-402 method properties are non-enumerable by
   default; our `duk_put_prop_string` makes them enumerable, so we
   sweep at the end. */
static void make_props_non_enumerable(duk_context *ctx)
{
    /* Get keys via Object.getOwnPropertyNames(obj). */
    duk_get_global_string(ctx, "Object");
    duk_get_prop_string(ctx, -1, "getOwnPropertyNames");
    duk_dup(ctx, -3);  /* obj */
    duk_call(ctx, 1);
    duk_remove(ctx, -2);  /* Object */
    /* stack: [..., obj, keys[]] */
    duk_size_t n = duk_get_length(ctx, -1);
    for (duk_size_t i = 0; i < n; i++) {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        const char *key = duk_get_string(ctx, -1);
        if (!key) { duk_pop(ctx); continue; }
        if (!strcmp(key, "length") || !strcmp(key, "prototype") || !strcmp(key, "name")) {
            duk_pop(ctx);
            continue;
        }
        /* key is at -1. Now set enumerable=false on obj's property. */
        duk_def_prop(ctx, -3,
                     DUK_DEFPROP_HAVE_ENUMERABLE);
        /* def_prop consumed the key. */
    }
    duk_pop(ctx);  /* keys array */
}

/* Attach `supportedLocalesOf` as a static method to the constructor
   currently at stack top.  Per spec it is writable/configurable but
   NOT enumerable. */
static void attach_supported_locales_of(duk_context *ctx)
{
    duk_push_string(ctx, "supportedLocalesOf");
    duk_push_c_function(ctx, intl_supported_locales_of, DUK_VARARGS);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_VALUE
                 | DUK_DEFPROP_HAVE_WRITABLE     | DUK_DEFPROP_SET_WRITABLE
                 | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
}

/* Set proto.constructor = ctor.  Stack layout assumed:
   [..., constructor, prototype].  After: unchanged (still
   constructor + prototype at top).  ECMA-402 expects
   `Intl.Foo.prototype.constructor === Intl.Foo`. */
static __attribute__((unused)) void
set_proto_constructor(duk_context *ctx)
{
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, "constructor");
}

/* ---------------- String.prototype.normalize (ES2015 21.1.3.12) ----------------
 *
 * Backed by ICU's UNormalizer2.  Replaces the lazy-load stub that
 * duktape's DUK_RP_USE_STRING_EXTRAS installs at heap-create time.
 * When the user calls "x".normalize() before rampart-intl is loaded,
 * the stub does require('rampart-intl') which triggers install_intl()
 * which calls install_string_normalize() below to replace the stub.
 * On every subsequent call the user goes straight to this real impl.
 *
 * Form: "NFC" (default), "NFD", "NFKC", "NFKD".  RangeError for any
 * other string per spec. */
static const UNormalizer2 *rp_get_normalizer2(const char *form, UErrorCode *err)
{
    if (form == NULL || strcmp(form, "NFC") == 0) {
        return unorm2_getNFCInstance(err);
    } else if (strcmp(form, "NFD") == 0) {
        return unorm2_getNFDInstance(err);
    } else if (strcmp(form, "NFKC") == 0) {
        return unorm2_getNFKCInstance(err);
    } else if (strcmp(form, "NFKD") == 0) {
        return unorm2_getNFKDInstance(err);
    }
    return NULL;
}

static duk_ret_t rp_string_normalize(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_to_string(ctx, -1);  /* ToString(this) per spec step 1 */
    duk_size_t in_bytes;
    const char *in_cesu = duk_get_lstring(ctx, -1, &in_bytes);

    const char *form = NULL;
    if (duk_get_top(ctx) >= 2 && !duk_is_undefined(ctx, 0)) {
        form = duk_to_string(ctx, 0);
    }

    UErrorCode err = U_ZERO_ERROR;
    const UNormalizer2 *n2 = rp_get_normalizer2(form, &err);
    if (n2 == NULL || U_FAILURE(err)) {
        return duk_range_error(ctx,
            "String.prototype.normalize: invalid form (must be NFC, NFD, NFKC, or NFKD)");
    }

    int32_t in_ulen = 0;
    UChar *in_u = utf8_to_uchar(in_cesu, (int32_t) in_bytes, &in_ulen);
    if (in_u == NULL) {
        return duk_error(ctx, DUK_ERR_ERROR,
            "String.prototype.normalize: out of memory");
    }

    /* Normalize.  Try a buffer the size of the input first; grow once
     * if ICU reports overflow.  In practice the NF* output is within a
     * small factor of the input size. */
    int32_t cap = in_ulen + 16;
    UChar *out_u = (UChar *) malloc(sizeof(UChar) * (size_t)(cap + 1));
    if (out_u == NULL) {
        free(in_u);
        return duk_error(ctx, DUK_ERR_ERROR,
            "String.prototype.normalize: out of memory");
    }

    err = U_ZERO_ERROR;
    int32_t out_ulen = unorm2_normalize(n2, in_u, in_ulen,
                                         out_u, cap, &err);
    if (err == U_BUFFER_OVERFLOW_ERROR) {
        free(out_u);
        cap = out_ulen + 16;
        out_u = (UChar *) malloc(sizeof(UChar) * (size_t)(cap + 1));
        if (out_u == NULL) {
            free(in_u);
            return duk_error(ctx, DUK_ERR_ERROR,
                "String.prototype.normalize: out of memory");
        }
        err = U_ZERO_ERROR;
        out_ulen = unorm2_normalize(n2, in_u, in_ulen,
                                      out_u, cap, &err);
    }
    free(in_u);

    if (U_FAILURE(err)) {
        free(out_u);
        return duk_error(ctx, DUK_ERR_ERROR,
            "String.prototype.normalize: ICU error %s", u_errorName(err));
    }

    uchar_push_utf8(ctx, out_u, out_ulen);
    free(out_u);
    return 1;
}

/* Replaces the lazy-load stub on String.prototype.normalize with the
 * real ICU-backed implementation.  Idempotent: re-loading rampart-intl
 * just rewrites the slot with a fresh function value pointing at the
 * same C function.  Called from install_intl().  Descriptor matches
 * spec: writable, non-enumerable, configurable. */
static void install_string_normalize(duk_context *ctx)
{
    duk_get_global_string(ctx, "String");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_push_string(ctx, "normalize");
    duk_push_c_function(ctx, rp_string_normalize, DUK_VARARGS);
    duk_def_prop(ctx, -3,
        DUK_DEFPROP_HAVE_VALUE |
        DUK_DEFPROP_HAVE_WRITABLE     | DUK_DEFPROP_WRITABLE |
        DUK_DEFPROP_HAVE_ENUMERABLE   /* enumerable: false (no flag set) */ |
        DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE |
        DUK_DEFPROP_FORCE);
    duk_pop_2(ctx);  /* prototype + String */
}

/* ---------------- Init / install ---------------- */

/* Build the Intl object and install it on globalThis.  Idempotent. */
static void install_intl(duk_context *ctx)
{
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "Intl");
    /* If something is already there (e.g. a previous load), replace
       silently to avoid surprising "Intl is read-only" failures. */
    duk_pop(ctx);

    duk_push_object(ctx); /* Intl */

    /* DateTimeFormat */
    duk_push_c_function(ctx, dtf_construct, DUK_VARARGS);
    duk_push_object(ctx); /* prototype */
    install_bound_getter(ctx, "format", dtf_format_getter);
    duk_push_c_function(ctx, dtf_format_to_parts,   DUK_VARARGS); duk_put_prop_string(ctx, -2, "formatToParts");
    duk_push_c_function(ctx, dtf_format_range,      2); duk_put_prop_string(ctx, -2, "formatRange");
    duk_push_c_function(ctx, dtf_format_range_to_parts, 2); duk_put_prop_string(ctx, -2, "formatRangeToParts");
    duk_push_c_function(ctx, dtf_resolved_options,  0);           duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, dtf_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.DateTimeFormat");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "DateTimeFormat");

    /* NumberFormat */
    duk_push_c_function(ctx, nf_construct, DUK_VARARGS);
    duk_push_object(ctx);
    install_bound_getter(ctx, "format", nf_format_getter);
    duk_push_c_function(ctx, nf_format_to_parts,  1); duk_put_prop_string(ctx, -2, "formatToParts");
    duk_push_c_function(ctx, nf_format_range,        2); duk_put_prop_string(ctx, -2, "formatRange");
    duk_push_c_function(ctx, nf_format_range_to_parts, 2); duk_put_prop_string(ctx, -2, "formatRangeToParts");
    duk_push_c_function(ctx, nf_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, nf_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.NumberFormat");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "NumberFormat");

    /* Collator */
    duk_push_c_function(ctx, col_construct, DUK_VARARGS);
    duk_push_object(ctx);
    install_bound_getter(ctx, "compare", col_compare_getter);
    duk_push_c_function(ctx, col_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, col_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.Collator");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "Collator");

    /* PluralRules */
    duk_push_c_function(ctx, plr_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, plr_select,           1); duk_put_prop_string(ctx, -2, "select");
    duk_push_c_function(ctx, plr_select_range,     2); duk_put_prop_string(ctx, -2, "selectRange");
    duk_push_c_function(ctx, plr_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, plr_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.PluralRules");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "PluralRules");

    /* RelativeTimeFormat */
    duk_push_c_function(ctx, rtf_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, rtf_format,           2); duk_put_prop_string(ctx, -2, "format");
    duk_push_c_function(ctx, rtf_format_to_parts,  2); duk_put_prop_string(ctx, -2, "formatToParts");
    duk_push_c_function(ctx, rtf_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, rtf_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.RelativeTimeFormat");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "RelativeTimeFormat");

    /* ListFormat */
    duk_push_c_function(ctx, lf_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, lf_format,           1); duk_put_prop_string(ctx, -2, "format");
    duk_push_c_function(ctx, lf_format_to_parts,  1); duk_put_prop_string(ctx, -2, "formatToParts");
    duk_push_c_function(ctx, lf_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, lf_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.ListFormat");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "ListFormat");

    /* DisplayNames */
    duk_push_c_function(ctx, dn_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, dn_of,                1); duk_put_prop_string(ctx, -2, "of");
    duk_push_c_function(ctx, dn_resolved_options,  0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, dn_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.DisplayNames");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "DisplayNames");

    /* Locale (no supportedLocalesOf — it's not a locale-sensitive
       constructor, just a locale parser). */
    duk_push_c_function(ctx, loc_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, loc_to_string, 0); duk_put_prop_string(ctx, -2, "toString");
    duk_push_c_function(ctx, loc_maximize,  0); duk_put_prop_string(ctx, -2, "maximize");
    duk_push_c_function(ctx, loc_minimize,  0); duk_put_prop_string(ctx, -2, "minimize");
    /* Accessor properties for each locale subtag. */
    for (int i = 0; LOC_FIELDS[i]; i++) {
        duk_push_string(ctx, LOC_FIELDS[i]);
        duk_push_c_function(ctx, loc_getter_by_magic, 0);
        duk_set_magic(ctx, -1, i);
        duk_def_prop(ctx, -3,
                     DUK_DEFPROP_HAVE_GETTER
                     | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
    }
    /* firstDayOfWeek + variants getters. */
    duk_push_string(ctx, "firstDayOfWeek");
    duk_push_c_function(ctx, loc_first_day_of_week, 0);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_GETTER
                 | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
    duk_push_string(ctx, "variants");
    duk_push_c_function(ctx, loc_variants, 0);
    duk_def_prop(ctx, -3,
                 DUK_DEFPROP_HAVE_GETTER
                 | DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_SET_CONFIGURABLE);
    /* get* methods (modern ECMA-402, replacing legacy attribute getters). */
    static const struct { const char *name; int magic; } LOC_METHODS[] = {
        {"getCalendars",        LM_CALENDARS},
        {"getCollations",       LM_COLLATIONS},
        {"getHourCycles",       LM_HOURCYCLES},
        {"getNumberingSystems", LM_NUMSYS},
        {"getTimeZones",        LM_TIMEZONES},
        {"getTextInfo",         LM_TEXTINFO},
        {"getWeekInfo",         LM_WEEKINFO},
        {NULL, 0}
    };
    for (int i = 0; LOC_METHODS[i].name; i++) {
        duk_push_c_function(ctx, loc_method_by_magic, 0);
        duk_set_magic(ctx, -1, LOC_METHODS[i].magic);
        duk_put_prop_string(ctx, -2, LOC_METHODS[i].name);
    }
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.Locale");
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "Locale");

    /* Segmenter */
    duk_push_c_function(ctx, sg_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, sg_segment,          1); duk_put_prop_string(ctx, -2, "segment");
    duk_push_c_function(ctx, sg_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, sg_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.Segmenter");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "Segmenter");

    /* DurationFormat */
    duk_push_c_function(ctx, durf_construct, DUK_VARARGS);
    duk_push_object(ctx);
    duk_push_c_function(ctx, durf_format,           1); duk_put_prop_string(ctx, -2, "format");
    duk_push_c_function(ctx, durf_format_to_parts,  1); duk_put_prop_string(ctx, -2, "formatToParts");
    duk_push_c_function(ctx, durf_resolved_options, 0); duk_put_prop_string(ctx, -2, "resolvedOptions");
    duk_push_c_function(ctx, durf_finalizer, 2);
    duk_set_finalizer(ctx, -2);
    make_props_non_enumerable(ctx);
    set_to_string_tag(ctx, "Intl.DurationFormat");
    /* Built-in `.prototype` is {writable:false, enumerable:false,
       configurable:false} per spec. */
    duk_push_string(ctx, "prototype");
    duk_swap_top(ctx, -2);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
    attach_supported_locales_of(ctx);
    duk_put_prop_string(ctx, -2, "DurationFormat");

    /* top-level */
    duk_push_c_function(ctx, intl_get_canonical_locales, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "getCanonicalLocales");
    duk_push_c_function(ctx, intl_supported_values_of, 1);
    duk_put_prop_string(ctx, -2, "supportedValuesOf");

    /* Make all Intl.* props non-enumerable per spec. */
    make_props_non_enumerable(ctx);

    /* Install on globalThis. */
    duk_put_prop_string(ctx, -2, "Intl");
    duk_pop(ctx); /* global */

    /* Also replace duktape's lazy-load stub on String.prototype.normalize
     * with the real ICU-backed implementation.  Doing this here means
     * any code that does `'café'.normalize('NFC')` without first
     * requiring rampart-intl will trigger the load via the stub, which
     * then comes here, and the call re-dispatches to the real method.
     * Subsequent calls go straight to the real method. */
    install_string_normalize(ctx);
}

/* Module entry: idempotent install of Intl; also returns the Intl
   object so `var Intl = require('rampart-intl')` works. */
static void rp_intl_cleanup_atexit(void *arg)
{
    (void)arg;
    u_cleanup();
}

duk_ret_t duk_open_module(duk_context *ctx)
{
    static int registered = 0;
    if (!registered)
    {
        add_exit_func(rp_intl_cleanup_atexit, NULL);
        registered = 1;
    }
    install_intl(ctx);
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "Intl");
    duk_remove(ctx, -2);
    return 1;
}
