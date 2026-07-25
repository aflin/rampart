/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * rampart-url.cpp — exposes upa-url's WHATWG URL parser + UTS #46
 * IDN converter as rampart.utils.urlComponents / absUrl / toASCII /
 * toUnicode.  Thin C++ wrapper around the vendored extern/upa/.
 */

#include <string>
#include <string_view>
#include <stdexcept>
#include <cstring>
#include <optional>

#include "upa/url.h"
#include "upa/idna.h"
#include "upa/regex_engine_std.h"
#include "upa/urlpattern.h"

/* rp_urlpattern_t is templated on the regex engine.  Use std::regex
   (slower than SRELL but no extra vendored dep — already pull in
   <regex> for free via libstdc++). */
using rp_urlpattern_t = upa::urlpattern<upa::regex_engine_std>;

extern "C" {
#include "rampart.h"
}

namespace {

/* UTF-32 → UTF-8 for upa::idna::to_unicode output.  ~20 lines, avoids
   pulling in std::wstring_convert (deprecated in C++17). */
void u32_to_u8(const std::u32string& in, std::string& out) {
    out.clear();
    out.reserve(in.size() * 2);
    for (char32_t c : in) {
        if (c < 0x80u) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (c >> 6)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        } else if (c < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (c >> 12)));
            out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        } else if (c < 0x110000u) {
            out.push_back(static_cast<char>(0xF0u | (c >> 18)));
            out.push_back(static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
        }
        /* skip out-of-range */
    }
}

inline void push_sv(duk_context *ctx, std::string_view sv) {
    duk_push_lstring(ctx, sv.data(), sv.size());
}

/* CESU-8 → UTF-8 normalization.
 *
 * Duktape stores JS strings as CESU-8: supplementary-plane codepoints
 * are encoded as UTF-8 of each UTF-16 surrogate half (3 bytes each, 6
 * total).  upa-url's URL parser is strict UTF-8 and treats those bytes
 * as invalid surrogates → replaces with U+FFFD.
 *
 * This helper recognizes the 6-byte CESU-8 surrogate-pair pattern
 *   ED A0..AF xx xx ED B0..BF xx xx
 * and emits the equivalent 4-byte UTF-8 sequence so upa sees the
 * actual codepoint.  Other bytes are passed through unchanged.
 *
 * Returns a std::string by value; the caller hands its data()/size()
 * to upa as a string_view.  For ASCII-only inputs (the common case)
 * this is a single linear copy with no transformation.
 */
std::string cesu8_to_utf8(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    const auto *p = reinterpret_cast<const unsigned char *>(in.data());
    size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        if (i + 5 < n
            && p[i]   == 0xEDu
            && (p[i+1] & 0xF0u) == 0xA0u    /* high surrogate D800..DBFF */
            && (p[i+2] & 0xC0u) == 0x80u
            && p[i+3] == 0xEDu
            && (p[i+4] & 0xF0u) == 0xB0u    /* low surrogate DC00..DFFF */
            && (p[i+5] & 0xC0u) == 0x80u)
        {
            unsigned hi = 0xD800u | ((unsigned(p[i+1] & 0x0Fu) << 6) | unsigned(p[i+2] & 0x3Fu));
            unsigned lo = 0xDC00u | ((unsigned(p[i+4] & 0x0Fu) << 6) | unsigned(p[i+5] & 0x3Fu));
            unsigned cp = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
            i += 6;
        } else {
            out.push_back(static_cast<char>(p[i]));
            i++;
        }
    }
    return out;
}

