#ifndef RP_STRING_H
#define RP_STRING_H

/* rp_string: small growable string (C99), header-only
 *
 * Usage:
 *   // in exactly ONE .c file:
 *   #define RP_STRING_IMPLEMENTATION
 *   #include "rp_string.h"
 *
 *   // elsewhere:
 *   #include "rp_string.h"
 *
 * Customization (define BEFORE including this header if desired):
 *   #define RP_STRING_API              // e.g., __declspec(dllexport) or visibility attributes
 *   #define RP_STRING_MIN_CAP 64       // minimum capacity used on first grow
 *
 *   // Allocators (default uses REMALLOC if available/defined below):
 *   #define RP_STRING_ALLOC(p, sz)     // p is (T*), set p from NULL to allocated block
 *   #define RP_STRING_REALLOC(p, sz)   // resize p to sz
 *   #define RP_STRING_FREE(p)          // free p
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RP_STRING_API
#define RP_STRING_API
#endif

typedef struct {
    char   *str;  /* owned buffer, NUL-terminated if cap>0 */
    size_t  len;  /* bytes used, excluding NUL */
    size_t  cap;  /* bytes allocated, including room for NUL */
} rp_string;

/* -------------------- Lifecycle -------------------- */

/* Create a new rp_string object. If initial_cap > 0, allocate that much (bytes). */
RP_STRING_API rp_string *rp_string_new(size_t initial_cap);

/* Clear contents (len=0) but keep allocation. */
RP_STRING_API void       rp_string_clear(rp_string *s);

/* --- rp_string *rp_string_free(rp_string *s) MACRO ---
   Free both the string buffer and the rp_string struct itself.
   Returns NULL for convenient assignment: s = rp_string_free(s); */
RP_STRING_API rp_string *rp_string_free_internal(rp_string *s);
#ifdef RP_STRING_REPORT_FREES
#include <stdio.h>
#include <string.h>
#define rp_string_free(s) ({\
    fprintf(stderr, "line %d: %lu of %lu\n",__LINE__, (s)->len, (s)->cap);\
    rp_string_free_internal((s));\
    (rp_string*)NULL;\
})
#else
#define rp_string_free(s) rp_string_free_internal((s))
#endif

/* Ensure buffer capacity >= need_cap (including NUL). (No-op if already large enough.) */
RP_STRING_API void       rp_string_reserve(rp_string *s, size_t need_cap);

/* --- char *rp_string_steal(rp_string *s) MACRO ---
   Take ownership of the internal buffer: returns s->str, sets it to NULL,
   and zeroes len/cap. Caller must free() the returned char*. */
RP_STRING_API char      *rp_string_steal_internal(rp_string *s);
#ifdef RP_STRING_REPORT_FREES

RP_STRING_API char      *rp_string_steal_wrapper(rp_string *s,  int line)
{
    fprintf(stderr, "line %d: %lu of %lu\n", line, (s)->len, (s)->cap);
    return rp_string_steal_internal(s);
}
#define rp_string_steal(s)  rp_string_steal_wrapper((s),__LINE__)

#else
#define rp_string_steal(s) rp_string_steal_internal((s))
#endif

/* Adopt a heap buffer (NUL-terminated) and optionally expand capacity by a factor.
   buf must be heap-allocated with the same allocator family used by RP_STRING_*.
   - cap: total allocated bytes for buf (>= strlen(buf)+1). Pass 0 if unknown.
   - extra_cap: multiplicative factor; if < 1.0 it is clamped to 1.0.
   Returns a new rp_string that owns 'buf' (possibly reallocated). */
RP_STRING_API rp_string *rp_string_new_adopt_with_cap(char *buf, size_t cap, double extra_cap);

/* Convenience: adopt with factor only (unknown/ignored cap). */
static inline rp_string *rp_string_new_adopt(char *buf) {
    return rp_string_new_adopt_with_cap(buf, 0, 1.0);
}