/* Push the components object for a parsed url. */
void push_components(duk_context *ctx, const upa::url& u) {
    duk_push_object(ctx);

    push_sv(ctx, u.protocol());  duk_put_prop_string(ctx, -2, "protocol");
    push_sv(ctx, u.username());  duk_put_prop_string(ctx, -2, "username");
    push_sv(ctx, u.password());  duk_put_prop_string(ctx, -2, "password");
    push_sv(ctx, u.host());      duk_put_prop_string(ctx, -2, "host");
    push_sv(ctx, u.hostname());  duk_put_prop_string(ctx, -2, "hostname");
    push_sv(ctx, u.port());      duk_put_prop_string(ctx, -2, "port");

    /* numericPort — rampart extension.  Mirrors the `port` numeric
       property from the pure-JS rampart-url.js: explicit port (as int)
       if set; else the IANA default for the scheme; else null.
       upa's real_port_int() covers the six WHATWG special schemes
       (http/https/ws/wss/ftp/file); the table below extends it with
       common non-special schemes the JS version also handled. */
    {
        int p = u.real_port_int();
        if (p < 0) {
            /* Per-scheme default-port fallback for non-special schemes.
               Keep in sync with portmap in js_modules/rampart-url.js. */
            static const struct { const char *scheme; int port; } extras[] = {
                {"ssh",     22}, {"smtp",   25}, {"tftp",   69},
                {"gopher",  70}, {"sftp",  115}, {"imap",  143},
                {"imaps",  993}, {"pop3",  110}, {"pop3s", 995},
                {"ftps",   990}, {"telnets",992},
                {nullptr, 0}
            };
            std::string_view proto = u.protocol();
            if (!proto.empty() && proto.back() == ':') proto.remove_suffix(1);
            for (int i = 0; extras[i].scheme; i++) {
                if (proto == extras[i].scheme) { p = extras[i].port; break; }
            }
        }
        if (p >= 0) duk_push_int(ctx, p);
        else        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "numericPort");
    }
    push_sv(ctx, u.pathname());  duk_put_prop_string(ctx, -2, "pathname");
    push_sv(ctx, u.search());    duk_put_prop_string(ctx, -2, "search");
    push_sv(ctx, u.hash());      duk_put_prop_string(ctx, -2, "hash");
    push_sv(ctx, u.href());      duk_put_prop_string(ctx, -2, "href");

    {
        std::string o = u.origin();
        duk_push_lstring(ctx, o.data(), o.size());
        duk_put_prop_string(ctx, -2, "origin");
    }

    /* hostUnicode — rampart extension.  Display form of the hostname
       (reverses the IDN-to-ASCII conversion).  Equals hostname for
       already-ASCII hosts; differs only when input had U-label IDN. */
    {
        std::string_view hn = u.hostname();
        if (hn.empty()) {
            duk_push_lstring(ctx, "", 0);
        } else {
            std::u32string u32;
            if (upa::idna::domain_to_unicode(u32, hn.data(), hn.data() + hn.size())) {
                std::string u8;
                u32_to_u8(u32, u8);
                duk_push_lstring(ctx, u8.data(), u8.size());
            } else {
                push_sv(ctx, hn);
            }
        }
        duk_put_prop_string(ctx, -2, "hostUnicode");
    }
}

/* --- the four C functions --- */

duk_ret_t rp_url_components(duk_context *ctx) {
    duk_size_t url_len = 0, base_len = 0;
    const char *url_str = REQUIRE_LSTRING(ctx, 0, &url_len,
        "rampart.utils.urlComponents: first argument must be a string");
    const char *base_str = nullptr;
    if (duk_get_top(ctx) >= 2
        && !duk_is_undefined(ctx, 1)
        && !duk_is_null(ctx, 1))
    {
        base_str = REQUIRE_LSTRING(ctx, 1, &base_len,
            "rampart.utils.urlComponents: second argument (base) must be a string");
    }

    /* Normalize CESU-8 surrogate pairs to UTF-8 before upa sees them
       — otherwise supplementary-plane chars (🌈, 💩, ...) become 6
       replacement-character bytes. */
    std::string url_u8 = cesu8_to_utf8(std::string_view(url_str, url_len));
    std::string base_u8;
    if (base_str) base_u8 = cesu8_to_utf8(std::string_view(base_str, base_len));

    try {
        upa::url u = base_str
            ? upa::url(std::string_view(url_u8),
                       std::string_view(base_u8))
            : upa::url(std::string_view(url_u8));
        push_components(ctx, u);
    } catch (const std::exception&) {
        duk_push_undefined(ctx);
    }
    return 1;
}

/* _urlSetField(currentHref, field, value) → new href, or undefined on
 * invalid input.  Spec-compliant single-field setter via upa's
 * per-component setters. */