/* -------------------- Trimming (in-place, UTF-8 aware) ----------------- */
/* Trim using ASCII isspace() PLUS common Unicode space characters in UTF-8.
   No realloc is performed; content is shifted in place. */

RP_STRING_API void       rp_string_trim (rp_string *s);  /* both ends, like JS trim() */
RP_STRING_API void       rp_string_ltrim(rp_string *s);  /* left only */
RP_STRING_API void       rp_string_rtrim(rp_string *s);  /* right only */

/* -------------------- Appends (struct API) -------------------- */

RP_STRING_API int  rp_string_putc (rp_string *s, int ch);                     /* append one char */
RP_STRING_API int  rp_string_puts (rp_string *s, const char *z);              /* append NUL-terminated string */
RP_STRING_API int  rp_string_putsn(rp_string *s, const char *p, size_t n);    /* append n bytes (may contain NULs) */

RP_STRING_API int  rp_string_vappendf(rp_string *s, const char *fmt, va_list ap); /* formatted append */
RP_STRING_API int  rp_string_appendf (rp_string *s, const char *fmt, ...);

/* -------------------- Replace -------------------- */
/* Replace bytes in [begin, end) with rep.
   If preserve_nls != 0, try to preserve newline count as follows:
   - Count '\n' in clipped region and in rep.
   - If clipped > rep: append (clipped - rep) '\n' to end of rep before inserting.
   - If clipped < rep: first collapse "\n\n" -> "\n" until counts match;
     if still too many, remove single '\n' left-to-right until equal.
   Returns 0 on success, -1 on error (bad args / overflow). */
RP_STRING_API int rp_string_replace(rp_string *s, size_t begin, size_t end,
                                    const char *rep, int preserve_nls);

/* -------------------- Legacy char** shims -------------------- */

RP_STRING_API int  rp_string_putc_str (char **bufp, int ch);
RP_STRING_API int  rp_string_puts_str (char **bufp, const char *z);
RP_STRING_API int  rp_string_putsn_str(char **bufp, const char *p, size_t n);

RP_STRING_API int  rp_string_vappendf_str(char **bufp, const char *fmt, va_list ap);
RP_STRING_API int  rp_string_appendf_str (char **bufp, const char *fmt, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif


#ifdef RP_STRING_IMPLEMENTATION


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdint.h>

/* ---- Allocator hooks ---------------------------------------------------
 * Default to user's REMALLOC (realloc-with-check) if present/defined.
 * You can override by defining RP_STRING_ALLOC / RP_STRING_REALLOC / RP_STRING_FREE.
 * but such definitions should include a check as well.
 */
#ifndef REMALLOC
/* main macro for malloc uses realloc.  Set var to null to initiate */
#define REMALLOC(s, t) do{                                        \
    (s) = realloc((s), (t));                                      \
    if ((char *)(s) == (char *)NULL)                              \
    {                                                             \
        fprintf(stderr, "error: realloc(var, %d) in %s at %d\n",  \
            (int)(t), __FILE__, __LINE__);                        \
        abort();                                                  \
    }                                                             \
} while(0)
#endif

#ifndef RP_STRING_ALLOC
#define RP_STRING_ALLOC(p, size)   REMALLOC(p, size)   /* pass p=NULL for fresh malloc */
#endif
#ifndef RP_STRING_REALLOC
#define RP_STRING_REALLOC(p, size) REMALLOC(p, size)
#endif
#ifndef RP_STRING_FREE
#define RP_STRING_FREE(p)          free(p)
#endif
#ifndef RP_STRING_MIN_CAP
#define RP_STRING_MIN_CAP 64
#endif

/* ---- Internal UTF-8 + whitespace helpers ------------------------------ */

/* Decode one UTF-8 code point from s[0..n-1].
 * On success, returns number of bytes consumed (1..4) and writes *out.
 * On invalid sequence, returns 0 (treat byte as non-space elsewhere). */