duk_ret_t rp_url_set_field(duk_context *ctx) {
    duk_size_t href_len, value_len;
    const char *href  = REQUIRE_LSTRING(ctx, 0, &href_len,  "_urlSetField: href required");
    const char *field = REQUIRE_STRING (ctx, 1, "_urlSetField: field required");
    /* value must come through length-aware: WHATWG URL setters preserve
     * NUL bytes (they get percent-encoded as %00), but string_view(const
     * char*) truncates at the first NUL.  Same goes for href, which may
     * contain percent-decoded internal state we need to round-trip. */
    const char *value = REQUIRE_LSTRING(ctx, 2, &value_len, "_urlSetField: value required");
    /* CESU-8 → UTF-8 so supplementary-plane chars in href or value
       reach upa as real codepoints rather than invalid surrogate
       bytes (which would get replaced with U+FFFD). */
    std::string href_u8  = cesu8_to_utf8(std::string_view(href, href_len));
    std::string value_u8 = cesu8_to_utf8(std::string_view(value, value_len));
    try {
        upa::url u{std::string_view(href_u8)};
        bool ok = false;
        std::string_view v(value_u8);
        if      (!std::strcmp(field, "href"))     ok = u.href(v);
        else if (!std::strcmp(field, "protocol")) ok = u.protocol(v);
        else if (!std::strcmp(field, "username")) ok = u.username(v);
        else if (!std::strcmp(field, "password")) ok = u.password(v);
        else if (!std::strcmp(field, "host"))     ok = u.host(v);
        else if (!std::strcmp(field, "hostname")) ok = u.hostname(v);
        else if (!std::strcmp(field, "port"))     ok = u.port(v);
        else if (!std::strcmp(field, "pathname")) ok = u.pathname(v);
        else if (!std::strcmp(field, "search"))   ok = u.search(v);
        else if (!std::strcmp(field, "hash"))     ok = u.hash(v);
        else {
            duk_push_undefined(ctx);
            return 1;
        }
        if (!ok) {
            /* upa returns false on invalid value for that field.  For most
               WHATWG URL setters that's a silent no-op — the JS caller
               will see the old href returned and won't observe failure.
               But for `href` itself, the spec says throw TypeError; the
               JS layer needs a way to detect failure, so we return null. */
            if (!std::strcmp(field, "href")) {
                duk_push_null(ctx);
                return 1;
            }
        }
        push_sv(ctx, u.href());
        return 1;
    } catch (const std::exception &) {
        duk_push_undefined(ctx);
        return 1;
    }
}

/* absUrl(base, rel, asComponents?) — resolve `rel` against `base`.
 *
 * Matches the contract in js_modules/rampart-url.js:
 *   - base is a string (the absolute URL to resolve against)
 *   - rel is a string OR an array of strings
 *   - asComponents (optional truthy) → return components object(s)
 *     instead of plain href strings
 *   - returns undefined if base is unparseable
 *   - per-rel: returns undefined for that entry if that rel is
 *     unparseable; otherwise the resolved string (or components)
 * Length-aware on both args so embedded NULs round-trip via
 * percent-encoding rather than being truncated by strlen().
 */
duk_ret_t rp_url_absolute(duk_context *ctx) {
    duk_size_t base_len;
    const char *base = REQUIRE_LSTRING(ctx, 0, &base_len,
        "rampart.utils.absUrl: first argument (base) must be a string");
    bool asComponents = duk_get_top(ctx) >= 3 && duk_to_boolean(ctx, 2);

    /* Validate base parses (so we can bail early with undefined).
       CESU-8 → UTF-8 first so supplementary-plane chars survive. */
    std::string base_u8 = cesu8_to_utf8(std::string_view(base, base_len));
    std::string_view base_sv(base_u8);
    try {
        (void)upa::url{base_sv};
    } catch (const std::exception&) {
        duk_push_undefined(ctx);
        return 1;
    }

    auto resolveOne = [&](const char *rel, duk_size_t rel_len) {
        std::string rel_u8 = cesu8_to_utf8(std::string_view(rel, rel_len));
        try {
            upa::url u{std::string_view(rel_u8), base_sv};
            if (asComponents) push_components(ctx, u);
            else              push_sv(ctx, u.href());
        } catch (const std::exception&) {
            duk_push_undefined(ctx);
        }
    };

    if (duk_is_array(ctx, 1)) {
        duk_uarridx_t n = (duk_uarridx_t)duk_get_length(ctx, 1);
        duk_push_array(ctx);
        for (duk_uarridx_t i = 0; i < n; i++) {
            duk_get_prop_index(ctx, 1, i);
            if (duk_is_string(ctx, -1)) {
                duk_size_t rl;
                const char *r = duk_get_lstring(ctx, -1, &rl);
                resolveOne(r, rl);
            } else {
                duk_push_undefined(ctx);
            }
            duk_remove(ctx, -2);                /* drop the read element */
            duk_put_prop_index(ctx, -2, i);     /* arr[i] = result */
        }
        return 1;
    }

    duk_size_t rel_len;
    const char *rel = REQUIRE_LSTRING(ctx, 1, &rel_len,
        "rampart.utils.absUrl: second argument must be a string or array of strings");
    resolveOne(rel, rel_len);
    return 1;
}