static size_t rp__utf8_decode(const char *s, size_t n, uint32_t *out) {
    if (n == 0) return 0;
    const unsigned char b0 = (unsigned char)s[0];

    if (b0 < 0x80) { /* ASCII */
        *out = b0;
        return 1;
    } else if ((b0 & 0xE0) == 0xC0) { /* 2-byte */
        if (n < 2) return 0;
        unsigned char b1 = (unsigned char)s[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) |
                       (uint32_t)(b1 & 0x3F);
        if (cp < 0x80) return 0; /* overlong */
        *out = cp;
        return 2;
    } else if ((b0 & 0xF0) == 0xE0) { /* 3-byte */
        if (n < 3) return 0;
        unsigned char b1 = (unsigned char)s[1];
        unsigned char b2 = (unsigned char)s[2];
        if (((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80)) return 0;
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)(b1 & 0x3F) << 6) |
                       (uint32_t)(b2 & 0x3F);
        if (cp < 0x800) return 0; /* overlong */
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0; /* surrogate */
        *out = cp;
        return 3;
    } else if ((b0 & 0xF8) == 0xF0) { /* 4-byte */
        if (n < 4) return 0;
        unsigned char b1 = (unsigned char)s[1];
        unsigned char b2 = (unsigned char)s[2];
        unsigned char b3 = (unsigned char)s[3];
        if (((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80)) return 0;
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)(b1 & 0x3F) << 12) |
                      ((uint32_t)(b2 & 0x3F) << 6) |
                       (uint32_t)(b3 & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) return 0; /* overlong or out of range */
        *out = cp;
        return 4;
    }
    return 0;
}

/* Unicode whitespace set for trimming (UTF-8):
 *  - ASCII isspace(): 0x09..0x0D, 0x20
 *  - U+00A0 NO-BREAK SPACE
 *  - U+1680 OGHAM SPACE MARK
 *  - U+2000..U+200A EN QUAD .. HAIR SPACE
 *  - U+2028 LINE SEPARATOR
 *  - U+2029 PARAGRAPH SEPARATOR
 *  - U+202F NARROW NO-BREAK SPACE
 *  - U+205F MEDIUM MATHEMATICAL SPACE
 *  - U+3000 IDEOGRAPHIC SPACE
 *  - U+FEFF ZERO WIDTH NO-BREAK SPACE (BOM)
 */
static bool rp__is_unicode_space(uint32_t cp) {
    if (cp <= 0x7F) {
        return !!isspace((int)(unsigned char)cp);
    }
    switch (cp) {
        case 0x00A0: case 0x1680: case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default: break;
    }
    if (cp >= 0x2000 && cp <= 0x200A) return true;
    return false;
}

/* Walk forward over leading space; returns byte offset to first non-space. */
static size_t rp__utf8_ltrim_bytes(const char *s, size_t n) {
    size_t off = 0;
    while (off < n) {
        uint32_t cp; size_t k = rp__utf8_decode(s + off, n - off, &cp);
        if (k == 0) break;                 /* invalid sequence: stop trimming */
        if (!rp__is_unicode_space(cp)) break;
        off += k;
    }
    return off;
}

/* Scan and remember last non-space byte end for rtrim. */
static size_t rp__utf8_rtrim_length(const char *s, size_t n) {
    size_t i = 0, last_non_space_end = 0;
    while (i < n) {
        uint32_t cp; size_t k = rp__utf8_decode(s + i, n - i, &cp);
        if (k == 0) { last_non_space_end = i + 1; i += 1; continue; }
        if (!rp__is_unicode_space(cp)) last_non_space_end = i + k;
        i += k;
    }
    return last_non_space_end;
}