duk_ret_t rp_url_to_ascii(duk_context *ctx) {
    const char *host = REQUIRE_STRING(ctx, 0,
        "rampart.utils.toASCII: argument must be a string");

    std::string out;
    if (!upa::idna::domain_to_ascii(out, host, host + std::strlen(host))) {
        RP_THROW(ctx, "rampart.utils.toASCII: invalid IDN input '%s'", host);
    }
    duk_push_lstring(ctx, out.data(), out.size());
    return 1;
}

duk_ret_t rp_url_to_unicode(duk_context *ctx) {
    const char *host = REQUIRE_STRING(ctx, 0,
        "rampart.utils.toUnicode: argument must be a string");

    std::u32string u32;
    if (!upa::idna::domain_to_unicode(u32, host, host + std::strlen(host))) {
        RP_THROW(ctx, "rampart.utils.toUnicode: invalid IDN input '%s'", host);
    }
    std::string u8;
    u32_to_u8(u32, u8);
    duk_push_lstring(ctx, u8.data(), u8.size());
    return 1;
}

/* ====================================================================
 * URLPattern — thin wrapper around rp_urlpattern_t.
 *
 * From JS side, the WHATWG URLPattern class wraps these C++ functions:
 *   _urlPatternNew(input, baseURL?) → handle (object with hidden ptr)
 *   _urlPatternTest(handle, input, baseURL?) → boolean
 *   _urlPatternExec(handle, input, baseURL?) → result object | null
 *   _urlPatternGet(handle, component) → pattern string for that component
 *
 * Constructor accepts either:
 *   - a string  (constructor-string form, parsed by upa)
 *   - an object with per-component patterns (urlpattern_init form)
 * ==================================================================== */

#define RP_URLPAT_HIDDEN  DUK_HIDDEN_SYMBOL("urlpattern_ptr")

static duk_ret_t rp_urlpat_finalizer(duk_context *ctx) {
    if (duk_get_prop_string(ctx, 0, RP_URLPAT_HIDDEN)) {
        auto *p = static_cast<rp_urlpattern_t *>(duk_get_pointer(ctx, -1));
        delete p;
    }
    duk_pop(ctx);
    duk_push_pointer(ctx, nullptr);
    duk_put_prop_string(ctx, 0, RP_URLPAT_HIDDEN);
    return 0;
}

/* Convert a JS init object {protocol, hostname, pathname, ...} →
 * upa::urlpattern_init.  Each property is optional. */
static upa::urlpattern_init _initFromObject(duk_context *ctx, duk_idx_t idx) {
    upa::urlpattern_init init;
    auto pull = [&](const char *key, std::optional<std::string> upa::urlpattern_init::*field) {
        if (duk_get_prop_string(ctx, idx, key)) {
            if (duk_is_string(ctx, -1)) {
                init.*field = std::string(duk_get_string(ctx, -1));
            }
        }
        duk_pop(ctx);
    };
    pull("protocol", &upa::urlpattern_init::protocol);
    pull("username", &upa::urlpattern_init::username);
    pull("password", &upa::urlpattern_init::password);
    pull("hostname", &upa::urlpattern_init::hostname);
    pull("port",     &upa::urlpattern_init::port);
    pull("pathname", &upa::urlpattern_init::pathname);
    pull("search",   &upa::urlpattern_init::search);
    pull("hash",     &upa::urlpattern_init::hash);
    pull("baseURL",  &upa::urlpattern_init::base_url);
    return init;
}

/* _urlPatternNew(input, baseURL?) → handle */
duk_ret_t rp_urlpat_new(duk_context *ctx) {
    try {
        rp_urlpattern_t *p = nullptr;
        bool has_base = duk_get_top(ctx) >= 2 && duk_is_string(ctx, 1);
        if (duk_is_string(ctx, 0)) {
            const char *str = duk_get_string(ctx, 0);
            if (has_base) {
                const char *base = duk_get_string(ctx, 1);
                p = new rp_urlpattern_t(std::string_view(str), std::string_view(base));
            } else {
                p = new rp_urlpattern_t(std::string_view(str));
            }
        } else if (duk_is_object(ctx, 0)) {
            auto init = _initFromObject(ctx, 0);
            if (has_base) {
                /* init form takes baseURL as a field on init, not as
                   a second constructor arg. */
                init.base_url = std::string(duk_get_string(ctx, 1));
            }
            p = new rp_urlpattern_t(init);
        } else {
            RP_THROW(ctx, "URLPattern: first argument must be a string or object");
        }
        duk_push_object(ctx);
        duk_push_pointer(ctx, p);
        duk_put_prop_string(ctx, -2, RP_URLPAT_HIDDEN);
        duk_push_c_function(ctx, rp_urlpat_finalizer, 1);
        duk_set_finalizer(ctx, -2);
        return 1;
    } catch (const std::exception &e) {
        RP_THROW(ctx, "URLPattern: %s", e.what());
    }
    return 0; /* unreachable: RP_THROW longjmps */
}

/* _urlPatternTest(handle, input, baseURL?) → boolean */
duk_ret_t rp_urlpat_test(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, RP_URLPAT_HIDDEN);
    auto *p = static_cast<rp_urlpattern_t *>(duk_get_pointer(ctx, -1));
    duk_pop(ctx);
    if (!p) { duk_push_false(ctx); return 1; }
    try {
        bool result = false;
        if (duk_is_string(ctx, 1)) {
            const char *input = duk_get_string(ctx, 1);
            if (duk_get_top(ctx) >= 3 && duk_is_string(ctx, 2)) {
                const char *base = duk_get_string(ctx, 2);
                result = p->test(std::string_view(input), std::string_view(base));
            } else {
                result = p->test(std::string_view(input));
            }
        } else if (duk_is_object(ctx, 1)) {
            auto init = _initFromObject(ctx, 1);
            result = p->test(init);
        }
        duk_push_boolean(ctx, result);
        return 1;
    } catch (const std::exception &) {
        duk_push_false(ctx);
        return 1;
    }
}

/* Push a urlpattern_component_result as {input, groups: {...}}. */
static void _pushComponentResult(duk_context *ctx,
                                 const upa::urlpattern_component_result &cr) {
    duk_push_object(ctx);
    duk_push_lstring(ctx, cr.input.data(), cr.input.size());
    duk_put_prop_string(ctx, -2, "input");
    duk_push_object(ctx);
    for (const auto &kv : cr.groups) {
        if (kv.second.has_value()) {
            duk_push_lstring(ctx, kv.second->data(), kv.second->size());
        } else {
            duk_push_undefined(ctx);
        }
        duk_put_prop_lstring(ctx, -2, kv.first.data(), kv.first.size());
    }
    duk_put_prop_string(ctx, -2, "groups");
}