/* ---- Internal growth helper (~1.5x) ----------------------------------- */
static void rp__grow(rp_string *s, size_t need_cap) {
    if (!s) return;
    if (need_cap <= s->cap) return;

    size_t cap = s->cap ? s->cap : RP_STRING_MIN_CAP;
    while (cap < need_cap) {
        size_t inc = cap >> 1;                         /* cap/2 */
        if (cap > SIZE_MAX - inc) { cap = need_cap; break; }  /* overflow guard */
        cap += inc;
    }

    char *p = s->str;
    RP_STRING_REALLOC(p, cap);  /* REMALLOC aborts on OOM */
    s->str = p;
    s->cap = cap;
    if (s->len == 0) s->str[0] = '\0';
}

/* -------------------- Lifecycle -------------------- */

rp_string *rp_string_new(size_t initial_cap) {
    rp_string *s = NULL;
    RP_STRING_ALLOC(s, sizeof *s);
    s->str = NULL;
    s->len = 0;
    s->cap = 0;

    if (initial_cap) {
        RP_STRING_ALLOC(s->str, initial_cap);   /* NULL -> malloc */
        s->str[0] = '\0';
        s->cap = initial_cap;
    }
    return s;
}

void rp_string_clear(rp_string *s) {
    if (!s) return;
    if (s->str) s->str[0] = '\0';
    s->len = 0;
}

rp_string *rp_string_free_internal(rp_string *s) {
    if (!s) return NULL;
    RP_STRING_FREE(s->str);
    s->str = NULL;
    s->len = 0;
    s->cap = 0;
    RP_STRING_FREE(s);
    return NULL;
}

void rp_string_reserve(rp_string *s, size_t need_cap) {
    rp__grow(s, need_cap);
}

char *rp_string_steal_internal(rp_string *s) {
    if (!s) return NULL;
    char *out = s->str;
    s->str = NULL;
    s->len = 0;
    s->cap = 0;
    return out;
}

/* Adopt a heap buffer, optionally pre-grow by factor extra_cap. */
rp_string *rp_string_new_adopt_with_cap(char *buf, size_t cap, double extra_cap) {
    rp_string *s = rp_string_new(0);
    if (!buf) return s;

    size_t len = strlen(buf);
    if (cap < len + 1) cap = len + 1;

    if (!(extra_cap >= 1.0)) extra_cap = 1.0;  /* handles NaN and <1.0 */
    double want_d = (double)(len + 1) * extra_cap;
    size_t want = (want_d >= (double)SIZE_MAX) ? SIZE_MAX : (size_t)(want_d + 0.5);
    if (want < len + 1) want = len + 1;

    if (want > cap) {
        char *p = buf;
        RP_STRING_REALLOC(p, want);
        buf = p;
        cap = want;
    }

    s->str = buf;
    s->len = len;
    s->cap = cap;
    s->str[s->len] = '\0';
    return s;
}

/* -------------------- Trimming (in-place, UTF-8 aware) ----------------- */

void rp_string_ltrim(rp_string *s) {
    if (!s || !s->str || s->len == 0) return;
    size_t off = rp__utf8_ltrim_bytes(s->str, s->len);
    if (off) {
        size_t new_len = s->len - off;
        memmove(s->str, s->str + off, new_len);
        s->str[new_len] = '\0';
        s->len = new_len;
    }
}

void rp_string_rtrim(rp_string *s) {
    if (!s || !s->str || s->len == 0) return;
    size_t new_len = rp__utf8_rtrim_length(s->str, s->len);
    if (new_len != s->len) {
        s->str[new_len] = '\0';
        s->len = new_len;
    }
}

void rp_string_trim(rp_string *s) {
    if (!s || !s->str || s->len == 0) return;
    size_t off = rp__utf8_ltrim_bytes(s->str, s->len);
    if (off) {
        size_t tmp_len = s->len - off;
        memmove(s->str, s->str + off, tmp_len);
        s->str[tmp_len] = '\0';
        s->len = tmp_len;
    }
    size_t new_len = rp__utf8_rtrim_length(s->str, s->len);
    if (new_len != s->len) {
        s->str[new_len] = '\0';
        s->len = new_len;
    }
}

/* -------------------- Raw append helpers -------------------- */

static int rp__write(rp_string *s, const char *src, size_t nbytes) {
    if (!s || (!src && nbytes)) return -1;
    if (nbytes > SIZE_MAX - s->len - 1) return -1;      /* overflow guard */
    rp__grow(s, s->len + nbytes + 1);

    if (nbytes) memcpy(s->str + s->len, src, nbytes);
    s->len += nbytes;
    s->str[s->len] = '\0';
    return (int)nbytes;
}

/* -------------------- Appends (struct API) -------------------- */

int rp_string_putc(rp_string *s, int ch) {
    if (!s) return -1;
    if (1 > SIZE_MAX - s->len - 1) return -1;
    rp__grow(s, s->len + 1 + 1);
    s->str[s->len++] = (char)ch;
    s->str[s->len] = '\0';
    return 1;
}

int rp_string_puts(rp_string *s, const char *z) {
    if (!s || !z) return -1;
    return rp__write(s, z, strlen(z));
}

int rp_string_putsn(rp_string *s, const char *p, size_t n) {
    return rp__write(s, p, n);
}

int rp_string_vappendf(rp_string *s, const char *fmt, va_list ap) {
    if (!s || !fmt) return -1;

    va_list ap_count;
    va_copy(ap_count, ap);
    int n = vsnprintf(NULL, 0, fmt, ap_count);
    va_end(ap_count);
    if (n < 0) return -1;

    size_t need = (size_t)n;
    if (need > SIZE_MAX - s->len - 1) return -1;
    rp__grow(s, s->len + need + 1);

    va_list ap_write;
    va_copy(ap_write, ap);
    int wrote = vsnprintf(s->str + s->len, need + 1, fmt, ap_write);
    va_end(ap_write);
    if (wrote < 0) return -1;

    s->len += (size_t)wrote;
    return wrote;
}

int rp_string_appendf(rp_string *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = rp_string_vappendf(s, fmt, ap);
    va_end(ap);
    return r;
}

/* -------------------- Replace (with newline preservation) --------------- */

static size_t rp__count_nl(const char *p, size_t n) {
    size_t c = 0; for (size_t i = 0; i < n; ++i) if (p[i] == '\n') ++c; return c;
}

static int rp__adjust_rep_for_newlines(const char *rep, size_t rep_len,
                                       size_t nl_target,
                                       char **out, size_t *out_len)
{
    size_t nl_rep = rp__count_nl(rep ? rep : "", rep_len);
    if (nl_target == nl_rep) {
        char *buf = NULL; RP_STRING_ALLOC(buf, rep_len + 1);
        if (rep_len) memcpy(buf, rep, rep_len);
        buf[rep_len] = '\0';
        *out = buf; *out_len = rep_len; return 0;
    }

    if (nl_target > nl_rep) {
        size_t add = nl_target - nl_rep;
        size_t nlen = rep_len + add;
        char *buf = NULL; RP_STRING_ALLOC(buf, nlen + 1);
        if (rep_len) memcpy(buf, rep, rep_len);
        for (size_t i = 0; i < add; ++i) buf[rep_len + i] = '\n';
        buf[nlen] = '\0';
        *out = buf; *out_len = nlen; return 0;
    }

    /* nl_target < nl_rep: collapse pairs, then remove singles */
    size_t need_reduce = nl_rep - nl_target;

    char *tmp = NULL; RP_STRING_ALLOC(tmp, rep_len + 1);
    size_t i = 0, o = 0;
    while (i < rep_len) {
        if (need_reduce > 0 && rep[i] == '\n' && (i + 1) < rep_len && rep[i + 1] == '\n') {
            tmp[o++] = '\n';
            i += 2;
            --need_reduce;
        } else {
            tmp[o++] = rep[i++];
        }
    }
    tmp[o] = '\0';

    size_t nl_after = rp__count_nl(tmp, o);
    if (nl_after > nl_target) {
        size_t remove_single = nl_after - nl_target;
        char *tmp2 = NULL; RP_STRING_ALLOC(tmp2, o + 1);
        size_t o2 = 0;
        for (size_t j = 0; j < o; ++j) {
            if (remove_single > 0 && tmp[j] == '\n') { --remove_single; continue; }
            tmp2[o2++] = tmp[j];
        }
        tmp2[o2] = '\0';
        RP_STRING_FREE(tmp);
        *out = tmp2; *out_len = o2; return 0;
    } else {
        *out = tmp; *out_len = o; return 0;
    }
}