/* _urlPatternExec(handle, input, baseURL?) → result | null */
duk_ret_t rp_urlpat_exec(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, RP_URLPAT_HIDDEN);
    auto *p = static_cast<rp_urlpattern_t *>(duk_get_pointer(ctx, -1));
    duk_pop(ctx);
    if (!p) { duk_push_null(ctx); return 1; }
    try {
        std::optional<upa::urlpattern_result> res;
        if (duk_is_string(ctx, 1)) {
            const char *input = duk_get_string(ctx, 1);
            if (duk_get_top(ctx) >= 3 && duk_is_string(ctx, 2)) {
                const char *base = duk_get_string(ctx, 2);
                res = p->exec(std::string_view(input), std::string_view(base));
            } else {
                res = p->exec(std::string_view(input));
            }
        } else if (duk_is_object(ctx, 1)) {
            auto init = _initFromObject(ctx, 1);
            res = p->exec(init);
        }
        if (!res.has_value()) {
            duk_push_null(ctx);
            return 1;
        }
        duk_push_object(ctx);
        _pushComponentResult(ctx, res->protocol); duk_put_prop_string(ctx, -2, "protocol");
        _pushComponentResult(ctx, res->username); duk_put_prop_string(ctx, -2, "username");
        _pushComponentResult(ctx, res->password); duk_put_prop_string(ctx, -2, "password");
        _pushComponentResult(ctx, res->hostname); duk_put_prop_string(ctx, -2, "hostname");
        _pushComponentResult(ctx, res->port);     duk_put_prop_string(ctx, -2, "port");
        _pushComponentResult(ctx, res->pathname); duk_put_prop_string(ctx, -2, "pathname");
        _pushComponentResult(ctx, res->search);   duk_put_prop_string(ctx, -2, "search");
        _pushComponentResult(ctx, res->hash);     duk_put_prop_string(ctx, -2, "hash");
        return 1;
    } catch (const std::exception &) {
        duk_push_null(ctx);
        return 1;
    }
}

/* _urlPatternHasRegExpGroups(handle) → boolean
 * Returns whether any of the pattern's components contain a regex
 * group (parenthesized regex), per WHATWG URLPattern API. */
duk_ret_t rp_urlpat_has_regexp_groups(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, RP_URLPAT_HIDDEN);
    auto *p = static_cast<rp_urlpattern_t *>(duk_get_pointer(ctx, -1));
    duk_pop(ctx);
    if (!p) { duk_push_false(ctx); return 1; }
    duk_push_boolean(ctx, p->has_regexp_groups());
    return 1;
}

/* _urlPatternGet(handle, component) → pattern string */
duk_ret_t rp_urlpat_get(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, RP_URLPAT_HIDDEN);
    auto *p = static_cast<rp_urlpattern_t *>(duk_get_pointer(ctx, -1));
    duk_pop(ctx);
    if (!p) { duk_push_string(ctx, ""); return 1; }
    const char *which = REQUIRE_STRING(ctx, 1, "_urlPatternGet: component name required");
    std::string_view sv;
    if      (!std::strcmp(which, "protocol")) sv = p->get_protocol();
    else if (!std::strcmp(which, "username")) sv = p->get_username();
    else if (!std::strcmp(which, "password")) sv = p->get_password();
    else if (!std::strcmp(which, "hostname")) sv = p->get_hostname();
    else if (!std::strcmp(which, "port"))     sv = p->get_port();
    else if (!std::strcmp(which, "pathname")) sv = p->get_pathname();
    else if (!std::strcmp(which, "search"))   sv = p->get_search();
    else if (!std::strcmp(which, "hash"))     sv = p->get_hash();
    else { duk_push_string(ctx, ""); return 1; }
    push_sv(ctx, sv);
    return 1;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------
 * C-linkage helpers for other modules (currently rampart-curl.c) to
 * canonicalize URLs through upa without going through duktape.
 * Both return malloc()'d strings the caller must free().
 * ------------------------------------------------------------------ */
extern "C" char *rp_url_canonicalize(const char *href, size_t len) {
    try {
        std::string href_u8 = cesu8_to_utf8(std::string_view(href, len));
        upa::url u{std::string_view(href_u8)};
        std::string_view h = u.href();
        char *out = (char *)malloc(h.size() + 1);
        if (!out) return NULL;
        memcpy(out, h.data(), h.size());
        out[h.size()] = '\0';
        return out;
    } catch (const std::exception &) {
        return NULL;
    }
}

/* Returns a new URL string with the hostname converted from punycode
 * back to Unicode (UTS #46 ToUnicode).  Other URL components are
 * preserved verbatim.  If the hostname is already Unicode or non-IDN,
 * the result is byte-identical to the input.  Returns NULL on parse
 * failure. */
extern "C" char *rp_url_host_to_unicode(const char *href, size_t len) {
    try {
        upa::url u{std::string_view(href, len)};
        std::string_view hostname = u.hostname();
        if (hostname.empty()) {
            /* No hostname to translate; just dup the canonical href. */
            std::string_view h = u.href();
            char *out = (char *)malloc(h.size() + 1);
            if (!out) return NULL;
            memcpy(out, h.data(), h.size());
            out[h.size()] = '\0';
            return out;
        }
        std::u32string u32;
        if (!upa::idna::domain_to_unicode(u32, hostname.data(),
                                          hostname.data() + hostname.size())) {
            /* Can't decode → just return a copy of the canonical href. */
            std::string_view h = u.href();
            char *out = (char *)malloc(h.size() + 1);
            if (!out) return NULL;
            memcpy(out, h.data(), h.size());
            out[h.size()] = '\0';
            return out;
        }
        std::string unicodeHost;
        u32_to_u8(u32, unicodeHost);

        /* Splice unicodeHost into the canonical href in place of the
         * ASCII hostname.  Use u.href() so we work off the canonical
         * serialization where positions are well-defined. */
        std::string_view canonical = u.href();
        /* hostname() returns a view into the URL's internal storage;
         * find its byte offset within href to know where to splice. */
        const char *hostBegin = hostname.data();
        const char *hrefBegin = canonical.data();
        /* If the views aren't from the same buffer (shouldn't happen
         * with upa, but be defensive), do a byte search. */
        size_t hostOff;
        if (hostBegin >= hrefBegin && hostBegin <= hrefBegin + canonical.size()) {
            hostOff = (size_t)(hostBegin - hrefBegin);
        } else {
            size_t pos = canonical.find(hostname);
            if (pos == std::string_view::npos) {
                /* Fall back to canonical. */
                char *out = (char *)malloc(canonical.size() + 1);
                if (!out) return NULL;
                memcpy(out, canonical.data(), canonical.size());
                out[canonical.size()] = '\0';
                return out;
            }
            hostOff = pos;
        }
        std::string result;
        result.reserve(canonical.size() - hostname.size() + unicodeHost.size());
        result.append(canonical.data(), hostOff);
        result.append(unicodeHost);
        result.append(canonical.data() + hostOff + hostname.size(),
                      canonical.size() - hostOff - hostname.size());
        char *out = (char *)malloc(result.size() + 1);
        if (!out) return NULL;
        memcpy(out, result.data(), result.size());
        out[result.size()] = '\0';
        return out;
    } catch (const std::exception &) {
        return NULL;
    }
}

/* Entry point called from register.c.  Adds the four functions to
   rampart.utils.  Mirrors the pattern used by duk_printf_init. */
extern "C" void duk_rp_url_init(duk_context *ctx) {
    if (!duk_get_global_string(ctx, "rampart")) {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_global_string(ctx, "rampart");
        duk_get_global_string(ctx, "rampart");
    }
    if (!duk_get_prop_string(ctx, -1, "utils")) {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_dup_top(ctx);
        duk_put_prop_string(ctx, -3, "utils");
    }

    duk_push_c_function(ctx, rp_url_components, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "urlComponents");

    duk_push_c_function(ctx, rp_url_absolute, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "absUrl");

    duk_push_c_function(ctx, rp_url_to_ascii, 1);
    duk_put_prop_string(ctx, -2, "toASCII");

    duk_push_c_function(ctx, rp_url_to_unicode, 1);
    duk_put_prop_string(ctx, -2, "toUnicode");

    duk_push_c_function(ctx, rp_url_set_field, 3);
    duk_put_prop_string(ctx, -2, "_urlSetField");

    /* URLPattern wrapper around rp_urlpattern_t.  The WHATWG class
       lives in rampart-whatwg's install JS, built on these. */
    duk_push_c_function(ctx, rp_urlpat_new,  DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "_urlPatternNew");
    duk_push_c_function(ctx, rp_urlpat_test, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "_urlPatternTest");
    duk_push_c_function(ctx, rp_urlpat_exec, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "_urlPatternExec");
    duk_push_c_function(ctx, rp_urlpat_get,  2);
    duk_put_prop_string(ctx, -2, "_urlPatternGet");
    duk_push_c_function(ctx, rp_urlpat_has_regexp_groups, 1);
    duk_put_prop_string(ctx, -2, "_urlPatternHasRegExpGroups");

    duk_pop_2(ctx); /* utils, rampart */
}