int rp_string_replace(rp_string *s, size_t begin, size_t end,
                      const char *rep, int preserve_nls)
{
    if (!s || !s->str) return -1;

    if (begin > s->len) begin = s->len;
    if (end   > s->len) end   = s->len;
    if (end < begin) { size_t t = begin; begin = end; end = t; }

    size_t clip_len   = (end > begin) ? (end - begin) : 0;
    size_t suffix_len = s->len - end;

    const char *rep_in = rep ? rep : "";
    size_t rep_len = strlen(rep_in);

    char   *adj = NULL;
    size_t  adj_len = 0;

    if (preserve_nls) {
        size_t nl_clip = rp__count_nl(s->str + begin, clip_len);
        rp__adjust_rep_for_newlines(rep_in, rep_len, nl_clip, &adj, &adj_len);
    } else {
        RP_STRING_ALLOC(adj, rep_len + 1);
        if (rep_len) memcpy(adj, rep_in, rep_len);
        adj[rep_len] = '\0';
        adj_len = rep_len;
    }

    if (adj_len > SIZE_MAX - (s->len - clip_len) - 1) {
        RP_STRING_FREE(adj);
        return -1; /* overflow guard */
    }
    size_t new_len = s->len - clip_len + adj_len;

    rp__grow(s, new_len + 1);  /* +1 for NUL */

    if (suffix_len) {
        size_t dst = begin + adj_len;
        size_t src = end;
        if (dst != src) memmove(s->str + dst, s->str + src, suffix_len);
    }

    if (adj_len) memcpy(s->str + begin, adj, adj_len);
    RP_STRING_FREE(adj);

    s->len = new_len;
    s->str[new_len] = '\0';
    return 0;
}

/* -------------------- Legacy char** shims -------------------- */

static void rp__wrap_legacy(char **bufp, rp_string *t) {
    t->str = *bufp;
    t->len = (t->str && t->str[0]) ? strlen(t->str) : 0;
    t->cap = t->str ? (t->len + 1) : 0;
}

int rp_string_putc_str(char **bufp, int ch) {
    if (!bufp) return -1;
    rp_string t; rp__wrap_legacy(bufp, &t);
    int r = rp_string_putc(&t, ch);
    *bufp = t.str;
    return r;
}

int rp_string_puts_str(char **bufp, const char *z) {
    if (!bufp || !z) return -1;
    rp_string t; rp__wrap_legacy(bufp, &t);
    int r = rp_string_puts(&t, z);
    *bufp = t.str;
    return r;
}

int rp_string_putsn_str(char **bufp, const char *p, size_t n) {
    if (!bufp || (!p && n)) return -1;
    rp_string t; rp__wrap_legacy(bufp, &t);
    int r = rp_string_putsn(&t, p, n);
    *bufp = t.str;
    return r;
}

int rp_string_vappendf_str(char **bufp, const char *fmt, va_list ap) {
    if (!bufp || !fmt) return -1;
    rp_string t; rp__wrap_legacy(bufp, &t);
    int r = rp_string_vappendf(&t, fmt, ap);
    *bufp = t.str;
    return r;
}

int rp_string_appendf_str(char **bufp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = rp_string_vappendf_str(bufp, fmt, ap);
    va_end(ap);
    return r;
}
#endif /*  RP_STRING_IMPLEMENTATION */
#endif /* RP_STRING_H */
