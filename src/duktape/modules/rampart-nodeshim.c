/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * rampart-nodeshim: Node.js core-module compatibility shim.
 *
 * Returns a single object whose properties are node's built-in modules:
 *     var n = require('rampart-nodeshim');
 *     n.path.join('a','b');
 *     n.fs.readFileSync('f');
 *
 * Per-submodule init functions populate each slot. This scaffold pushes
 * empty placeholders for every planned submodule so subsequent commits
 * fill in one slot at a time without further structural edits.
 */

#include "rampart.h"
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <signal.h>          /* kill(2) — required by ISO C99+; glibc was lenient, macOS clang is not */
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>        /* waitpid(2), WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG */
#include <fcntl.h>           /* O_CLOEXEC, fcntl */
#include <netdb.h>
#include <time.h>

/* Platform/arch strings — used by os and process submodules. */
#if defined(__linux__)
#  define NS_PLATFORM "linux"
#elif defined(__APPLE__)
#  define NS_PLATFORM "darwin"
#elif defined(__FreeBSD__)
#  define NS_PLATFORM "freebsd"
#elif defined(__OpenBSD__)
#  define NS_PLATFORM "openbsd"
#elif defined(__CYGWIN__)
#  define NS_PLATFORM "cygwin"
#elif defined(_WIN32)
#  define NS_PLATFORM "win32"
#else
#  define NS_PLATFORM "unknown"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#  define NS_ARCH "x64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define NS_ARCH "arm64"
#elif defined(__arm__) || defined(_M_ARM)
#  define NS_ARCH "arm"
#elif defined(__i386__) || defined(_M_IX86)
#  define NS_ARCH "ia32"
#elif defined(__ppc64__) || defined(__PPC64__)
#  define NS_ARCH "ppc64"
#elif defined(__s390x__)
#  define NS_ARCH "s390x"
#elif defined(__riscv) && (__riscv_xlen == 64)
#  define NS_ARCH "riscv64"
#else
#  define NS_ARCH "unknown"
#endif

#ifdef __APPLE__

#include <util.h>
#include <sys/ioctl.h>
// execvpe polyfill
int execvpe(const char *program, char **argv, char **envp)
{
    char **saved = environ;
    int rc;
    environ = envp;
    rc = execvp(program, argv);
    environ = saved;
    return rc;
}

#endif

/* Per-submodule init prototypes. Each pushes a fully-built object onto
   the stack and returns. Stubs below; real implementations land in
   their own commits. */
static void nodeshim_init_path(duk_context *ctx);
static void nodeshim_init_fs(duk_context *ctx);
static void nodeshim_init_crypto(duk_context *ctx);
static void nodeshim_init_os(duk_context *ctx);
static void nodeshim_init_process(duk_context *ctx);
static void nodeshim_init_buffer(duk_context *ctx);
static void nodeshim_init_events(duk_context *ctx);
static void nodeshim_init_util(duk_context *ctx);
static void nodeshim_init_querystring(duk_context *ctx);
static void nodeshim_init_url(duk_context *ctx);
static void nodeshim_init_assert(duk_context *ctx);
static void nodeshim_init_timers(duk_context *ctx);
static void nodeshim_init_string_decoder(duk_context *ctx);
static void nodeshim_init_stream(duk_context *ctx);
static void nodeshim_init_module(duk_context *ctx);
static void nodeshim_init_console(duk_context *ctx);
static void nodeshim_init_perf_hooks(duk_context *ctx);
static void nodeshim_init_dns(duk_context *ctx);
static void nodeshim_init_zlib(duk_context *ctx);
static void nodeshim_init_worker_threads(duk_context *ctx);
static void nodeshim_init_tty(duk_context *ctx);
static void nodeshim_init_http(duk_context *ctx);
static void nodeshim_init_https(duk_context *ctx);
static void nodeshim_init_net(duk_context *ctx);
static void nodeshim_init_tls(duk_context *ctx);
static void nodeshim_init_readline(duk_context *ctx);
static void nodeshim_init_child_process(duk_context *ctx);
static void nodeshim_init_vm(duk_context *ctx);
static void nodeshim_init_repl(duk_context *ctx);

/* ============================================================
 * path
 * ------------------------------------------------------------
 * POSIX semantics ported from node's lib/path.js, implemented
 * natively in C (no inline JS) for performance — path methods
 * are on every require/file-open hot path.
 * win32 is currently aliased to posix; real win32 logic later.
 * ============================================================ */

/* --- node-style TypeError helpers --- */

static const char *ns_type_name(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_null(ctx, idx))     return "null";
    if (duk_is_array(ctx, idx))    return "object";
    if (duk_is_boolean(ctx, idx))  return "boolean";
    if (duk_is_number(ctx, idx))   return "number";
    if (duk_is_string(ctx, idx))   return "string";
    if (duk_is_undefined(ctx, idx))return "undefined";
    if (duk_is_function(ctx, idx)) return "function";
    if (duk_is_object(ctx, idx))   return "object";
    return "unknown";
}

/* Build a "Received ..." suffix matching node test/common/index.js
   invalidArgTypeHelper. Returned string is on top of stack; caller
   must remove or consume. */
static void ns_push_received_suffix(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_null(ctx, idx)) {
        duk_push_string(ctx, " Received null");
        return;
    }
    if (duk_is_undefined(ctx, idx)) {
        duk_push_string(ctx, " Received undefined");
        return;
    }
    if (duk_is_function(ctx, idx)) {
        const char *fname = "";
        if (duk_get_prop_string(ctx, idx, "name") && duk_is_string(ctx, -1))
            fname = duk_get_string(ctx, -1);
        duk_push_sprintf(ctx, " Received function %s", fname);
        duk_remove(ctx, -2);
        return;
    }
    if (duk_is_object(ctx, idx)) {
        const char *cname = NULL;
        if (duk_get_prop_string(ctx, idx, "constructor")) {
            if (duk_get_prop_string(ctx, -1, "name") && duk_is_string(ctx, -1))
                cname = duk_get_string(ctx, -1);
            if (cname && *cname) {
                duk_push_sprintf(ctx, " Received an instance of %s", cname);
                duk_remove(ctx, -2); duk_remove(ctx, -2);
                return;
            }
            duk_pop_2(ctx);
        } else {
            duk_pop(ctx);
        }
        duk_push_string(ctx, " Received [object Object]");
        return;
    }
    /* primitive: ' Received type <typeof> (<inspected>)' */
    const char *t = ns_type_name(ctx, idx);
    duk_size_t inspect_buf_cap = 64;
    char inspect_buf[64];
    if (duk_is_string(ctx, idx)) {
        const char *s = duk_get_string(ctx, idx);
        size_t slen = strlen(s);
        if (slen + 2 < inspect_buf_cap) {
            snprintf(inspect_buf, sizeof(inspect_buf), "'%s'", s);
        } else {
            snprintf(inspect_buf, sizeof(inspect_buf), "'%.22s...", s);
            strcat(inspect_buf, "'");
        }
    } else {
        duk_dup(ctx, idx);
        duk_to_string(ctx, -1);
        const char *s = duk_get_string(ctx, -1);
        snprintf(inspect_buf, sizeof(inspect_buf), "%.28s", s);
        duk_pop(ctx);
    }
    duk_push_sprintf(ctx, " Received type %s (%s)", t, inspect_buf);
}

/* Throw new TypeError("...").code = 'ERR_INVALID_ARG_TYPE' */
static duk_ret_t ns_throw_invalid_arg_type(duk_context *ctx, const char *argname,
                                            const char *expected, duk_idx_t idx)
{
    ns_push_received_suffix(ctx, idx);
    const char *suffix = duk_get_string(ctx, -1);
    duk_get_global_string(ctx, "TypeError");
    duk_push_sprintf(ctx, "The \"%s\" argument must be of type %s.%s",
                     argname, expected, suffix);
    duk_new(ctx, 1);
    duk_push_string(ctx, "ERR_INVALID_ARG_TYPE");
    duk_put_prop_string(ctx, -2, "code");
    duk_remove(ctx, -2); /* remove suffix string under the error */
    duk_throw(ctx);
    return 0; /* unreachable */
}

static const char *ns_require_string(duk_context *ctx, duk_idx_t idx,
                                      const char *argname, duk_size_t *outlen)
{
    if (!duk_is_string(ctx, idx))
        ns_throw_invalid_arg_type(ctx, argname, "string", idx);
    return duk_get_lstring(ctx, idx, outlen);
}

/* --- tiny growable string buffer --- */
typedef struct { char *buf; size_t len; size_t cap; } ns_sb;

static int ns_sb_grow(ns_sb *s, size_t need)
{
    if (s->cap >= need) return 0;
    size_t nc = s->cap ? s->cap : 64;
    while (nc < need) nc *= 2;
    char *nb = realloc(s->buf, nc);
    if (!nb) return -1;
    s->buf = nb;
    s->cap = nc;
    return 0;
}
static int ns_sb_append(ns_sb *s, const char *p, size_t n)
{
    if (ns_sb_grow(s, s->len + n) != 0) return -1;
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    return 0;
}
static int ns_sb_appendc(ns_sb *s, char c) { return ns_sb_append(s, &c, 1); }

/* Push current working directory as a string on the stack and return it.
   Prefers `globalThis.process.cwd()` (matches node; lets tests monkey-patch);
   falls back to getcwd(). Caller must duk_pop the result when done. Throws on
   failure. */
static const char *ns_push_cwd(duk_context *ctx, duk_size_t *out_plen)
{
    duk_get_global_string(ctx, "process");
    if (duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "cwd");
        if (duk_is_function(ctx, -1)) {
            duk_call(ctx, 0);
            duk_remove(ctx, -2);  /* drop process; leave result on stack */
            if (duk_is_string(ctx, -1)) {
                return duk_get_lstring(ctx, -1, out_plen);
            }
            duk_pop(ctx);  /* non-string result, fall through */
        } else {
            duk_pop_2(ctx);
        }
    } else {
        duk_pop(ctx);
    }
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL)
        RP_THROW(ctx, "path: getcwd failed: %s", strerror(errno));
    duk_push_string(ctx, buf);
    return duk_get_lstring(ctx, -1, out_plen);
}

/* ns_to_lowercase: Unicode-aware lowercase via duktape's String.prototype.toLowerCase.
   Returns a fresh malloc'd null-terminated buffer + byte length via *outlen.
   Returns NULL on OOM (caller throws). Note: byte length may differ from
   input even when char count doesn't, e.g. `İ` (U+0130, 2 UTF-8 bytes) →
   `i̇` (1 + 2 UTF-8 bytes). Callers that compare by index must
   either restrict to inputs where lengths match, or split into segments. */
static char *ns_to_lowercase(duk_context *ctx, const char *s, size_t slen, size_t *outlen)
{
    duk_push_lstring(ctx, s, slen);
    duk_push_string(ctx, "toLowerCase");
    duk_call_prop(ctx, -2, 0);
    duk_size_t lc_len = 0;
    const char *lc = duk_get_lstring(ctx, -1, &lc_len);
    char *out = NULL;
    if (lc) {
        out = malloc(lc_len + 1);
        if (out) {
            memcpy(out, lc, lc_len);
            out[lc_len] = '\0';
        }
    }
    duk_pop_2(ctx);  /* lowercased + original */
    *outlen = lc_len;
    return out;
}

/* Separator predicates */
static int path_is_posix_sep(int c) { return c == '/'; }
static int path_is_win32_sep(int c) { return c == '/' || c == '\\'; }

/* normalizeString — node's lib/path.js core, parameterized by output
   separator + isPathSeparator predicate so posix and win32 share it.
   Returns malloc'd buffer + length via *outlen; caller frees.
   On allocation failure returns NULL (callers throw). */
static char *path_normalize_string(const char *path, size_t plen,
                                    int allow_above_root,
                                    char separator,
                                    int (*is_sep)(int),
                                    size_t *outlen)
{
    ns_sb res = {0};
    size_t last_seg_len = 0;
    long last_slash = -1;
    int dots = 0;
    int code = 0;

    for (size_t i = 0; i <= plen; ++i) {
        if (i < plen) code = (unsigned char)path[i];
        else if (is_sep(code)) break;
        else code = separator;

        if (is_sep(code)) {
            if ((long)i - 1 == last_slash || dots == 1) {
                /* noop: consecutive slash or "." segment */
            } else if (dots == 2) {
                if (res.len < 2 || last_seg_len != 2 ||
                    res.buf[res.len - 1] != '.' || res.buf[res.len - 2] != '.') {
                    if (res.len > 2) {
                        long lsi = -1;
                        for (long j = (long)res.len - 1; j >= 0; --j)
                            if (res.buf[j] == separator) { lsi = j; break; }
                        if (lsi == -1) {
                            res.len = 0;
                            last_seg_len = 0;
                        } else {
                            res.len = (size_t)lsi;
                            long sl2 = -1;
                            for (long j = (long)res.len - 1; j >= 0; --j)
                                if (res.buf[j] == separator) { sl2 = j; break; }
                            last_seg_len = res.len - 1 - (size_t)sl2;
                        }
                        last_slash = (long)i;
                        dots = 0;
                        continue;
                    } else if (res.len != 0) {
                        res.len = 0;
                        last_seg_len = 0;
                        last_slash = (long)i;
                        dots = 0;
                        continue;
                    }
                }
                if (allow_above_root) {
                    if (res.len > 0) {
                        char buf3[3] = {separator, '.', '.'};
                        if (ns_sb_append(&res, buf3, 3) != 0) goto oom;
                    } else {
                        if (ns_sb_append(&res, "..", 2) != 0) goto oom;
                    }
                    last_seg_len = 2;
                }
            } else {
                size_t seg_start = (size_t)(last_slash + 1);
                size_t seg_len = i - seg_start;
                if (res.len > 0) {
                    if (ns_sb_appendc(&res, separator) != 0) goto oom;
                }
                if (ns_sb_append(&res, path + seg_start, seg_len) != 0) goto oom;
                last_seg_len = seg_len;
            }
            last_slash = (long)i;
            dots = 0;
        } else if (code == '.' && dots != -1) {
            ++dots;
        } else {
            dots = -1;
        }
    }
    *outlen = res.len;
    return res.buf;  /* may be NULL if res.len == 0 — caller handles */
oom:
    free(res.buf);
    *outlen = 0;
    return NULL;
}

/* Posix-flavour wrapper kept for call-site clarity. */
static char *path_normalize_string_posix(const char *path, size_t plen,
                                          int allow_above_root, size_t *outlen)
{
    return path_normalize_string(path, plen, allow_above_root, '/',
                                  path_is_posix_sep, outlen);
}

/* path.normalize(path) */
static duk_ret_t path_normalize(duk_context *ctx)
{
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);
    if (plen == 0) { duk_push_string(ctx, "."); return 1; }
    int is_absolute = (path[0] == '/');
    int trailing_sep = (path[plen - 1] == '/');

    size_t rlen = 0;
    char *r = path_normalize_string_posix(path, plen, !is_absolute, &rlen);
    if (rlen == 0 && r == NULL && plen != 0) {
        /* sb never allocated — represents empty result, not OOM (we only
           jump to oom *after* successful allocation). Treat as empty. */
    }

    if (rlen == 0) {
        free(r);
        if (is_absolute) { duk_push_string(ctx, "/"); return 1; }
        duk_push_string(ctx, trailing_sep ? "./" : ".");
        return 1;
    }
    /* Compose final: [is_absolute "/"] + r + [trailing_sep "/"] */
    ns_sb out = {0};
    if (is_absolute && ns_sb_appendc(&out, '/') != 0) goto oom;
    if (ns_sb_append(&out, r, rlen) != 0) goto oom;
    if (trailing_sep && ns_sb_appendc(&out, '/') != 0) goto oom;
    free(r);
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
oom:
    free(r); free(out.buf);
    RP_THROW(ctx, "path.normalize: out of memory");
    return 0;
}

/* path.isAbsolute(path) */
static duk_ret_t path_is_absolute(duk_context *ctx)
{
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);
    duk_push_boolean(ctx, plen > 0 && path[0] == '/');
    return 1;
}

/* path.join(...paths) */
static duk_ret_t path_join(duk_context *ctx)
{
    duk_idx_t n = duk_get_top(ctx);
    if (n == 0) { duk_push_string(ctx, "."); return 1; }

    ns_sb joined = {0};
    int any = 0;
    for (duk_idx_t i = 0; i < n; i++) {
        duk_size_t alen;
        const char *arg = ns_require_string(ctx, i, "path", &alen);
        if (alen == 0) continue;
        if (any) {
            if (ns_sb_appendc(&joined, '/') != 0) goto oom;
        }
        if (ns_sb_append(&joined, arg, alen) != 0) goto oom;
        any = 1;
    }
    if (!any) {
        free(joined.buf);
        duk_push_string(ctx, ".");
        return 1;
    }
    /* normalize joined */
    int is_absolute = (joined.buf[0] == '/');
    int trailing_sep = (joined.buf[joined.len - 1] == '/');
    size_t rlen = 0;
    char *r = path_normalize_string_posix(joined.buf, joined.len, !is_absolute, &rlen);
    free(joined.buf);
    if (rlen == 0) {
        free(r);
        if (is_absolute) { duk_push_string(ctx, "/"); return 1; }
        duk_push_string(ctx, trailing_sep ? "./" : ".");
        return 1;
    }
    ns_sb out = {0};
    if (is_absolute && ns_sb_appendc(&out, '/') != 0) goto oom2;
    if (ns_sb_append(&out, r, rlen) != 0) goto oom2;
    if (trailing_sep && ns_sb_appendc(&out, '/') != 0) goto oom2;
    free(r);
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
oom:
    free(joined.buf);
    RP_THROW(ctx, "path.join: out of memory");
    return 0;
oom2:
    free(r); free(out.buf);
    RP_THROW(ctx, "path.join: out of memory");
    return 0;
}

/* path.resolve(...paths) */
static duk_ret_t path_resolve(duk_context *ctx)
{
    duk_idx_t n = duk_get_top(ctx);
    ns_sb resolved = {0};
    int resolved_absolute = 0;

    /* Walk right-to-left until we have an absolute path */
    for (duk_idx_t i = n - 1; i >= -1 && !resolved_absolute; i--) {
        const char *p;
        duk_size_t plen;
        int did_push = 0;
        if (i >= 0) {
            p = ns_require_string(ctx, i, "path", &plen);
            if (plen == 0) continue;
        } else {
            p = ns_push_cwd(ctx, &plen);
            did_push = 1;
        }
        /* prepend: resolved = p + '/' + resolved */
        ns_sb tmp = {0};
        if (ns_sb_append(&tmp, p, plen) != 0) goto oom;
        if (ns_sb_appendc(&tmp, '/') != 0) goto oom;
        if (ns_sb_append(&tmp, resolved.buf, resolved.len) != 0) goto oom;
        free(resolved.buf);
        resolved = tmp;
        resolved_absolute = (plen > 0 && p[0] == '/');
        if (did_push) duk_pop(ctx);
    }

    size_t rlen = 0;
    char *r = path_normalize_string_posix(resolved.buf, resolved.len, !resolved_absolute, &rlen);
    free(resolved.buf);

    if (resolved_absolute) {
        ns_sb out = {0};
        if (ns_sb_appendc(&out, '/') != 0) goto oom2;
        if (rlen > 0 && ns_sb_append(&out, r, rlen) != 0) goto oom2;
        free(r);
        duk_push_lstring(ctx, out.buf, out.len);
        free(out.buf);
        return 1;
    }
    if (rlen > 0) {
        duk_push_lstring(ctx, r, rlen);
        free(r);
        return 1;
    }
    free(r);
    duk_push_string(ctx, ".");
    return 1;
oom:
    free(resolved.buf);
    RP_THROW(ctx, "path.resolve: out of memory");
    return 0;
oom2:
    free(r);
    RP_THROW(ctx, "path.resolve: out of memory");
    return 0;
}

/* path.relative(from, to) */
static duk_ret_t path_relative(duk_context *ctx)
{
    /* Validate both first */
    (void)ns_require_string(ctx, 0, "from", NULL);
    (void)ns_require_string(ctx, 1, "to", NULL);

    if (duk_strict_equals(ctx, 0, 1)) {
        duk_push_string(ctx, "");
        return 1;
    }
    /* Resolve from and to */
    duk_push_c_function(ctx, path_resolve, DUK_VARARGS);
    duk_dup(ctx, 0);
    duk_call(ctx, 1);
    duk_size_t fromLen;
    const char *fromBuf = duk_get_lstring(ctx, -1, &fromLen);
    /* Keep this string referenced by leaving on stack; we'll dup to a local */
    char *from = malloc(fromLen + 1);
    if (!from) { duk_pop(ctx); RP_THROW(ctx, "path.relative: oom"); }
    memcpy(from, fromBuf, fromLen);
    from[fromLen] = '\0';
    duk_pop(ctx);

    duk_push_c_function(ctx, path_resolve, DUK_VARARGS);
    duk_dup(ctx, 1);
    duk_call(ctx, 1);
    duk_size_t toLen;
    const char *toBuf = duk_get_lstring(ctx, -1, &toLen);
    char *to = malloc(toLen + 1);
    if (!to) { free(from); duk_pop(ctx); RP_THROW(ctx, "path.relative: oom"); }
    memcpy(to, toBuf, toLen);
    to[toLen] = '\0';
    duk_pop(ctx);

    if (strcmp(from, to) == 0) {
        free(from); free(to);
        duk_push_string(ctx, "");
        return 1;
    }

    long fromStart = 1;
    long fromEnd = (long)fromLen;
    long fromLenT = fromEnd - fromStart;
    long toStart = 1;
    long toLenT = (long)toLen - toStart;
    long length = fromLenT < toLenT ? fromLenT : toLenT;
    long last_common_sep = -1;
    long i = 0;
    for (; i < length; i++) {
        int fc = (unsigned char)from[fromStart + i];
        if (fc != (unsigned char)to[toStart + i]) break;
        else if (fc == '/') last_common_sep = i;
    }
    if (i == length) {
        if (toLenT > length) {
            if (to[toStart + i] == '/') {
                char *r = strdup(to + toStart + i + 1);
                free(from); free(to);
                duk_push_string(ctx, r ? r : "");
                free(r);
                return 1;
            }
            if (i == 0) {
                char *r = strdup(to + toStart + i);
                free(from); free(to);
                duk_push_string(ctx, r ? r : "");
                free(r);
                return 1;
            }
        } else if (fromLenT > length) {
            if (from[fromStart + i] == '/') last_common_sep = i;
            else if (i == 0) last_common_sep = 0;
        }
    }

    ns_sb out = {0};
    for (long j = fromStart + last_common_sep + 1; j <= fromEnd; ++j) {
        if (j == fromEnd || from[j] == '/') {
            if (out.len == 0) {
                if (ns_sb_append(&out, "..", 2) != 0) goto oom;
            } else {
                if (ns_sb_append(&out, "/..", 3) != 0) goto oom;
            }
        }
    }
    if (toStart + last_common_sep < (long)toLen) {
        if (ns_sb_append(&out, to + toStart + last_common_sep,
                         toLen - (toStart + last_common_sep)) != 0) goto oom;
    }
    free(from); free(to);
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
oom:
    free(from); free(to); free(out.buf);
    RP_THROW(ctx, "path.relative: out of memory");
    return 0;
}

/* path.dirname(path) */
static duk_ret_t path_dirname(duk_context *ctx)
{
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);
    if (plen == 0) { duk_push_string(ctx, "."); return 1; }
    int has_root = (path[0] == '/');
    long end = -1;
    int matched_slash = 1;
    for (long i = (long)plen - 1; i >= 1; --i) {
        if (path[i] == '/') {
            if (!matched_slash) { end = i; break; }
        } else {
            matched_slash = 0;
        }
    }
    if (end == -1) {
        duk_push_string(ctx, has_root ? "/" : ".");
        return 1;
    }
    if (has_root && end == 1) {
        duk_push_string(ctx, "//");
        return 1;
    }
    duk_push_lstring(ctx, path, (duk_size_t)end);
    return 1;
}

/* path.basename(path[, suffix]) */
static duk_ret_t path_basename(duk_context *ctx)
{
    duk_size_t suflen = 0;
    const char *suf = NULL;
    if (!duk_is_undefined(ctx, 1)) {
        suf = ns_require_string(ctx, 1, "ext", &suflen);
    }
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);

    long start = 0, end = -1;
    int matched_slash = 1;
    long i;

    if (suf != NULL && suflen > 0 && suflen <= plen) {
        if (suflen == plen && memcmp(suf, path, suflen) == 0) {
            duk_push_string(ctx, "");
            return 1;
        }
        long ext_idx = (long)suflen - 1;
        long first_non_slash_end = -1;
        for (i = (long)plen - 1; i >= 0; --i) {
            int code = (unsigned char)path[i];
            if (code == '/') {
                if (!matched_slash) { start = i + 1; break; }
            } else {
                if (first_non_slash_end == -1) {
                    matched_slash = 0;
                    first_non_slash_end = i + 1;
                }
                if (ext_idx >= 0) {
                    if (code == (unsigned char)suf[ext_idx]) {
                        if (--ext_idx == -1) end = i;
                    } else {
                        ext_idx = -1;
                        end = first_non_slash_end;
                    }
                }
            }
        }
        if (start == end) end = first_non_slash_end;
        else if (end == -1) end = (long)plen;
        duk_push_lstring(ctx, path + start, (duk_size_t)(end - start));
        return 1;
    }
    for (i = (long)plen - 1; i >= 0; --i) {
        if (path[i] == '/') {
            if (!matched_slash) { start = i + 1; break; }
        } else if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
    }
    if (end == -1) {
        duk_push_string(ctx, "");
        return 1;
    }
    duk_push_lstring(ctx, path + start, (duk_size_t)(end - start));
    return 1;
}

/* path.extname(path) */
static duk_ret_t path_extname(duk_context *ctx)
{
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);
    long start_dot = -1, start_part = 0, end = -1;
    int matched_slash = 1, pre_dot_state = 0;
    for (long i = (long)plen - 1; i >= 0; --i) {
        int code = (unsigned char)path[i];
        if (code == '/') {
            if (!matched_slash) { start_part = i + 1; break; }
            continue;
        }
        if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
        if (code == '.') {
            if (start_dot == -1) start_dot = i;
            else if (pre_dot_state != 1) pre_dot_state = 1;
        } else if (start_dot != -1) {
            pre_dot_state = -1;
        }
    }
    if (start_dot == -1 || end == -1 || pre_dot_state == 0 ||
        (pre_dot_state == 1 && start_dot == end - 1 && start_dot == start_part + 1)) {
        duk_push_string(ctx, "");
        return 1;
    }
    duk_push_lstring(ctx, path + start_dot, (duk_size_t)(end - start_dot));
    return 1;
}

/* path.parse(path) -> { root, dir, base, name, ext } */
static duk_ret_t path_parse(duk_context *ctx)
{
    duk_size_t plen;
    const char *path = ns_require_string(ctx, 0, "path", &plen);

    duk_push_object(ctx);
    /* Push placeholders so the property order matches node's output */
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "root");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "dir");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "base");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "ext");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "name");

    if (plen == 0) return 1;

    int is_absolute = (path[0] == '/');
    long start;
    if (is_absolute) {
        duk_push_string(ctx, "/");
        duk_put_prop_string(ctx, -2, "root");
        start = 1;
    } else {
        start = 0;
    }
    long start_dot = -1, start_part = 0, end = -1;
    int matched_slash = 1, pre_dot_state = 0;
    for (long i = (long)plen - 1; i >= start; --i) {
        int code = (unsigned char)path[i];
        if (code == '/') {
            if (!matched_slash) { start_part = i + 1; break; }
            continue;
        }
        if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
        if (code == '.') {
            if (start_dot == -1) start_dot = i;
            else if (pre_dot_state != 1) pre_dot_state = 1;
        } else if (start_dot != -1) {
            pre_dot_state = -1;
        }
    }
    if (end != -1) {
        long start_calc = (start_part == 0 && is_absolute) ? 1 : start_part;
        if (start_dot == -1 || pre_dot_state == 0 ||
            (pre_dot_state == 1 && start_dot == end - 1 && start_dot == start_part + 1)) {
            duk_push_lstring(ctx, path + start_calc, (duk_size_t)(end - start_calc));
            duk_put_prop_string(ctx, -2, "base");
            duk_push_lstring(ctx, path + start_calc, (duk_size_t)(end - start_calc));
            duk_put_prop_string(ctx, -2, "name");
        } else {
            duk_push_lstring(ctx, path + start_calc, (duk_size_t)(start_dot - start_calc));
            duk_put_prop_string(ctx, -2, "name");
            duk_push_lstring(ctx, path + start_calc, (duk_size_t)(end - start_calc));
            duk_put_prop_string(ctx, -2, "base");
            duk_push_lstring(ctx, path + start_dot, (duk_size_t)(end - start_dot));
            duk_put_prop_string(ctx, -2, "ext");
        }
    }
    if (start_part > 0) {
        duk_push_lstring(ctx, path, (duk_size_t)(start_part - 1));
        duk_put_prop_string(ctx, -2, "dir");
    } else if (is_absolute) {
        duk_push_string(ctx, "/");
        duk_put_prop_string(ctx, -2, "dir");
    }
    return 1;
}

/* path.format({root, dir, base, name, ext}) */
static duk_ret_t path_format(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0) || duk_is_null(ctx, 0) || duk_is_array(ctx, 0)) {
        ns_throw_invalid_arg_type(ctx, "pathObject", "object", 0);
    }
    const char *dir = NULL, *root = NULL, *base = NULL, *name = NULL, *ext = NULL;
    if (duk_get_prop_string(ctx, 0, "dir") && duk_is_string(ctx, -1))
        dir = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "root") && duk_is_string(ctx, -1))
        root = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "base") && duk_is_string(ctx, -1))
        base = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "name") && duk_is_string(ctx, -1))
        name = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "ext") && duk_is_string(ctx, -1))
        ext = duk_get_string(ctx, -1);
    duk_pop(ctx);

    const char *dir_use = (dir && *dir) ? dir : (root ? root : "");

    /* base = base || (name || '') + (ext || '') */
    ns_sb tmp_base = {0};
    int base_owned = 0;
    if (base && *base) {
        /* base provided as-is */
    } else {
        if (name) ns_sb_append(&tmp_base, name, strlen(name));
        if (ext && *ext) {
            /* node formatExt: prepend '.' if ext doesn't already start with one */
            if (ext[0] != '.') ns_sb_appendc(&tmp_base, '.');
            ns_sb_append(&tmp_base, ext, strlen(ext));
        }
        if (ns_sb_appendc(&tmp_base, '\0') != 0) { /* null-terminate */
            free(tmp_base.buf);
            RP_THROW(ctx, "path.format: oom");
        }
        tmp_base.len--; /* don't count the null */
        base = tmp_base.buf ? tmp_base.buf : "";
        base_owned = 1;
    }

    if (!dir_use || !*dir_use) {
        duk_push_string(ctx, base);
        if (base_owned) free(tmp_base.buf);
        return 1;
    }
    /* If dir === root, no separator; else dir + '/' + base */
    int dir_eq_root = (root && strcmp(dir_use, root) == 0);
    ns_sb out = {0};
    ns_sb_append(&out, dir_use, strlen(dir_use));
    if (!dir_eq_root) ns_sb_appendc(&out, '/');
    ns_sb_append(&out, base, strlen(base));
    if (base_owned) free(tmp_base.buf);
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
}

/* path.toNamespacedPath(path) — identity on posix */
static duk_ret_t path_to_namespaced_path(duk_context *ctx)
{
    duk_dup(ctx, 0);
    return 1;
}

/* ===========================================================
 * win32 path module — ported from node's lib/path.js
 * ===========================================================
 *
 * Differences from posix:
 *  - Two separators accepted as input: '/' and '\'. Output uses '\'.
 *  - Roots: drive-letter (`C:`), drive-absolute (`C:\`),
 *    drive-relative (`C:foo`), UNC (`\\server\share`),
 *    device namespace (`\\?\…`, `\\.\…`).
 *  - Case-insensitive comparison for relative().
 *  - Reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9).
 *
 * On linux, win32 logic still works: process.cwd() returns a posix
 * path, but each win32 function treats it as a path of "no drive"
 * (no drive-letter). This matches node's behavior on linux.
 */

static int is_windows_device_root(int code)
{
    return (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
}

/* Windows reserved device names. CON/PRN/AUX/NUL, COM1-COM9, LPT1-LPT9,
   plus the superscript variants (COM¹/COM²/COM³, LPT¹/LPT²/LPT³).
   Superscripts are U+00B9/U+00B2/U+00B3 → UTF-8 0xC2 0xB9/0xB2/0xB3. */
static const char *WINDOWS_RESERVED_NAMES[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    "COM\xc2\xb9", "COM\xc2\xb2", "COM\xc2\xb3",
    "LPT\xc2\xb9", "LPT\xc2\xb2", "LPT\xc2\xb3",
    NULL
};

/* Is path[0..end] (uppercased ASCII) a reserved Windows device name? */
static int is_windows_reserved_name(const char *path, size_t end)
{
    if (end == 0 || end > 8) return 0;  /* shortest 3 (CON), longest 5 bytes ascii or 5 UTF-8 (LPT+superscript) */
    char upper[8];
    if (end >= sizeof(upper)) return 0;
    for (size_t i = 0; i < end; i++) {
        char c = path[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        upper[i] = c;
    }
    upper[end] = '\0';
    for (int i = 0; WINDOWS_RESERVED_NAMES[i]; i++) {
        if (strcmp(upper, WINDOWS_RESERVED_NAMES[i]) == 0) return 1;
    }
    return 0;
}

/* Find first byte index of `ch` in path[0..len), or -1. */
static long find_byte(const char *path, size_t len, char ch)
{
    for (size_t i = 0; i < len; i++)
        if (path[i] == ch) return (long)i;
    return -1;
}

/* Find first byte index of `ch` in path[from..len), or -1. */
static long find_byte_from(const char *path, size_t len, char ch, size_t from)
{
    for (size_t i = from; i < len; i++)
        if (path[i] == ch) return (long)i;
    return -1;
}

/* path.win32.isAbsolute(path) */
static duk_ret_t pathw_is_absolute(duk_context *ctx)
{
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);
    if (plen == 0) { duk_push_boolean(ctx, 0); return 1; }
    int code = (unsigned char)p[0];
    if (path_is_win32_sep(code)) { duk_push_boolean(ctx, 1); return 1; }
    /* drive letter + : + sep */
    if (plen > 2 && is_windows_device_root(code) && p[1] == ':' &&
        path_is_win32_sep((unsigned char)p[2])) {
        duk_push_boolean(ctx, 1); return 1;
    }
    duk_push_boolean(ctx, 0);
    return 1;
}

/* pathw_normalize_helper:
 *   Returns final string + length. Caller frees buf.
 *   Implements the logic in node lib/path.js win32.normalize.
 *   Simplified: doesn't reproduce the CVE-2024-36139 colon-injection
 *   guard or the isWindowsReservedName special-cases — those are
 *   Windows-OS-only concerns and add ~80 lines for cases that don't
 *   apply on linux. Documented as a known omission. */
static char *pathw_normalize_raw(const char *path, size_t len, size_t *outlen)
{
    if (len == 0) {
        char *r = malloc(2); if (!r) return NULL;
        r[0] = '.'; r[1] = '\0'; *outlen = 1; return r;
    }
    if (len == 1) {
        /* Single char: forward slash becomes backslash; anything else as-is */
        char *r = malloc(2); if (!r) return NULL;
        r[0] = (path[0] == '/') ? '\\' : path[0];
        r[1] = '\0'; *outlen = 1; return r;
    }

    size_t root_end = 0;
    ns_sb device = {0};
    int is_absolute = 0;
    int code = (unsigned char)path[0];

    if (path_is_win32_sep(code)) {
        is_absolute = 1;
        if (path_is_win32_sep((unsigned char)path[1])) {
            /* possible UNC root */
            size_t j = 2, last = j;
            while (j < len && !path_is_win32_sep((unsigned char)path[j])) j++;
            if (j < len && j != last) {
                size_t first_part_start = last;
                size_t first_part_end = j;
                last = j;
                while (j < len && path_is_win32_sep((unsigned char)path[j])) j++;
                if (j < len && j != last) {
                    last = j;
                    while (j < len && !path_is_win32_sep((unsigned char)path[j])) j++;
                    if (j == len || j != last) {
                        int fp_dot = (first_part_end - first_part_start == 1 &&
                                      path[first_part_start] == '.');
                        int fp_q   = (first_part_end - first_part_start == 1 &&
                                      path[first_part_start] == '?');
                        if (fp_dot || fp_q) {
                            /* device root \\?\ or \\.\ */
                            ns_sb_append(&device, "\\\\", 2);
                            ns_sb_appendc(&device, fp_dot ? '.' : '?');
                            root_end = 4;
                        } else if (j == len) {
                            /* UNC root only -- return normalized form */
                            ns_sb out = {0};
                            ns_sb_append(&out, "\\\\", 2);
                            ns_sb_append(&out, path + first_part_start,
                                          first_part_end - first_part_start);
                            ns_sb_appendc(&out, '\\');
                            ns_sb_append(&out, path + last, j - last);
                            ns_sb_appendc(&out, '\\');
                            free(device.buf);
                            *outlen = out.len;
                            return out.buf;
                        } else {
                            /* UNC root with leftovers: device = \\srv\share */
                            ns_sb_append(&device, "\\\\", 2);
                            ns_sb_append(&device, path + first_part_start,
                                          first_part_end - first_part_start);
                            ns_sb_appendc(&device, '\\');
                            ns_sb_append(&device, path + last, j - last);
                            root_end = j;
                        }
                    }
                }
            }
        } else {
            root_end = 1;
        }
    } else if (is_windows_device_root(code) && len > 1 && path[1] == ':') {
        ns_sb_append(&device, path, 2);
        root_end = 2;
        if (len > 2 && path_is_win32_sep((unsigned char)path[2])) {
            is_absolute = 1;
            root_end = 3;
        }
    }

    /* normalize the tail (everything after root_end) */
    size_t tail_len = 0;
    char *tail = NULL;
    if (root_end < len) {
        tail = path_normalize_string(path + root_end, len - root_end,
                                      !is_absolute, '\\', path_is_win32_sep, &tail_len);
        if (tail == NULL && root_end < len && !is_absolute) {
            /* empty result from normalize: treat as empty tail */
        }
    }
    if (tail_len == 0 && !is_absolute) {
        free(tail);
        tail = malloc(2); if (!tail) { free(device.buf); return NULL; }
        tail[0] = '.'; tail[1] = '\0'; tail_len = 1;
    }
    /* preserve trailing separator if original had one */
    if (tail_len > 0 && path_is_win32_sep((unsigned char)path[len - 1])) {
        char *nt = realloc(tail, tail_len + 1);
        if (!nt) { free(tail); free(device.buf); return NULL; }
        tail = nt;
        tail[tail_len++] = '\\';
    }

    /* CVE-2024-36139 guard: a non-absolute, no-device path that contains
       a colon must not normalize to something Windows would interpret as
       an absolute path. Prepend ".\\" in that case. Matches node lines 455-472. */
    int prepend_dot = 0;
    if (!is_absolute && device.len == 0) {
        long colon_in_path = find_byte(path, len, ':');
        if (colon_in_path != -1) {
            if (tail_len >= 2 && is_windows_device_root((unsigned char)tail[0]) && tail[1] == ':') {
                prepend_dot = 1;
            } else {
                long idx = colon_in_path;
                while (idx != -1) {
                    if ((size_t)idx == len - 1 ||
                        path_is_win32_sep((unsigned char)path[idx + 1])) {
                        prepend_dot = 1;
                        break;
                    }
                    idx = find_byte_from(path, len, ':', (size_t)idx + 1);
                }
            }
        }
    }
    /* Reserved-name guard: if path[0..colon] is a Windows reserved name,
       also prepend ".\\" (with device if any). Matches node lines 473-476. */
    int reserved_prepend = 0;
    long colon_idx = find_byte(path, len, ':');
    if (colon_idx > 0 && is_windows_reserved_name(path, (size_t)colon_idx)) {
        reserved_prepend = 1;
    }

    ns_sb out = {0};
    if (prepend_dot || reserved_prepend) ns_sb_append(&out, ".\\", 2);
    if (device.len == 0) {
        if (is_absolute) ns_sb_appendc(&out, '\\');
        if (tail) ns_sb_append(&out, tail, tail_len);
    } else {
        ns_sb_append(&out, device.buf, device.len);
        if (is_absolute) ns_sb_appendc(&out, '\\');
        if (tail) ns_sb_append(&out, tail, tail_len);
    }
    free(tail);
    free(device.buf);
    *outlen = out.len;
    return out.buf;
}

/* path.win32.normalize(path) */
static duk_ret_t pathw_normalize(duk_context *ctx)
{
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);
    size_t rlen = 0;
    char *r = pathw_normalize_raw(p, plen, &rlen);
    if (!r) RP_THROW(ctx, "path.win32.normalize: out of memory");
    duk_push_lstring(ctx, r, rlen);
    free(r);
    return 1;
}

/* path.win32.dirname(path) */
static duk_ret_t pathw_dirname(duk_context *ctx)
{
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);
    if (plen == 0) { duk_push_string(ctx, "."); return 1; }
    long root_end = -1;
    long offset = 0;
    int code = (unsigned char)p[0];

    if (plen == 1) {
        duk_push_string(ctx, path_is_win32_sep(code) ? "." : ".");
        if (path_is_win32_sep(code)) { duk_pop(ctx); duk_dup(ctx, 0); }
        return 1;
    }

    if (path_is_win32_sep(code)) {
        root_end = offset = 1;
        if (path_is_win32_sep((unsigned char)p[1])) {
            size_t j = 2, last = j;
            while (j < plen && !path_is_win32_sep((unsigned char)p[j])) j++;
            if (j < plen && j != last) {
                last = j;
                while (j < plen && path_is_win32_sep((unsigned char)p[j])) j++;
                if (j < plen && j != last) {
                    last = j;
                    while (j < plen && !path_is_win32_sep((unsigned char)p[j])) j++;
                    if (j == plen) { duk_dup(ctx, 0); return 1; }
                    if (j != last) {
                        root_end = offset = (long)(j + 1);
                    }
                }
            }
        }
    } else if (is_windows_device_root(code) && plen > 1 && p[1] == ':') {
        root_end = (plen > 2 && path_is_win32_sep((unsigned char)p[2])) ? 3 : 2;
        offset = root_end;
    }

    long end = -1;
    int matched_slash = 1;
    for (long i = (long)plen - 1; i >= offset; --i) {
        if (path_is_win32_sep((unsigned char)p[i])) {
            if (!matched_slash) { end = i; break; }
        } else {
            matched_slash = 0;
        }
    }
    if (end == -1) {
        if (root_end == -1) { duk_push_string(ctx, "."); return 1; }
        end = root_end;
    }
    duk_push_lstring(ctx, p, (duk_size_t)end);
    return 1;
}

/* path.win32.basename(path[, suffix]) */
static duk_ret_t pathw_basename(duk_context *ctx)
{
    duk_size_t suflen = 0;
    const char *suf = NULL;
    if (!duk_is_undefined(ctx, 1)) {
        suf = ns_require_string(ctx, 1, "suffix", &suflen);
    }
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);

    long start = 0, end = -1;
    int matched_slash = 1;

    /* drive-letter prefix: skip "C:" so it isn't mistaken for a trailing sep */
    if (plen >= 2 && is_windows_device_root((unsigned char)p[0]) && p[1] == ':') {
        start = 2;
    }

    if (suf != NULL && suflen > 0 && suflen <= plen) {
        if (suflen == plen && memcmp(suf, p, suflen) == 0) {
            duk_push_string(ctx, ""); return 1;
        }
        long ext_idx = (long)suflen - 1;
        long first_non_slash_end = -1;
        for (long i = (long)plen - 1; i >= start; --i) {
            int code = (unsigned char)p[i];
            if (path_is_win32_sep(code)) {
                if (!matched_slash) { start = i + 1; break; }
            } else {
                if (first_non_slash_end == -1) {
                    matched_slash = 0;
                    first_non_slash_end = i + 1;
                }
                if (ext_idx >= 0) {
                    if (code == (unsigned char)suf[ext_idx]) {
                        if (--ext_idx == -1) end = i;
                    } else {
                        ext_idx = -1;
                        end = first_non_slash_end;
                    }
                }
            }
        }
        if (start == end) end = first_non_slash_end;
        else if (end == -1) end = (long)plen;
        duk_push_lstring(ctx, p + start, (duk_size_t)(end - start));
        return 1;
    }
    for (long i = (long)plen - 1; i >= start; --i) {
        if (path_is_win32_sep((unsigned char)p[i])) {
            if (!matched_slash) { start = i + 1; break; }
        } else if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
    }
    if (end == -1) { duk_push_string(ctx, ""); return 1; }
    duk_push_lstring(ctx, p + start, (duk_size_t)(end - start));
    return 1;
}

/* path.win32.extname(path) */
static duk_ret_t pathw_extname(duk_context *ctx)
{
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);
    long start = 0, start_dot = -1, start_part = 0, end = -1;
    int matched_slash = 1, pre_dot_state = 0;

    if (plen >= 2 && p[1] == ':' && is_windows_device_root((unsigned char)p[0])) {
        start = start_part = 2;
    }

    for (long i = (long)plen - 1; i >= start; --i) {
        int code = (unsigned char)p[i];
        if (path_is_win32_sep(code)) {
            if (!matched_slash) { start_part = i + 1; break; }
            continue;
        }
        if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
        if (code == '.') {
            if (start_dot == -1) start_dot = i;
            else if (pre_dot_state != 1) pre_dot_state = 1;
        } else if (start_dot != -1) {
            pre_dot_state = -1;
        }
    }
    if (start_dot == -1 || end == -1 || pre_dot_state == 0 ||
        (pre_dot_state == 1 && start_dot == end - 1 && start_dot == start_part + 1)) {
        duk_push_string(ctx, "");
        return 1;
    }
    duk_push_lstring(ctx, p + start_dot, (duk_size_t)(end - start_dot));
    return 1;
}

/* path.win32.parse(path) */
static duk_ret_t pathw_parse(duk_context *ctx)
{
    duk_size_t plen;
    const char *p = ns_require_string(ctx, 0, "path", &plen);

    duk_push_object(ctx);
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "root");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "dir");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "base");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "ext");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, -2, "name");

    if (plen == 0) return 1;

    long root_end = 0;
    int code = (unsigned char)p[0];

    if (plen == 1) {
        if (path_is_win32_sep(code)) {
            duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "root");
            duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "dir");
            return 1;
        }
        duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "base");
        duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "name");
        return 1;
    }

    if (path_is_win32_sep(code)) {
        root_end = 1;
        if (path_is_win32_sep((unsigned char)p[1])) {
            size_t j = 2, last = j;
            while (j < plen && !path_is_win32_sep((unsigned char)p[j])) j++;
            if (j < plen && j != last) {
                last = j;
                while (j < plen && path_is_win32_sep((unsigned char)p[j])) j++;
                if (j < plen && j != last) {
                    last = j;
                    while (j < plen && !path_is_win32_sep((unsigned char)p[j])) j++;
                    if (j == plen) root_end = (long)j;
                    else if (j != last) root_end = (long)(j + 1);
                }
            }
        }
    } else if (is_windows_device_root(code) && plen > 1 && p[1] == ':') {
        if (plen <= 2) {
            duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "root");
            duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "dir");
            return 1;
        }
        root_end = 2;
        if (path_is_win32_sep((unsigned char)p[2])) {
            if (plen == 3) {
                duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "root");
                duk_dup(ctx, 0); duk_put_prop_string(ctx, -2, "dir");
                return 1;
            }
            root_end = 3;
        }
    }
    if (root_end > 0) {
        duk_push_lstring(ctx, p, (duk_size_t)root_end);
        duk_put_prop_string(ctx, -2, "root");
    }

    long start_dot = -1, start_part = root_end, end = -1;
    int matched_slash = 1, pre_dot_state = 0;

    for (long i = (long)plen - 1; i >= root_end; --i) {
        int c = (unsigned char)p[i];
        if (path_is_win32_sep(c)) {
            if (!matched_slash) { start_part = i + 1; break; }
            continue;
        }
        if (end == -1) {
            matched_slash = 0;
            end = i + 1;
        }
        if (c == '.') {
            if (start_dot == -1) start_dot = i;
            else if (pre_dot_state != 1) pre_dot_state = 1;
        } else if (start_dot != -1) {
            pre_dot_state = -1;
        }
    }

    if (end != -1) {
        if (start_dot == -1 || pre_dot_state == 0 ||
            (pre_dot_state == 1 && start_dot == end - 1 && start_dot == start_part + 1)) {
            duk_push_lstring(ctx, p + start_part, (duk_size_t)(end - start_part));
            duk_put_prop_string(ctx, -2, "base");
            duk_push_lstring(ctx, p + start_part, (duk_size_t)(end - start_part));
            duk_put_prop_string(ctx, -2, "name");
        } else {
            duk_push_lstring(ctx, p + start_part, (duk_size_t)(start_dot - start_part));
            duk_put_prop_string(ctx, -2, "name");
            duk_push_lstring(ctx, p + start_part, (duk_size_t)(end - start_part));
            duk_put_prop_string(ctx, -2, "base");
            duk_push_lstring(ctx, p + start_dot, (duk_size_t)(end - start_dot));
            duk_put_prop_string(ctx, -2, "ext");
        }
    }

    if (start_part > 0 && start_part != root_end) {
        duk_push_lstring(ctx, p, (duk_size_t)(start_part - 1));
        duk_put_prop_string(ctx, -2, "dir");
    } else {
        duk_get_prop_string(ctx, -1, "root");
        duk_put_prop_string(ctx, -2, "dir");
    }
    return 1;
}

/* path.win32.format(obj) */
static duk_ret_t pathw_format(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0) || duk_is_null(ctx, 0) || duk_is_array(ctx, 0))
        ns_throw_invalid_arg_type(ctx, "pathObject", "object", 0);

    const char *dir = NULL, *root = NULL, *base = NULL, *name = NULL, *ext = NULL;
    if (duk_get_prop_string(ctx, 0, "dir") && duk_is_string(ctx, -1))  dir  = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "root") && duk_is_string(ctx, -1)) root = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "base") && duk_is_string(ctx, -1)) base = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "name") && duk_is_string(ctx, -1)) name = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, 0, "ext") && duk_is_string(ctx, -1))  ext  = duk_get_string(ctx, -1);
    duk_pop(ctx);

    const char *dir_use = (dir && *dir) ? dir : (root ? root : "");
    ns_sb tmp_base = {0};
    int base_owned = 0;
    if (!(base && *base)) {
        if (name) ns_sb_append(&tmp_base, name, strlen(name));
        if (ext && *ext) {
            /* node formatExt: prepend '.' if ext doesn't already start with one */
            if (ext[0] != '.') ns_sb_appendc(&tmp_base, '.');
            ns_sb_append(&tmp_base, ext, strlen(ext));
        }
        ns_sb_appendc(&tmp_base, '\0');
        tmp_base.len--;
        base = tmp_base.buf ? tmp_base.buf : "";
        base_owned = 1;
    }
    if (!dir_use || !*dir_use) {
        duk_push_string(ctx, base);
        if (base_owned) free(tmp_base.buf);
        return 1;
    }
    int dir_eq_root = (root && strcmp(dir_use, root) == 0);
    ns_sb out = {0};
    ns_sb_append(&out, dir_use, strlen(dir_use));
    if (!dir_eq_root) ns_sb_appendc(&out, '\\');
    ns_sb_append(&out, base, strlen(base));
    if (base_owned) free(tmp_base.buf);
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
}

/* path.win32.toNamespacedPath(path) */
static duk_ret_t pathw_to_namespaced_path(duk_context *ctx)
{
    if (!duk_is_string(ctx, 0) || duk_get_length(ctx, 0) == 0) {
        duk_dup(ctx, 0);
        return 1;
    }
    /* call win32.resolve(path) to get an absolute path */
    duk_push_c_function(ctx, NULL, 0); /* placeholder, will fix when we have resolve */
    duk_pop(ctx);
    /* For now, use the path as-is and apply the wrap */
    duk_size_t rlen;
    const char *r = duk_get_lstring(ctx, 0, &rlen);
    if (rlen <= 2) { duk_dup(ctx, 0); return 1; }
    int c0 = (unsigned char)r[0], c1 = (unsigned char)r[1], c2 = (unsigned char)r[2];
    if (c0 == '\\') {
        if (c1 == '\\') {
            if (c2 != '?' && c2 != '.') {
                /* Convert to long UNC: \\?\UNC\... */
                ns_sb out = {0};
                ns_sb_append(&out, "\\\\?\\UNC\\", 8);
                ns_sb_append(&out, r + 2, rlen - 2);
                duk_push_lstring(ctx, out.buf, out.len);
                free(out.buf);
                return 1;
            }
        }
    } else if (is_windows_device_root(c0) && c1 == ':' && c2 == '\\') {
        ns_sb out = {0};
        ns_sb_append(&out, "\\\\?\\", 4);
        ns_sb_append(&out, r, rlen);
        duk_push_lstring(ctx, out.buf, out.len);
        free(out.buf);
        return 1;
    }
    duk_dup(ctx, 0);
    return 1;
}

/* path.win32.join(...paths) */
static duk_ret_t pathw_join(duk_context *ctx)
{
    duk_idx_t n = duk_get_top(ctx);
    if (n == 0) { duk_push_string(ctx, "."); return 1; }
    ns_sb joined = {0};
    int any = 0;
    const char *first_part = NULL;
    duk_size_t first_part_len = 0;
    for (duk_idx_t i = 0; i < n; i++) {
        duk_size_t alen;
        const char *arg = ns_require_string(ctx, i, "path", &alen);
        if (alen == 0) continue;
        if (!any) { first_part = arg; first_part_len = alen; }
        if (any) ns_sb_appendc(&joined, '\\');
        ns_sb_append(&joined, arg, alen);
        any = 1;
    }
    if (!any) { free(joined.buf); duk_push_string(ctx, "."); return 1; }

    /* UNC-leading-slash dedup logic from node */
    int needs_replace = 1;
    size_t slash_count = 0;
    if (first_part_len > 0 && path_is_win32_sep((unsigned char)first_part[0])) {
        slash_count++;
        if (first_part_len > 1 && path_is_win32_sep((unsigned char)first_part[1])) {
            slash_count++;
            if (first_part_len > 2) {
                if (path_is_win32_sep((unsigned char)first_part[2])) slash_count++;
                else needs_replace = 0;
            }
        }
    }
    if (needs_replace) {
        while (slash_count < joined.len && path_is_win32_sep((unsigned char)joined.buf[slash_count])) {
            slash_count++;
        }
        if (slash_count >= 2) {
            /* joined = '\\' + slice(slash_count) */
            ns_sb tmp = {0};
            ns_sb_appendc(&tmp, '\\');
            if (joined.len > slash_count)
                ns_sb_append(&tmp, joined.buf + slash_count, joined.len - slash_count);
            free(joined.buf);
            joined = tmp;
        }
    }

    /* Reserved-name skip: if any segment of `joined` (split on '\' with
       consecutive backslashes collapsed) contains a colon and the part
       before the colon is a Windows reserved name, skip normalization
       entirely and just convert forward slashes to backslashes.
       Matches node lines 569-595. */
    int has_reserved = 0;
    {
        size_t k = 0;
        while (k < joined.len && !has_reserved) {
            /* find next segment in joined */
            while (k < joined.len && joined.buf[k] == '\\') k++;
            size_t seg_start = k;
            while (k < joined.len && joined.buf[k] != '\\') k++;
            size_t seg_end = k;
            if (seg_end > seg_start) {
                /* find colon in [seg_start, seg_end) */
                for (size_t m = seg_start; m < seg_end; m++) {
                    if (joined.buf[m] == ':') {
                        if (is_windows_reserved_name(joined.buf + seg_start, m - seg_start))
                            has_reserved = 1;
                        break;
                    }
                }
            }
        }
    }
    if (has_reserved) {
        /* Replace forward slashes with backslashes; no normalize */
        for (size_t k = 0; k < joined.len; k++)
            if (joined.buf[k] == '/') joined.buf[k] = '\\';
        duk_push_lstring(ctx, joined.buf, joined.len);
        free(joined.buf);
        return 1;
    }

    /* normal path: normalize via pathw_normalize_raw */
    size_t rlen = 0;
    char *r = pathw_normalize_raw(joined.buf, joined.len, &rlen);
    free(joined.buf);
    if (!r) RP_THROW(ctx, "path.win32.join: out of memory");
    duk_push_lstring(ctx, r, rlen);
    free(r);
    return 1;
}

/* path.win32.resolve(...paths) — simplified. On linux we don't have
   per-drive cwds, so we use a single process.cwd() with no drive. */
static duk_ret_t pathw_resolve(duk_context *ctx)
{
    duk_idx_t n = duk_get_top(ctx);
    ns_sb resolved_tail = {0};
    ns_sb resolved_device = {0};
    int resolved_absolute = 0;

    for (duk_idx_t i = n - 1; i >= -1; i--) {
        const char *p;
        duk_size_t plen;
        int did_push = 0;  /* track whether this iteration pushed a string to stack */
        if (i >= 0) {
            p = ns_require_string(ctx, i, "path", &plen);
            if (plen == 0) continue;
        } else if (resolved_device.len == 0) {
            p = ns_push_cwd(ctx, &plen);
            did_push = 1;
        } else {
            /* device set but not absolute: would be drive cwd on windows.
               On linux, fall back to drive's root. */
            ns_sb tmp = {0};
            ns_sb_append(&tmp, resolved_device.buf, resolved_device.len);
            ns_sb_appendc(&tmp, '\\');
            duk_push_lstring(ctx, tmp.buf, tmp.len);
            free(tmp.buf);
            p = duk_get_lstring(ctx, -1, &plen);
            did_push = 1;
        }

        size_t len = plen;
        size_t root_end = 0;
        ns_sb device = {0};
        int is_absolute = 0;
        int c = (unsigned char)p[0];

        if (len == 1) {
            if (path_is_win32_sep(c)) { root_end = 1; is_absolute = 1; }
        } else if (path_is_win32_sep(c)) {
            is_absolute = 1;
            if (path_is_win32_sep((unsigned char)p[1])) {
                size_t j = 2, last = j;
                while (j < len && !path_is_win32_sep((unsigned char)p[j])) j++;
                if (j < len && j != last) {
                    size_t fp_s = last, fp_e = j;
                    last = j;
                    while (j < len && path_is_win32_sep((unsigned char)p[j])) j++;
                    if (j < len && j != last) {
                        last = j;
                        while (j < len && !path_is_win32_sep((unsigned char)p[j])) j++;
                        if (j == len || j != last) {
                            int fp_dot = (fp_e - fp_s == 1 && p[fp_s] == '.');
                            int fp_q   = (fp_e - fp_s == 1 && p[fp_s] == '?');
                            if (!fp_dot && !fp_q) {
                                ns_sb_append(&device, "\\\\", 2);
                                ns_sb_append(&device, p + fp_s, fp_e - fp_s);
                                ns_sb_appendc(&device, '\\');
                                ns_sb_append(&device, p + last, j - last);
                                root_end = j;
                            } else {
                                ns_sb_append(&device, "\\\\", 2);
                                ns_sb_appendc(&device, fp_dot ? '.' : '?');
                                root_end = 4;
                            }
                        }
                    }
                }
            } else {
                root_end = 1;
            }
        } else if (is_windows_device_root(c) && len > 1 && p[1] == ':') {
            ns_sb_append(&device, p, 2);
            root_end = 2;
            if (len > 2 && path_is_win32_sep((unsigned char)p[2])) {
                is_absolute = 1;
                root_end = 3;
            }
        }

        if (device.len > 0) {
            if (resolved_device.len > 0) {
                /* case-insensitive comparison */
                int same = (device.len == resolved_device.len);
                if (same) {
                    for (size_t k = 0; k < device.len; k++) {
                        int a = (unsigned char)device.buf[k];
                        int b = (unsigned char)resolved_device.buf[k];
                        if (a >= 'A' && a <= 'Z') a += 32;
                        if (b >= 'A' && b <= 'Z') b += 32;
                        if (a != b) { same = 0; break; }
                    }
                }
                if (!same) { free(device.buf); if (did_push) duk_pop(ctx); continue; }
            } else {
                free(resolved_device.buf);
                resolved_device = device;
                device.buf = NULL;
            }
        }
        free(device.buf);

        if (resolved_absolute) {
            if (resolved_device.len > 0) { if (did_push) duk_pop(ctx); break; }
        } else {
            /* resolved_tail = p[root_end..] + '\\' + resolved_tail */
            ns_sb tmp = {0};
            ns_sb_append(&tmp, p + root_end, plen - root_end);
            ns_sb_appendc(&tmp, '\\');
            ns_sb_append(&tmp, resolved_tail.buf, resolved_tail.len);
            free(resolved_tail.buf);
            resolved_tail = tmp;
            resolved_absolute = is_absolute;
            if (is_absolute && resolved_device.len > 0) { if (did_push) duk_pop(ctx); break; }
        }
        if (did_push) duk_pop(ctx);
    }

    /* normalize tail */
    size_t rt_len = 0;
    char *rt = path_normalize_string(resolved_tail.buf, resolved_tail.len,
                                      !resolved_absolute, '\\', path_is_win32_sep, &rt_len);
    free(resolved_tail.buf);

    ns_sb out = {0};
    if (resolved_absolute) {
        if (resolved_device.len > 0) ns_sb_append(&out, resolved_device.buf, resolved_device.len);
        ns_sb_appendc(&out, '\\');
        if (rt_len > 0) ns_sb_append(&out, rt, rt_len);
    } else {
        if (resolved_device.len > 0) ns_sb_append(&out, resolved_device.buf, resolved_device.len);
        if (rt_len > 0) ns_sb_append(&out, rt, rt_len);
    }
    free(rt);
    free(resolved_device.buf);

    if (out.len == 0) { free(out.buf); duk_push_string(ctx, "."); return 1; }
    duk_push_lstring(ctx, out.buf, out.len);
    free(out.buf);
    return 1;
}

/* path.win32.relative(from, to) — case-insensitive */
static duk_ret_t pathw_relative(duk_context *ctx)
{
    (void)ns_require_string(ctx, 0, "from", NULL);
    (void)ns_require_string(ctx, 1, "to", NULL);
    if (duk_strict_equals(ctx, 0, 1)) { duk_push_string(ctx, ""); return 1; }

    /* resolve both */
    duk_push_c_function(ctx, pathw_resolve, DUK_VARARGS);
    duk_dup(ctx, 0);
    duk_call(ctx, 1);
    duk_size_t fol;
    const char *fob = duk_get_lstring(ctx, -1, &fol);
    char *from_orig = malloc(fol + 1);
    if (!from_orig) { duk_pop(ctx); RP_THROW(ctx, "path.win32.relative: oom"); }
    memcpy(from_orig, fob, fol);
    from_orig[fol] = '\0';
    duk_pop(ctx);

    duk_push_c_function(ctx, pathw_resolve, DUK_VARARGS);
    duk_dup(ctx, 1);
    duk_call(ctx, 1);
    duk_size_t tol;
    const char *tob = duk_get_lstring(ctx, -1, &tol);
    char *to_orig = malloc(tol + 1);
    if (!to_orig) { free(from_orig); duk_pop(ctx); RP_THROW(ctx, "path.win32.relative: oom"); }
    memcpy(to_orig, tob, tol);
    to_orig[tol] = '\0';
    duk_pop(ctx);

    if (strcmp(from_orig, to_orig) == 0) {
        free(from_orig); free(to_orig);
        duk_push_string(ctx, "");
        return 1;
    }

    /* lowercase copies via duktape's Unicode-aware toLowerCase */
    size_t from_lc_len = 0, to_lc_len = 0;
    char *from_lc = ns_to_lowercase(ctx, from_orig, fol, &from_lc_len);
    char *to_lc   = ns_to_lowercase(ctx, to_orig,   tol, &to_lc_len);
    if (!from_lc || !to_lc) {
        free(from_orig); free(to_orig); free(from_lc); free(to_lc);
        RP_THROW(ctx, "path.win32.relative: oom");
    }

    if (from_lc_len == to_lc_len && memcmp(from_lc, to_lc, from_lc_len) == 0) {
        free(from_orig); free(to_orig); free(from_lc); free(to_lc);
        duk_push_string(ctx, "");
        return 1;
    }

    /* SLOW PATH: lowercase changed length (Unicode expansion, e.g. Turkish İ).
       Index-aligned comparison no longer works against the original strings.
       Split both on '\\' and compare segments case-insensitively, matching
       node's lib/path.js win32.relative slow path (lines 629-663). */
    if (from_lc_len != fol || to_lc_len != tol) {
        free(from_lc); free(to_lc);

        typedef struct { const char *p; size_t len; } seg;
        size_t fs_cap = 16, fs_n = 0;
        size_t ts_cap = 16, ts_n = 0;
        seg *fs = malloc(fs_cap * sizeof(seg));
        seg *ts = malloc(ts_cap * sizeof(seg));
        if (!fs || !ts) {
            free(fs); free(ts); free(from_orig); free(to_orig);
            RP_THROW(ctx, "path.win32.relative: oom");
        }
        /* split from_orig on '\\' */
        size_t k = 0;
        while (k <= fol) {
            size_t e = k;
            while (e < fol && from_orig[e] != '\\') e++;
            if (fs_n == fs_cap) {
                fs_cap *= 2;
                seg *nfs = realloc(fs, fs_cap * sizeof(seg));
                if (!nfs) { free(fs); free(ts); free(from_orig); free(to_orig); RP_THROW(ctx, "oom"); }
                fs = nfs;
            }
            fs[fs_n].p = from_orig + k;
            fs[fs_n].len = e - k;
            fs_n++;
            if (e == fol) break;
            k = e + 1;
        }
        /* split to_orig on '\\' */
        k = 0;
        while (k <= tol) {
            size_t e = k;
            while (e < tol && to_orig[e] != '\\') e++;
            if (ts_n == ts_cap) {
                ts_cap *= 2;
                seg *nts = realloc(ts, ts_cap * sizeof(seg));
                if (!nts) { free(fs); free(ts); free(from_orig); free(to_orig); RP_THROW(ctx, "oom"); }
                ts = nts;
            }
            ts[ts_n].p = to_orig + k;
            ts[ts_n].len = e - k;
            ts_n++;
            if (e == tol) break;
            k = e + 1;
        }
        /* pop trailing empty segments (matches node's pop()) */
        if (fs_n > 0 && fs[fs_n - 1].len == 0) fs_n--;
        if (ts_n > 0 && ts[ts_n - 1].len == 0) ts_n--;

        size_t length = fs_n < ts_n ? fs_n : ts_n;
        size_t common = 0;
        for (size_t j = 0; j < length; j++) {
            size_t a_lc_len, b_lc_len;
            char *a_lc = ns_to_lowercase(ctx, fs[j].p, fs[j].len, &a_lc_len);
            char *b_lc = ns_to_lowercase(ctx, ts[j].p, ts[j].len, &b_lc_len);
            int eq = a_lc && b_lc && a_lc_len == b_lc_len &&
                     memcmp(a_lc, b_lc, a_lc_len) == 0;
            free(a_lc); free(b_lc);
            if (!eq) break;
            common++;
        }

        ns_sb out = {0};
        if (common == 0) {
            duk_push_lstring(ctx, to_orig, tol);
        } else if (common == length) {
            if (ts_n > length) {
                for (size_t j = common; j < ts_n; j++) {
                    ns_sb_append(&out, ts[j].p, ts[j].len);
                    if (j < ts_n - 1) ns_sb_appendc(&out, '\\');
                }
                duk_push_lstring(ctx, out.buf, out.len);
            } else if (fs_n > length) {
                for (size_t j = 0; j + 1 + common < fs_n; j++)
                    ns_sb_append(&out, "..\\", 3);
                ns_sb_append(&out, "..", 2);
                duk_push_lstring(ctx, out.buf, out.len);
            } else {
                duk_push_string(ctx, "");
            }
        } else {
            for (size_t j = 0; j + common < fs_n; j++)
                ns_sb_append(&out, "..\\", 3);
            for (size_t j = common; j < ts_n; j++) {
                ns_sb_append(&out, ts[j].p, ts[j].len);
                if (j < ts_n - 1) ns_sb_appendc(&out, '\\');
            }
            duk_push_lstring(ctx, out.buf, out.len);
        }
        free(out.buf); free(fs); free(ts);
        free(from_orig); free(to_orig);
        return 1;
    }
    /* fast path falls through to index-aligned comparison */

    /* trim leading backslashes */
    long from_start = 0;
    while (from_start < (long)fol && from_lc[from_start] == '\\') from_start++;
    long from_end = (long)fol;
    while (from_end - 1 > from_start && from_lc[from_end - 1] == '\\') from_end--;
    long from_len = from_end - from_start;

    long to_start = 0;
    while (to_start < (long)tol && to_lc[to_start] == '\\') to_start++;
    long to_end = (long)tol;
    while (to_end - 1 > to_start && to_lc[to_end - 1] == '\\') to_end--;
    long to_len = to_end - to_start;

    long length = from_len < to_len ? from_len : to_len;
    long last_common_sep = -1;
    long i = 0;
    for (; i < length; i++) {
        int fc = (unsigned char)from_lc[from_start + i];
        if (fc != (unsigned char)to_lc[to_start + i]) break;
        else if (fc == '\\') last_common_sep = i;
    }

    if (i != length) {
        if (last_common_sep == -1) {
            free(from_orig); free(from_lc); free(to_lc);
            duk_push_string(ctx, to_orig); free(to_orig); return 1;
        }
    } else {
        if (to_len > length) {
            if (to_lc[to_start + i] == '\\') {
                duk_push_string(ctx, to_orig + to_start + i + 1);
                free(from_orig); free(to_orig); free(from_lc); free(to_lc); return 1;
            }
            if (i == 2) {
                duk_push_string(ctx, to_orig + to_start + i);
                free(from_orig); free(to_orig); free(from_lc); free(to_lc); return 1;
            }
        }
        if (from_len > length) {
            if (from_lc[from_start + i] == '\\') last_common_sep = i;
            else if (i == 2) last_common_sep = 3;
        }
        if (last_common_sep == -1) last_common_sep = 0;
    }

    ns_sb out = {0};
    for (long j = from_start + last_common_sep + 1; j <= from_end; ++j) {
        if (j == from_end || from_lc[j] == '\\') {
            if (out.len == 0) ns_sb_append(&out, "..", 2);
            else ns_sb_append(&out, "\\..", 3);
        }
    }
    to_start += last_common_sep;
    if (out.len > 0) {
        ns_sb_append(&out, to_orig + to_start, (size_t)(to_end - to_start));
        duk_push_lstring(ctx, out.buf, out.len);
        free(out.buf);
    } else {
        if (to_orig[to_start] == '\\') to_start++;
        duk_push_lstring(ctx, to_orig + to_start, (duk_size_t)(to_end - to_start));
    }
    free(from_orig); free(to_orig); free(from_lc); free(to_lc);
    return 1;
}

static void nodeshim_init_path(duk_context *ctx)
{
    /* Build a posix object and a win32 object as siblings, then return
       posix as the "default" path (since rampart is linux-only). Both
       have .posix and .win32 properties pointing at the same pair.
       This matches node's structure on a posix host. */

#define POSIX_FN(name, fn, nargs) do { \
    duk_push_c_function(ctx, fn, nargs); \
    duk_put_prop_string(ctx, -2, name); \
} while (0)

    /* --- posix object --- */
    duk_idx_t posix_idx = duk_push_object(ctx);
    POSIX_FN("normalize",         path_normalize,           1);
    POSIX_FN("isAbsolute",        path_is_absolute,         1);
    POSIX_FN("join",              path_join,                DUK_VARARGS);
    POSIX_FN("resolve",           path_resolve,             DUK_VARARGS);
    POSIX_FN("relative",          path_relative,            2);
    POSIX_FN("dirname",           path_dirname,             1);
    POSIX_FN("basename",          path_basename,            2);
    POSIX_FN("extname",           path_extname,             1);
    POSIX_FN("parse",             path_parse,               1);
    POSIX_FN("format",            path_format,              1);
    POSIX_FN("toNamespacedPath",  path_to_namespaced_path,  1);
    duk_push_string(ctx, "/"); duk_put_prop_string(ctx, -2, "sep");
    duk_push_string(ctx, ":"); duk_put_prop_string(ctx, -2, "delimiter");

    /* --- win32 object --- */
    duk_idx_t win32_idx = duk_push_object(ctx);
    POSIX_FN("normalize",         pathw_normalize,          1);
    POSIX_FN("isAbsolute",        pathw_is_absolute,        1);
    POSIX_FN("join",              pathw_join,               DUK_VARARGS);
    POSIX_FN("resolve",           pathw_resolve,            DUK_VARARGS);
    POSIX_FN("relative",          pathw_relative,           2);
    POSIX_FN("dirname",           pathw_dirname,            1);
    POSIX_FN("basename",          pathw_basename,           2);
    POSIX_FN("extname",           pathw_extname,            1);
    POSIX_FN("parse",             pathw_parse,              1);
    POSIX_FN("format",            pathw_format,             1);
    POSIX_FN("toNamespacedPath",  pathw_to_namespaced_path, 1);
    duk_push_string(ctx, "\\"); duk_put_prop_string(ctx, -2, "sep");
    duk_push_string(ctx, ";");  duk_put_prop_string(ctx, -2, "delimiter");
#undef POSIX_FN

    /* Cross-references: posix.posix=posix, posix.win32=win32,
                          win32.posix=posix, win32.win32=win32 */
    duk_dup(ctx, posix_idx); duk_put_prop_string(ctx, win32_idx, "posix");
    duk_dup(ctx, win32_idx); duk_put_prop_string(ctx, win32_idx, "win32");
    duk_dup(ctx, win32_idx); duk_put_prop_string(ctx, posix_idx, "win32");
    duk_dup(ctx, posix_idx); duk_put_prop_string(ctx, posix_idx, "posix");

    /* path.matchesGlob(path, pattern) -- node 22+.  Returns true if
     * `path` matches the glob pattern.  Supports *, ?, [...], and `**`
     * (multi-segment wildcard).  Implemented as JS so it shares the
     * regex engine and stays compact (~25 LOC). */
    static const char *matches_glob_src =
        "(function(path, pattern) {\n"
        "  function esc(s) { return s.replace(/[.+^$()|{}\\\\]/g, '\\\\$&'); }\n"
        "  var re = '^', i = 0, n = pattern.length;\n"
        "  while (i < n) {\n"
        "    var c = pattern.charAt(i);\n"
        "    if (c === '*') {\n"
        "      if (pattern.charAt(i+1) === '*') {\n"
        "        re += '.*'; i += 2;\n"
        "        if (pattern.charAt(i) === '/') i++;\n"
        "      } else { re += '[^/]*'; i++; }\n"
        "    } else if (c === '?') { re += '[^/]'; i++; }\n"
        "    else if (c === '[') {\n"
        "      var end = pattern.indexOf(']', i);\n"
        "      if (end < 0) { re += '\\\\['; i++; }\n"
        "      else { re += pattern.slice(i, end+1).replace(/^\\[!/, '[^');"
        "             i = end + 1; }\n"
        "    } else if (c === '/') { re += '/'; i++; }\n"
        "    else { re += esc(c); i++; }\n"
        "  }\n"
        "  return new RegExp(re + '$').test(path);\n"
        "})";

    duk_eval_string(ctx, matches_glob_src);
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, posix_idx, "matchesGlob");
    duk_put_prop_string(ctx, win32_idx, "matchesGlob");

    /* pop win32; leave posix on stack as the returned object */
    duk_remove(ctx, win32_idx);
}

#if 0  /* obsolete inline-JS path implementation, kept as comment for reference */
static const char *path_js_obsolete =
"function(_natives) {\n"
"  'use strict';\n"
"  var CHAR_DOT = 46;\n"
"  var CHAR_FORWARD_SLASH = 47;\n"
"\n"
"  function validateString(value, name) {\n"
"    if (typeof value !== 'string') {\n"
"      var t = value === null ? 'null' : typeof value;\n"
"      var e = new TypeError('The \"' + name + '\" argument must be of type string. Received type ' + t);\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"  }\n"
"\n"
"  function isPosixPathSeparator(code) { return code === CHAR_FORWARD_SLASH; }\n"
"\n"
"  function normalizeString(path, allowAboveRoot, separator, isPathSeparator) {\n"
"    var res = '';\n"
"    var lastSegmentLength = 0;\n"
"    var lastSlash = -1;\n"
"    var dots = 0;\n"
"    var code = 0;\n"
"    for (var i = 0; i <= path.length; ++i) {\n"
"      if (i < path.length) code = path.charCodeAt(i);\n"
"      else if (isPathSeparator(code)) break;\n"
"      else code = CHAR_FORWARD_SLASH;\n"
"      if (isPathSeparator(code)) {\n"
"        if (lastSlash === i - 1 || dots === 1) {\n"
"          /* noop */\n"
"        } else if (dots === 2) {\n"
"          if (res.length < 2 || lastSegmentLength !== 2 ||\n"
"              res.charCodeAt(res.length - 1) !== CHAR_DOT ||\n"
"              res.charCodeAt(res.length - 2) !== CHAR_DOT) {\n"
"            if (res.length > 2) {\n"
"              var lastSlashIndex = res.lastIndexOf(separator);\n"
"              if (lastSlashIndex === -1) {\n"
"                res = '';\n"
"                lastSegmentLength = 0;\n"
"              } else {\n"
"                res = res.slice(0, lastSlashIndex);\n"
"                lastSegmentLength = res.length - 1 - res.lastIndexOf(separator);\n"
"              }\n"
"              lastSlash = i;\n"
"              dots = 0;\n"
"              continue;\n"
"            } else if (res.length !== 0) {\n"
"              res = '';\n"
"              lastSegmentLength = 0;\n"
"              lastSlash = i;\n"
"              dots = 0;\n"
"              continue;\n"
"            }\n"
"          }\n"
"          if (allowAboveRoot) {\n"
"            res += res.length > 0 ? separator + '..' : '..';\n"
"            lastSegmentLength = 2;\n"
"          }\n"
"        } else {\n"
"          if (res.length > 0)\n"
"            res += separator + path.slice(lastSlash + 1, i);\n"
"          else\n"
"            res = path.slice(lastSlash + 1, i);\n"
"          lastSegmentLength = i - lastSlash - 1;\n"
"        }\n"
"        lastSlash = i;\n"
"        dots = 0;\n"
"      } else if (code === CHAR_DOT && dots !== -1) {\n"
"        ++dots;\n"
"      } else {\n"
"        dots = -1;\n"
"      }\n"
"    }\n"
"    return res;\n"
"  }\n"
"\n"
"  function _format(sep, pathObject) {\n"
"    var dir = pathObject.dir || pathObject.root;\n"
"    var base = pathObject.base || ((pathObject.name || '') + (pathObject.ext || ''));\n"
"    if (!dir) return base;\n"
"    return dir === pathObject.root ? dir + base : dir + sep + base;\n"
"  }\n"
"\n"
"  var posix = {\n"
"    resolve: function() {\n"
"      var resolvedPath = '';\n"
"      var resolvedAbsolute = false;\n"
"      for (var i = arguments.length - 1; i >= -1 && !resolvedAbsolute; i--) {\n"
"        var path;\n"
"        if (i >= 0) {\n"
"          path = arguments[i];\n"
"          validateString(path, 'path');\n"
"          if (path.length === 0) continue;\n"
"        } else {\n"
"          path = _natives.cwd();\n"
"        }\n"
"        resolvedPath = path + '/' + resolvedPath;\n"
"        resolvedAbsolute = path.charCodeAt(0) === CHAR_FORWARD_SLASH;\n"
"      }\n"
"      resolvedPath = normalizeString(resolvedPath, !resolvedAbsolute, '/', isPosixPathSeparator);\n"
"      if (resolvedAbsolute) return '/' + resolvedPath;\n"
"      return resolvedPath.length > 0 ? resolvedPath : '.';\n"
"    },\n"
"\n"
"    normalize: function(path) {\n"
"      validateString(path, 'path');\n"
"      if (path.length === 0) return '.';\n"
"      var isAbsolute = path.charCodeAt(0) === CHAR_FORWARD_SLASH;\n"
"      var trailingSeparator = path.charCodeAt(path.length - 1) === CHAR_FORWARD_SLASH;\n"
"      path = normalizeString(path, !isAbsolute, '/', isPosixPathSeparator);\n"
"      if (path.length === 0) {\n"
"        if (isAbsolute) return '/';\n"
"        return trailingSeparator ? './' : '.';\n"
"      }\n"
"      if (trailingSeparator) path += '/';\n"
"      return isAbsolute ? '/' + path : path;\n"
"    },\n"
"\n"
"    isAbsolute: function(path) {\n"
"      validateString(path, 'path');\n"
"      return path.length > 0 && path.charCodeAt(0) === CHAR_FORWARD_SLASH;\n"
"    },\n"
"\n"
"    join: function() {\n"
"      if (arguments.length === 0) return '.';\n"
"      var joined;\n"
"      for (var i = 0; i < arguments.length; ++i) {\n"
"        var arg = arguments[i];\n"
"        validateString(arg, 'path');\n"
"        if (arg.length > 0) {\n"
"          if (joined === undefined) joined = arg;\n"
"          else joined += '/' + arg;\n"
"        }\n"
"      }\n"
"      if (joined === undefined) return '.';\n"
"      return posix.normalize(joined);\n"
"    },\n"
"\n"
"    relative: function(from, to) {\n"
"      validateString(from, 'from');\n"
"      validateString(to, 'to');\n"
"      if (from === to) return '';\n"
"      from = posix.resolve(from);\n"
"      to = posix.resolve(to);\n"
"      if (from === to) return '';\n"
"      var fromStart = 1;\n"
"      var fromEnd = from.length;\n"
"      var fromLen = fromEnd - fromStart;\n"
"      var toStart = 1;\n"
"      var toLen = to.length - toStart;\n"
"      var length = fromLen < toLen ? fromLen : toLen;\n"
"      var lastCommonSep = -1;\n"
"      var i = 0;\n"
"      for (; i < length; i++) {\n"
"        var fromCode = from.charCodeAt(fromStart + i);\n"
"        if (fromCode !== to.charCodeAt(toStart + i)) break;\n"
"        else if (fromCode === CHAR_FORWARD_SLASH) lastCommonSep = i;\n"
"      }\n"
"      if (i === length) {\n"
"        if (toLen > length) {\n"
"          if (to.charCodeAt(toStart + i) === CHAR_FORWARD_SLASH) return to.slice(toStart + i + 1);\n"
"          if (i === 0) return to.slice(toStart + i);\n"
"        } else if (fromLen > length) {\n"
"          if (from.charCodeAt(fromStart + i) === CHAR_FORWARD_SLASH) lastCommonSep = i;\n"
"          else if (i === 0) lastCommonSep = 0;\n"
"        }\n"
"      }\n"
"      var out = '';\n"
"      for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {\n"
"        if (i === fromEnd || from.charCodeAt(i) === CHAR_FORWARD_SLASH)\n"
"          out += out.length === 0 ? '..' : '/..';\n"
"      }\n"
"      return out + to.slice(toStart + lastCommonSep);\n"
"    },\n"
"\n"
"    toNamespacedPath: function(path) { return path; },\n"
"\n"
"    dirname: function(path) {\n"
"      validateString(path, 'path');\n"
"      if (path.length === 0) return '.';\n"
"      var hasRoot = path.charCodeAt(0) === CHAR_FORWARD_SLASH;\n"
"      var end = -1;\n"
"      var matchedSlash = true;\n"
"      for (var i = path.length - 1; i >= 1; --i) {\n"
"        if (path.charCodeAt(i) === CHAR_FORWARD_SLASH) {\n"
"          if (!matchedSlash) { end = i; break; }\n"
"        } else {\n"
"          matchedSlash = false;\n"
"        }\n"
"      }\n"
"      if (end === -1) return hasRoot ? '/' : '.';\n"
"      if (hasRoot && end === 1) return '//';\n"
"      return path.slice(0, end);\n"
"    },\n"
"\n"
"    basename: function(path, suffix) {\n"
"      if (suffix !== undefined) validateString(suffix, 'ext');\n"
"      validateString(path, 'path');\n"
"      var start = 0;\n"
"      var end = -1;\n"
"      var matchedSlash = true;\n"
"      var i;\n"
"      if (suffix !== undefined && suffix.length > 0 && suffix.length <= path.length) {\n"
"        if (suffix === path) return '';\n"
"        var extIdx = suffix.length - 1;\n"
"        var firstNonSlashEnd = -1;\n"
"        for (i = path.length - 1; i >= 0; --i) {\n"
"          var code = path.charCodeAt(i);\n"
"          if (code === CHAR_FORWARD_SLASH) {\n"
"            if (!matchedSlash) { start = i + 1; break; }\n"
"          } else {\n"
"            if (firstNonSlashEnd === -1) {\n"
"              matchedSlash = false;\n"
"              firstNonSlashEnd = i + 1;\n"
"            }\n"
"            if (extIdx >= 0) {\n"
"              if (code === suffix.charCodeAt(extIdx)) {\n"
"                if (--extIdx === -1) end = i;\n"
"              } else {\n"
"                extIdx = -1;\n"
"                end = firstNonSlashEnd;\n"
"              }\n"
"            }\n"
"          }\n"
"        }\n"
"        if (start === end) end = firstNonSlashEnd;\n"
"        else if (end === -1) end = path.length;\n"
"        return path.slice(start, end);\n"
"      }\n"
"      for (i = path.length - 1; i >= 0; --i) {\n"
"        if (path.charCodeAt(i) === CHAR_FORWARD_SLASH) {\n"
"          if (!matchedSlash) { start = i + 1; break; }\n"
"        } else if (end === -1) {\n"
"          matchedSlash = false;\n"
"          end = i + 1;\n"
"        }\n"
"      }\n"
"      if (end === -1) return '';\n"
"      return path.slice(start, end);\n"
"    },\n"
"\n"
"    extname: function(path) {\n"
"      validateString(path, 'path');\n"
"      var startDot = -1;\n"
"      var startPart = 0;\n"
"      var end = -1;\n"
"      var matchedSlash = true;\n"
"      var preDotState = 0;\n"
"      for (var i = path.length - 1; i >= 0; --i) {\n"
"        var code = path.charCodeAt(i);\n"
"        if (code === CHAR_FORWARD_SLASH) {\n"
"          if (!matchedSlash) { startPart = i + 1; break; }\n"
"          continue;\n"
"        }\n"
"        if (end === -1) {\n"
"          matchedSlash = false;\n"
"          end = i + 1;\n"
"        }\n"
"        if (code === CHAR_DOT) {\n"
"          if (startDot === -1) startDot = i;\n"
"          else if (preDotState !== 1) preDotState = 1;\n"
"        } else if (startDot !== -1) {\n"
"          preDotState = -1;\n"
"        }\n"
"      }\n"
"      if (startDot === -1 || end === -1 || preDotState === 0 ||\n"
"          (preDotState === 1 && startDot === end - 1 && startDot === startPart + 1)) {\n"
"        return '';\n"
"      }\n"
"      return path.slice(startDot, end);\n"
"    },\n"
"\n"
"    format: function(pathObject) {\n"
"      if (pathObject === null || typeof pathObject !== 'object') {\n"
"        var t = pathObject === null ? 'null' : typeof pathObject;\n"
"        var e = new TypeError('The \"pathObject\" argument must be of type object. Received type ' + t);\n"
"        e.code = 'ERR_INVALID_ARG_TYPE';\n"
"        throw e;\n"
"      }\n"
"      return _format('/', pathObject);\n"
"    },\n"
"\n"
"    parse: function(path) {\n"
"      validateString(path, 'path');\n"
"      var ret = { root: '', dir: '', base: '', ext: '', name: '' };\n"
"      if (path.length === 0) return ret;\n"
"      var isAbsolute = path.charCodeAt(0) === CHAR_FORWARD_SLASH;\n"
"      var start;\n"
"      if (isAbsolute) { ret.root = '/'; start = 1; } else { start = 0; }\n"
"      var startDot = -1;\n"
"      var startPart = 0;\n"
"      var end = -1;\n"
"      var matchedSlash = true;\n"
"      var i = path.length - 1;\n"
"      var preDotState = 0;\n"
"      for (; i >= start; --i) {\n"
"        var code = path.charCodeAt(i);\n"
"        if (code === CHAR_FORWARD_SLASH) {\n"
"          if (!matchedSlash) { startPart = i + 1; break; }\n"
"          continue;\n"
"        }\n"
"        if (end === -1) {\n"
"          matchedSlash = false;\n"
"          end = i + 1;\n"
"        }\n"
"        if (code === CHAR_DOT) {\n"
"          if (startDot === -1) startDot = i;\n"
"          else if (preDotState !== 1) preDotState = 1;\n"
"        } else if (startDot !== -1) {\n"
"          preDotState = -1;\n"
"        }\n"
"      }\n"
"      if (end !== -1) {\n"
"        var startCalc = startPart === 0 && isAbsolute ? 1 : startPart;\n"
"        if (startDot === -1 || end === -1 || preDotState === 0 ||\n"
"            (preDotState === 1 && startDot === end - 1 && startDot === startPart + 1)) {\n"
"          ret.base = ret.name = path.slice(startCalc, end);\n"
"        } else {\n"
"          ret.name = path.slice(startCalc, startDot);\n"
"          ret.base = path.slice(startCalc, end);\n"
"          ret.ext = path.slice(startDot, end);\n"
"        }\n"
"      }\n"
"      if (startPart > 0) ret.dir = path.slice(0, startPart - 1);\n"
"      else if (isAbsolute) ret.dir = '/';\n"
"      return ret;\n"
"    },\n"
"\n"
"    sep: '/',\n"
"    delimiter: ':',\n"
"    win32: null,\n"
"    posix: null\n"
"  };\n"
"\n"
"  posix.posix = posix;\n"
"  posix.win32 = posix;\n"
"  return posix;\n"
"}";
#endif

/* ============================================================
 * fs — node's filesystem module, wrapping rampart.utils.
 *
 * Covers: sync, callback, and promise forms of the common ops:
 *   readFile / writeFile / appendFile
 *   stat / lstat / exists / access
 *   mkdir / rmdir / rm / unlink / readdir
 *   copyFile / rename / chmod / chown / realpath / link / symlink
 *   plus Stats class, Dirent class, constants.
 *
 * Each op has *Sync (sync), bare-name (callback), and a mirror in
 * fs.promises that returns a Promise. The async wrappers fake async
 * via setTimeout(fn, 0) — there's no real I/O thread pool here, but
 * the API surface and callback timing match node closely enough that
 * code that just awaits/callbacks reads correctly.
 *
 * Deferred (will be filled in by later fs-extras phases):
 *   createReadStream / createWriteStream — needs `stream`
 *   open / close / read / write (fd-based) — Phase 2
 *   ftruncate / fsync / fdatasync / fstat / fchmod / fchown / futimes — Phase 2
 *   cp / glob / opendir — Phase 3
 *   watch / watchFile / FSWatcher — Phase 4
 * ============================================================ */

static const char *fs_js =
"function() {\n"
"  'use strict';\n"
"  var u = (typeof rampart !== 'undefined' && rampart.utils) ? rampart.utils : null;\n"
"  function _need() {\n"
"    if (!u) throw new Error('rampart.utils not available — fs cannot operate');\n"
"  }\n"
"\n"
"  /* Normalize a node-style path arg (string | Buffer | URL with file://) */\n"
"  function _path(p) {\n"
"    if (typeof p === 'string') return p;\n"
"    if (Buffer.isBuffer(p)) return p.toString('utf8');\n"
"    if (p && typeof p.href === 'string' && p.href.indexOf('file://') === 0)\n"
"      return p.href.slice(7);\n"
"    if (p && typeof p.pathname === 'string') return p.pathname;\n"
"    var e = new TypeError('The \"path\" argument must be of type string, Buffer, or URL');\n"
"    e.code = 'ERR_INVALID_ARG_TYPE';\n"
"    throw e;\n"
"  }\n"
"\n"
"  /* Build a node-style fs error from a thrown rampart error */\n"
"  function _fsErr(err, syscall, path) {\n"
"    var msg = (err && err.message) ? err.message : String(err);\n"
"    /* Try to extract errno-style code from message text */\n"
"    var code = 'EUNKNOWN';\n"
"    var m = /(E[A-Z]{2,})/.exec(msg);\n"
"    if (m) code = m[1];\n"
"    if (/No such file or directory|cannot stat|cannot open|does not exist/i.test(msg)) code = 'ENOENT';\n"
"    else if (/[Pp]ermission denied|EACCES/.test(msg)) code = 'EACCES';\n"
"    else if (/exists|EEXIST/i.test(msg)) code = 'EEXIST';\n"
"    else if (/Not a directory|ENOTDIR/.test(msg)) code = 'ENOTDIR';\n"
"    else if (/Is a directory|EISDIR/.test(msg)) code = 'EISDIR';\n"
"    var e = new Error(code + ': ' + (syscall ? syscall + ' ' : '') + (path ? \"'\" + path + \"' \" : '') + msg);\n"
"    e.code = code;\n"
"    e.syscall = syscall;\n"
"    if (path) e.path = path;\n"
"    return e;\n"
"  }\n"
"\n"
"  /* ----- Stats class ----- */\n"
"  function Stats(raw) {\n"
"    this.dev = raw.dev; this.ino = raw.ino; this.mode = raw.mode;\n"
"    this.nlink = raw.nlink; this.uid = raw.uid; this.gid = raw.gid;\n"
"    this.rdev = raw.rdev; this.size = raw.size;\n"
"    this.blksize = raw.blksize; this.blocks = raw.blocks;\n"
"    /* rampart returns ISO strings or Date objects; normalize */\n"
"    var atime = new Date(raw.atime), mtime = new Date(raw.mtime), ctime = new Date(raw.ctime);\n"
"    this.atime = atime; this.mtime = mtime; this.ctime = ctime; this.birthtime = ctime;\n"
"    this.atimeMs = atime.getTime();\n"
"    this.mtimeMs = mtime.getTime();\n"
"    this.ctimeMs = ctime.getTime();\n"
"    this.birthtimeMs = this.ctimeMs;\n"
"    this.atimeNs = this.atimeMs * 1e6;\n"
"    this.mtimeNs = this.mtimeMs * 1e6;\n"
"    this.ctimeNs = this.ctimeMs * 1e6;\n"
"    this.birthtimeNs = this.birthtimeMs * 1e6;\n"
"    this._raw = raw;\n"
"  }\n"
"  Stats.prototype.isFile            = function() { return !!this._raw.isFile; };\n"
"  Stats.prototype.isDirectory       = function() { return !!this._raw.isDirectory; };\n"
"  Stats.prototype.isBlockDevice     = function() { return !!this._raw.isBlockDevice; };\n"
"  Stats.prototype.isCharacterDevice = function() { return !!this._raw.isCharacterDevice; };\n"
"  Stats.prototype.isFIFO            = function() { return !!this._raw.isFIFO; };\n"
"  Stats.prototype.isSocket          = function() { return !!this._raw.isSocket; };\n"
"  Stats.prototype.isSymbolicLink    = function() {\n"
"    /* rampart's stat follows symlinks; lstat returns isSymlink */\n"
"    return !!(this._raw.isSymlink || this._raw.isSymbolicLink);\n"
"  };\n"
"\n"
"  /* ----- Dirent class ----- */\n"
"  function Dirent(name, parentPath) {\n"
"    this.name = name;\n"
"    this.parentPath = parentPath;\n"
"    this.path = parentPath;  /* deprecated node alias */\n"
"    this._stat = null;\n"
"  }\n"
"  Dirent.prototype._getStat = function() {\n"
"    if (this._stat) return this._stat;\n"
"    try {\n"
"      var p = this.parentPath ? (this.parentPath.replace(/\\/$/, '') + '/' + this.name) : this.name;\n"
"      this._stat = u.lstat(p);\n"
"    } catch (e) { this._stat = {}; }\n"
"    return this._stat;\n"
"  };\n"
"  Dirent.prototype.isFile            = function() { return !!this._getStat().isFile; };\n"
"  Dirent.prototype.isDirectory       = function() { return !!this._getStat().isDirectory; };\n"
"  Dirent.prototype.isBlockDevice     = function() { return !!this._getStat().isBlockDevice; };\n"
"  Dirent.prototype.isCharacterDevice = function() { return !!this._getStat().isCharacterDevice; };\n"
"  Dirent.prototype.isFIFO            = function() { return !!this._getStat().isFIFO; };\n"
"  Dirent.prototype.isSocket          = function() { return !!this._getStat().isSocket; };\n"
"  Dirent.prototype.isSymbolicLink    = function() { var s = this._getStat(); return !!(s.isSymlink || s.isSymbolicLink); };\n"
"\n"
"  /* ----- Sync ops ----- */\n"
"\n"
"  /* rampart.utils.stat/lstat return `false` for missing paths instead of\n"
"     throwing. Translate that into a node-style ENOENT error. */\n"
"  /* Throws ENOENT on missing-file unless opts.throwIfNoEntry === false,\n"
"     in which case returns undefined (newer node behavior). */\n"
"  function _statOrThrow(fn, syscall, p, opts) {\n"
"    var raw;\n"
"    try { raw = fn(p); }\n"
"    catch (e) { throw _fsErr(e, syscall, p); }\n"
"    if (!raw || typeof raw !== 'object') {\n"
"      if (opts && opts.throwIfNoEntry === false) return undefined;\n"
"      var e = new Error('ENOENT: no such file or directory, ' + syscall + \" '\" + p + \"'\");\n"
"      e.code = 'ENOENT'; e.errno = -2; e.syscall = syscall; e.path = p;\n"
"      throw e;\n"
"    }\n"
"    return raw;\n"
"  }\n"
"  function statSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var raw = _statOrThrow(u.stat, 'stat', p, opts);\n"
"    return raw === undefined ? undefined : new Stats(raw);\n"
"  }\n"
"  function lstatSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var raw = _statOrThrow(u.lstat, 'lstat', p, opts);\n"
"    return raw === undefined ? undefined : new Stats(raw);\n"
"  }\n"
"\n"
"  function readFileSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var encoding = null;\n"
"    if (typeof opts === 'string') encoding = opts;\n"
"    else if (opts && opts.encoding) encoding = opts.encoding;\n"
"    var data;\n"
"    try { data = u.readFile(p); }\n"
"    catch (e) { throw _fsErr(e, 'open', p); }\n"
"    if (!Buffer.isBuffer(data)) data = Buffer.from(data);\n"
"    return encoding ? data.toString(encoding) : data;\n"
"  }\n"
"\n"
"  function writeFileSync(path, data, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var encoding = 'utf8';\n"
"    var flag = 'w';\n"
"    var mode = -1;\n"
"    if (typeof opts === 'string') encoding = opts;\n"
"    else if (opts && typeof opts === 'object') {\n"
"      if (opts.encoding) encoding = opts.encoding;\n"
"      if (opts.flag) flag = opts.flag;\n"
"      if (opts.mode !== undefined) mode = opts.mode;\n"
"    }\n"
"    /* Translate node flags to fopen modes (best effort) */\n"
"    var fopenMode = 'w';\n"
"    if (flag === 'w' || flag === 'w+') fopenMode = flag === 'w+' ? 'w+' : 'w';\n"
"    else if (flag === 'a' || flag === 'a+') fopenMode = flag === 'a+' ? 'a+' : 'a';\n"
"    else if (flag === 'r+') fopenMode = 'r+';\n"
"    else if (flag === 'wx' || flag === 'wx+') fopenMode = flag === 'wx+' ? 'w+' : 'w';\n"
"    var bytes;\n"
"    if (Buffer.isBuffer(data)) bytes = data;\n"
"    else if (data instanceof Uint8Array) bytes = Buffer.from(data);\n"
"    else if (typeof data === 'string') bytes = Buffer.from(data, encoding);\n"
"    else bytes = Buffer.from(String(data), encoding);\n"
"    var fh;\n"
"    try {\n"
"      fh = u.fopen(p, fopenMode);\n"
"      if (bytes.length > 0) u.fwrite(fh, bytes);\n"
"      u.fclose(fh);\n"
"    } catch (e) {\n"
"      try { if (fh) u.fclose(fh); } catch (_) {}\n"
"      throw _fsErr(e, 'open', p);\n"
"    }\n"
"    if (mode >= 0) {\n"
"      try { u.chmod(p, mode); } catch (e) { /* non-fatal */ }\n"
"    }\n"
"  }\n"
"\n"
"  function appendFileSync(path, data, opts) {\n"
"    var actual = opts;\n"
"    if (typeof opts === 'string') actual = { encoding: opts, flag: 'a' };\n"
"    else if (opts && typeof opts === 'object') {\n"
"      actual = {};\n"
"      for (var k in opts) if (Object.prototype.hasOwnProperty.call(opts, k)) actual[k] = opts[k];\n"
"      actual.flag = actual.flag || 'a';\n"
"    } else {\n"
"      actual = { flag: 'a' };\n"
"    }\n"
"    writeFileSync(path, data, actual);\n"
"  }\n"
"\n"
"  function existsSync(path) {\n"
"    if (!u) return false;\n"
"    try {\n"
"      var s = u.stat(_path(path));\n"
"      /* rampart.utils.stat returns `false` for non-existent paths instead\n"
"         of throwing. Treat any non-object result as 'does not exist'. */\n"
"      return !!s && typeof s === 'object';\n"
"    } catch (e) { return false; }\n"
"  }\n"
"\n"
"  function accessSync(path, mode) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    if (mode === undefined) mode = 0;  /* F_OK */\n"
"    var st = _statOrThrow(u.stat, 'access', p);\n"
"    var R = 4, W = 2, X = 1;\n"
"    if ((mode & R) && !st.readable)   { var e = new Error('EACCES: permission denied, access \\'' + p + '\\''); e.code = 'EACCES'; e.path = p; e.syscall = 'access'; throw e; }\n"
"    if ((mode & W) && !st.writable)   { var e = new Error('EACCES: permission denied, access \\'' + p + '\\''); e.code = 'EACCES'; e.path = p; e.syscall = 'access'; throw e; }\n"
"    if ((mode & X) && !st.executable) { var e = new Error('EACCES: permission denied, access \\'' + p + '\\''); e.code = 'EACCES'; e.path = p; e.syscall = 'access'; throw e; }\n"
"  }\n"
"\n"
"  function mkdirSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var recursive = false, mode = 0x1ff; /* 0o777 */\n"
"    if (typeof opts === 'number') mode = opts;\n"
"    else if (opts && typeof opts === 'object') {\n"
"      recursive = !!opts.recursive;\n"
"      if (opts.mode !== undefined) mode = opts.mode;\n"
"    }\n"
"    if (recursive) {\n"
"      var parts = p.split('/');\n"
"      var cur = (p.charAt(0) === '/') ? '' : '.';\n"
"      var firstCreated = null;\n"
"      for (var i = 0; i < parts.length; i++) {\n"
"        if (!parts[i]) continue;\n"
"        cur = cur + '/' + parts[i];\n"
"        try {\n"
"          u.mkDir(cur);\n"
"          if (firstCreated === null) firstCreated = cur;\n"
"        } catch (e) {\n"
"          /* assume exists; verify it's a dir */\n"
"          var s = u.stat(cur);\n"
"          if (!s || typeof s !== 'object' || !s.isDirectory)\n"
"            throw _fsErr(e, 'mkdir', cur);\n"
"        }\n"
"      }\n"
"      return firstCreated;\n"
"    }\n"
"    try { u.mkDir(p); }\n"
"    catch (e) { throw _fsErr(e, 'mkdir', p); }\n"
"  }\n"
"\n"
"  function rmdirSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.rmDir(p); }\n"
"    catch (e) { throw _fsErr(e, 'rmdir', p); }\n"
"  }\n"
"\n"
"  function rmSync(path, opts) {\n"
"    _need();\n"
"    opts = opts || {};\n"
"    var p = _path(path);\n"
"    var st;\n"
"    try { st = _statOrThrow(u.lstat, 'lstat', p); }\n"
"    catch (e) {\n"
"      if (opts.force && e.code === 'ENOENT') return;\n"
"      throw e;\n"
"    }\n"
"    if (st.isDirectory) {\n"
"      if (opts.recursive) {\n"
"        var entries = u.readDir(p);\n"
"        for (var i = 0; i < entries.length; i++) {\n"
"          if (entries[i] === '.' || entries[i] === '..') continue;\n"
"          rmSync(p + '/' + entries[i], opts);\n"
"        }\n"
"      }\n"
"      try { u.rmDir(p); }\n"
"      catch (e) { if (!opts.force) throw _fsErr(e, 'rmdir', p); }\n"
"    } else {\n"
"      try { u.rmFile(p); }\n"
"      catch (e) { if (!opts.force) throw _fsErr(e, 'unlink', p); }\n"
"    }\n"
"  }\n"
"\n"
"  function unlinkSync(path) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.rmFile(p); }\n"
"    catch (e) { throw _fsErr(e, 'unlink', p); }\n"
"  }\n"
"\n"
"  function readdirSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var withFileTypes = false, recursive = false;\n"
"    var encoding = 'utf8';\n"
"    if (typeof opts === 'string') encoding = opts;\n"
"    else if (opts && typeof opts === 'object') {\n"
"      if (opts.withFileTypes) withFileTypes = true;\n"
"      if (opts.encoding) encoding = opts.encoding;\n"
"      if (opts.recursive) recursive = true;\n"
"    }\n"
"    /* Recursive walk via rampart.utils.walkDir.  Skip the root entry,\n"
"       emit paths relative to `p` (matching node's fs.readdirSync\n"
"       {recursive:true} behavior). */\n"
"    if (recursive) {\n"
"      var rel = [];\n"
"      var prefixLen = p.length + (p.charAt(p.length-1) === '/' ? 0 : 1);\n"
"      try {\n"
"        u.walkDir(p, function(fullPath, type, depth) {\n"
"          if (depth === 0) return;  /* skip the root itself */\n"
"          var r = fullPath.slice(prefixLen);\n"
"          if (withFileTypes) {\n"
"            var lastSlash = r.lastIndexOf('/');\n"
"            var parentPath = (lastSlash < 0) ? p : (p + '/' + r.slice(0, lastSlash));\n"
"            var name = (lastSlash < 0) ? r : r.slice(lastSlash + 1);\n"
"            rel.push(new Dirent(name, parentPath));\n"
"          } else {\n"
"            rel.push(encoding === 'buffer' ? Buffer.from(r) : r);\n"
"          }\n"
"        });\n"
"      } catch (e) { throw _fsErr(e, 'scandir', p); }\n"
"      return rel;\n"
"    }\n"
"    var entries;\n"
"    try { entries = u.readDir(p); }\n"
"    catch (e) { throw _fsErr(e, 'scandir', p); }\n"
"    if (withFileTypes) {\n"
"      return entries.map(function(name) { return new Dirent(name, p); });\n"
"    }\n"
"    if (encoding === 'buffer') return entries.map(function(n) { return Buffer.from(n); });\n"
"    return entries;\n"
"  }\n"
"\n"
"  /* COPYFILE_EXCL: fail with EEXIST if dest exists.\n"
"     COPYFILE_FICLONE / COPYFILE_FICLONE_FORCE: would require ioctl_ficlone\n"
"     on Linux; we treat them as best-effort hints and fall through to a\n"
"     normal copy.  COPYFILE_FICLONE_FORCE strictly should fail when CoW\n"
"     unavailable; we don't enforce that. */\n"
"  function copyFileSync(src, dest, mode) {\n"
"    _need();\n"
"    var s = _path(src), d = _path(dest);\n"
"    var excl = mode && (mode & fs_constants_COPYFILE_EXCL);\n"
"    try {\n"
"      if (excl) {\n"
"        /* Probe first to surface EEXIST consistently with node */\n"
"        if (u.exists(d)) {\n"
"          var e = new Error(\"EEXIST: file already exists, copyfile '\" + s + \"' -> '\" + d + \"'\");\n"
"          e.code = 'EEXIST'; e.syscall = 'copyfile'; e.path = s; e.dest = d; throw e;\n"
"        }\n"
"        u.copyFile(s, d, false);  /* don't overwrite -- belt + suspenders */\n"
"      } else {\n"
"        u.copyFile(s, d, true);   /* allow overwrite, matching node's default */\n"
"      }\n"
"    } catch (e) {\n"
"      if (e.code) throw e;\n"
"      throw _fsErr(e, 'copyfile', s);\n"
"    }\n"
"  }\n"
"  var fs_constants_COPYFILE_EXCL = 1;  /* must match constants below */\n"
"  function renameSync(src, dest) {\n"
"    _need();\n"
"    var s = _path(src), d = _path(dest);\n"
"    try { u.rename(s, d); }\n"
"    catch (e) { throw _fsErr(e, 'rename', s); }\n"
"  }\n"
"  function chmodSync(path, mode) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.chmod(p, mode); }\n"
"    catch (e) { throw _fsErr(e, 'chmod', p); }\n"
"  }\n"
"  function chownSync(path, uid, gid) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.chown(p, uid, gid); }\n"
"    catch (e) { throw _fsErr(e, 'chown', p); }\n"
"  }\n"
"  /* Common encoding-option unpacker: returns 'buffer' or null. */\n"
"  function _enc(opts) {\n"
"    if (opts === 'buffer') return 'buffer';\n"
"    if (opts && typeof opts === 'object' && opts.encoding === 'buffer') return 'buffer';\n"
"    return null;\n"
"  }\n"
"\n"
"  function realpathSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var result;\n"
"    try { result = u.realPath(p); }\n"
"    catch (e) { throw _fsErr(e, 'realpath', p); }\n"
"    return _enc(opts) === 'buffer' ? Buffer.from(result) : result;\n"
"  }\n"
"  function linkSync(existing, newPath) {\n"
"    _need();\n"
"    try { u.link(_path(existing), _path(newPath)); }\n"
"    catch (e) { throw _fsErr(e, 'link', _path(existing)); }\n"
"  }\n"
"  function symlinkSync(target, path, _type) {\n"
"    _need();\n"
"    try { u.symlink(_path(target), _path(path)); }\n"
"    catch (e) { throw _fsErr(e, 'symlink', _path(target)); }\n"
"  }\n"
"  function lchmodSync(path, mode) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.lchmod(p, mode); }\n"
"    catch (e) { throw _fsErr(e, 'lchmod', p); }\n"
"  }\n"
"  function lchownSync(path, uid, gid) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.lchown(p, uid, gid); }\n"
"    catch (e) { throw _fsErr(e, 'lchown', p); }\n"
"  }\n"
"  function lutimesSync(path, atime, mtime) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try {\n"
"      u.lUtimes(p, {\n"
"        setaccess: _toEpochSecs(atime),\n"
"        setmodify: _toEpochSecs(mtime)\n"
"      });\n"
"    } catch (e) { throw _fsErr(e, 'lutimes', p); }\n"
"  }\n"
"  function readlinkSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var target;\n"
"    try { target = u.readLink(p); }\n"
"    catch (e) { throw _fsErr(e, 'readlink', p); }\n"
"    return _enc(opts) === 'buffer' ? Buffer.from(target) : target;\n"
"  }\n"
"\n"
"  function truncateSync(path, len) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.truncate(p, len || 0); }\n"
"    catch (e) { throw _fsErr(e, 'truncate', p); }\n"
"  }\n"
"\n"
"  /* utimes: node accepts atime/mtime as number (seconds), Date, or\n"
"     numeric-string. rampart.utils.touch handles number-or-Date directly. */\n"
"  function _toEpochSecs(v) {\n"
"    if (v instanceof Date) return v;\n"
"    if (typeof v === 'number') return v;\n"
"    if (typeof v === 'string') return Number(v);\n"
"    var e = new TypeError('atime/mtime must be a number, string, or Date'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"  }\n"
"  function utimesSync(path, atime, mtime) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    try { u.touch({path: p, setaccess: _toEpochSecs(atime), setmodify: _toEpochSecs(mtime), nocreate: true}); }\n"
"    catch (e) { throw _fsErr(e, 'utime', p); }\n"
"  }\n"
"\n"
"  function mkdtempSync(prefix, opts) {\n"
"    _need();\n"
"    var s = (typeof prefix === 'string') ? prefix : _path(prefix);\n"
"    var result;\n"
"    try { result = u.mkdTemp(s); }\n"
"    catch (e) { throw _fsErr(e, 'mkdtemp', s); }\n"
"    return _enc(opts) === 'buffer' ? Buffer.from(result) : result;\n"
"  }\n"
"\n"
"  /* StatFs class -- thin wrapper so `result instanceof fs.StatFs` works\n"
"     (matches node's class-returning behavior from fs.statfsSync). */\n"
"  function StatFs(sv) {\n"
"    this.type   = sv.flag;\n"
"    this.bsize  = sv.bsize;\n"
"    this.blocks = sv.blocks;\n"
"    this.bfree  = sv.bfree;\n"
"    this.bavail = sv.bavail;\n"
"    this.files  = sv.files;\n"
"    this.ffree  = sv.ffree;\n"
"  }\n"
"  function statfsSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var sv;\n"
"    try { sv = u.statVfs(p); }\n"
"    catch (e) { throw _fsErr(e, 'statfs', p); }\n"
"    return new StatFs(sv);\n"
"  }\n"
"\n"
"  /* ----- Phase 2: fd-based I/O via rampart.utils POSIX fd API ----- */\n"
"\n"
"  /* Translate node-style string flag ('r','w','a','r+','w+','a+','wx','wx+','ax')\n"
"     to a numeric open(2) bitmask using rampart.utils.O. Numeric flags pass through. */\n"
"  function _flagsToInt(flags) {\n"
"    if (typeof flags === 'number') return flags;\n"
"    if (flags === undefined) return u.O.RDONLY;\n"
"    var O = u.O;\n"
"    switch (flags) {\n"
"      case 'r':   return O.RDONLY;\n"
"      case 'r+':  return O.RDWR;\n"
"      case 'rs':  case 'sr':  return O.RDONLY | (O.SYNC || 0);\n"
"      case 'rs+': case 'sr+': return O.RDWR | (O.SYNC || 0);\n"
"      case 'w':   return O.WRONLY | O.CREAT | O.TRUNC;\n"
"      case 'wx':  case 'xw':  return O.WRONLY | O.CREAT | O.TRUNC | O.EXCL;\n"
"      case 'w+':  return O.RDWR | O.CREAT | O.TRUNC;\n"
"      case 'wx+': case 'xw+': return O.RDWR | O.CREAT | O.TRUNC | O.EXCL;\n"
"      case 'a':   return O.WRONLY | O.CREAT | O.APPEND;\n"
"      case 'ax':  case 'xa':  return O.WRONLY | O.CREAT | O.APPEND | O.EXCL;\n"
"      case 'a+':  return O.RDWR | O.CREAT | O.APPEND;\n"
"      case 'ax+': case 'xa+': return O.RDWR | O.CREAT | O.APPEND | O.EXCL;\n"
"    }\n"
"    var e = new TypeError(\"Unknown file open flag: '\" + flags + \"'\");\n"
"    e.code = 'ERR_INVALID_ARG_VALUE'; throw e;\n"
"  }\n"
"\n"
"  function openSync(path, flags, mode) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var fl = _flagsToInt(flags === undefined ? 'r' : flags);\n"
"    var m = (mode === undefined) ? 0x1a4 /* 0o644 */ : mode;\n"
"    try { return u.open(p, fl, m); }\n"
"    catch (e) { throw _fsErr(e, 'open', p); }\n"
"  }\n"
"\n"
"  function closeSync(fd) {\n"
"    _need();\n"
"    try { u.close(fd); }\n"
"    catch (e) { throw _fsErr(e, 'close'); }\n"
"  }\n"
"\n"
"  /* readSync(fd, buffer, offset, length, position) -> bytesRead.\n"
"     Newer alt form: readSync(fd, buffer, options) where options has\n"
"     {offset, length, position}.\n"
"     Reads INTO `buffer` starting at `offset` for up to `length` bytes. */\n"
"  function readSync(fd, buffer, offset, length, position) {\n"
"    _need();\n"
"    if (!Buffer.isBuffer(buffer) && !(buffer instanceof Uint8Array)) {\n"
"      var e = new TypeError('buffer must be a Buffer or Uint8Array');\n"
"      e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    /* Options-object form (3-arg) */\n"
"    if (offset && typeof offset === 'object' && length === undefined) {\n"
"      var opts = offset;\n"
"      offset = opts.offset || 0;\n"
"      length = (opts.length === undefined) ? (buffer.length - offset) : opts.length;\n"
"      position = opts.position;\n"
"    }\n"
"    if (offset === undefined || offset === null) offset = 0;\n"
"    if (length === undefined || length === null) length = buffer.length - offset;\n"
"    if (position === undefined) position = null;\n"
"    var got;\n"
"    try {\n"
"      got = (position === null) ? u.read(fd, length) : u.read(fd, length, position);\n"
"    } catch (e) { throw _fsErr(e, 'read'); }\n"
"    /* Copy into the caller's buffer */\n"
"    for (var i = 0; i < got.length; i++) buffer[offset + i] = got[i];\n"
"    return got.length;\n"
"  }\n"
"\n"
"  /* writeSync(fd, buffer, offset, length, position) -> bytesWritten.\n"
"     Alt: writeSync(fd, string, position, encoding) -> bytesWritten. */\n"
"  function writeSync(fd, data, p2, p3, p4) {\n"
"    _need();\n"
"    var payload, position;\n"
"    if (typeof data === 'string') {\n"
"      var encoding = p3 || 'utf8';\n"
"      position = (typeof p2 === 'number') ? p2 : null;\n"
"      payload = Buffer.from(data, encoding);\n"
"    } else if (Buffer.isBuffer(data) || (data instanceof Uint8Array)) {\n"
"      var offset = (p2 === undefined || p2 === null) ? 0 : p2;\n"
"      var length = (p3 === undefined || p3 === null) ? (data.length - offset) : p3;\n"
"      position = (p4 === undefined) ? null : p4;\n"
"      if (offset === 0 && length === data.length) {\n"
"        payload = data;\n"
"      } else {\n"
"        payload = Buffer.alloc(length);\n"
"        for (var i = 0; i < length; i++) payload[i] = data[offset + i];\n"
"      }\n"
"    } else {\n"
"      var e = new TypeError('data must be a String or Buffer');\n"
"      e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    try {\n"
"      return (position === null) ? u.write(fd, payload) : u.write(fd, payload, position);\n"
"    } catch (e) { throw _fsErr(e, 'write'); }\n"
"  }\n"
"\n"
"  function fsyncSync(fd) {\n"
"    _need();\n"
"    try { u.fsyncFd(fd); } catch (e) { throw _fsErr(e, 'fsync'); }\n"
"  }\n"
"  function fdatasyncSync(fd) {\n"
"    _need();\n"
"    try { u.fdatasyncFd(fd); } catch (e) { throw _fsErr(e, 'fdatasync'); }\n"
"  }\n"
"  function ftruncateSync(fd, len) {\n"
"    _need();\n"
"    try { u.ftruncateFd(fd, len || 0); } catch (e) { throw _fsErr(e, 'ftruncate'); }\n"
"  }\n"
"  function fstatSync(fd, opts) {\n"
"    _need();\n"
"    var raw;\n"
"    try { raw = u.fstatFd(fd); } catch (e) { throw _fsErr(e, 'fstat'); }\n"
"    return new Stats(raw);\n"
"  }\n"
"  function fchmodSync(fd, mode) {\n"
"    _need();\n"
"    try { u.fchmodFd(fd, mode); } catch (e) { throw _fsErr(e, 'fchmod'); }\n"
"  }\n"
"  function fchownSync(fd, uid, gid) {\n"
"    _need();\n"
"    try { u.fchownFd(fd, uid, gid); } catch (e) { throw _fsErr(e, 'fchown'); }\n"
"  }\n"
"  function futimesSync(fd, atime, mtime) {\n"
"    _need();\n"
"    try {\n"
"      u.futimesFd(fd, {\n"
"        setaccess: _toEpochSecs(atime),\n"
"        setmodify: _toEpochSecs(mtime)\n"
"      });\n"
"    } catch (e) { throw _fsErr(e, 'futime'); }\n"
"  }\n"
"\n"
"  /* ----- FileHandle class (used by fs.promises.open) ----- */\n"
"  function FileHandle(fd) { this.fd = fd; this._closed = false; }\n"
"  FileHandle.prototype._check = function() {\n"
"    if (this._closed) {\n"
"      var e = new Error('FileHandle has been closed'); e.code = 'EBADF'; throw e;\n"
"    }\n"
"  };\n"
"  FileHandle.prototype.close = function() {\n"
"    var self = this;\n"
"    return _makePromise(function() {\n"
"      if (self._closed) return undefined;\n"
"      self._closed = true;\n"
"      closeSync(self.fd);\n"
"    })();\n"
"  };\n"
"  FileHandle.prototype.read = function(buffer, offset, length, position) {\n"
"    var self = this;\n"
"    return _makePromise(function(b, o, l, p) {\n"
"      self._check();\n"
"      var n = readSync(self.fd, b, o, l, p);\n"
"      return { bytesRead: n, buffer: b };\n"
"    })(buffer, offset, length, position);\n"
"  };\n"
"  FileHandle.prototype.write = function(data, p2, p3, p4) {\n"
"    var self = this;\n"
"    return _makePromise(function(d, a, b, c) {\n"
"      self._check();\n"
"      var n = writeSync(self.fd, d, a, b, c);\n"
"      return { bytesWritten: n, buffer: d };\n"
"    })(data, p2, p3, p4);\n"
"  };\n"
"  FileHandle.prototype.stat = function(opts) {\n"
"    var self = this;\n"
"    return _makePromise(function() { self._check(); return fstatSync(self.fd, opts); })();\n"
"  };\n"
"  FileHandle.prototype.sync     = function()    { var s=this; return _makePromise(function(){s._check(); fsyncSync(s.fd);})(); };\n"
"  FileHandle.prototype.datasync = function()    { var s=this; return _makePromise(function(){s._check(); fdatasyncSync(s.fd);})(); };\n"
"  FileHandle.prototype.truncate = function(len) { var s=this; return _makePromise(function(l){s._check(); ftruncateSync(s.fd, l);})(len); };\n"
"  FileHandle.prototype.chmod    = function(m)   { var s=this; return _makePromise(function(x){s._check(); fchmodSync(s.fd, x);})(m); };\n"
"  FileHandle.prototype.chown    = function(u_,g){ var s=this; return _makePromise(function(uu,gg){s._check(); fchownSync(s.fd, uu, gg);})(u_, g); };\n"
"  FileHandle.prototype.utimes   = function(a,m) { var s=this; return _makePromise(function(aa,mm){s._check(); futimesSync(s.fd, aa, mm);})(a, m); };\n"
"  FileHandle.prototype.readFile = function(opts){\n"
"    var self = this;\n"
"    return _makePromise(function(o) {\n"
"      self._check();\n"
"      /* Stat then read whole file in one shot */\n"
"      var st = fstatSync(self.fd);\n"
"      var data;\n"
"      try { data = u.read(self.fd, st.size, 0); } catch(e){ throw _fsErr(e, 'read'); }\n"
"      var buf = Buffer.from(data);\n"
"      if (typeof o === 'string') return buf.toString(o);\n"
"      if (o && o.encoding) return buf.toString(o.encoding);\n"
"      return buf;\n"
"    })(opts);\n"
"  };\n"
"  FileHandle.prototype.writeFile = function(data, opts) {\n"
"    var self = this;\n"
"    return _makePromise(function(d, o) {\n"
"      self._check();\n"
"      var enc = (typeof o === 'string') ? o : (o && o.encoding) || 'utf8';\n"
"      var payload = (typeof d === 'string') ? Buffer.from(d, enc) : d;\n"
"      writeSync(self.fd, payload);\n"
"    })(data, opts);\n"
"  };\n"
"  FileHandle.prototype.appendFile = FileHandle.prototype.writeFile;\n"
"  /* FileHandle.createReadStream / createWriteStream -- wrap our existing\n"
"     ReadStream/WriteStream around the open fd by passing an `fd` option\n"
"     so they reuse the descriptor rather than re-opening the path. */\n"
"  FileHandle.prototype.createReadStream = function(opts) {\n"
"    var o = opts ? Object.assign({}, opts) : {};\n"
"    o.fd = this.fd;\n"
"    return new ReadStream(null, o);\n"
"  };\n"
"  FileHandle.prototype.createWriteStream = function(opts) {\n"
"    var o = opts ? Object.assign({}, opts) : {};\n"
"    o.fd = this.fd;\n"
"    return new WriteStream(null, o);\n"
"  };\n"
"\n"
"  /* ----- Phase 3: recursive helpers + opendir + streams ----- */\n"
"\n"
"  function cpSync(src, dest, opts) {\n"
"    _need();\n"
"    var s = _path(src), d = _path(dest);\n"
"    try { u.cp(s, d, opts || {recursive: false}); }\n"
"    catch (e) { throw _fsErr(e, 'cp', s); }\n"
"  }\n"
"\n"
"  /* globSync(pattern, options) supports node-style {cwd, exclude, withFileTypes}.\n"
"     `exclude` may be an array of glob patterns (matched against the relative\n"
"     path) or a function `fn(path) -> bool` returning true to exclude. */\n"
"  function globSync(pattern, opts) {\n"
"    _need();\n"
"    opts = opts || {};\n"
"    var matches;\n"
"    try { matches = u.glob(pattern, {cwd: opts.cwd || '.', dot: !!opts.dot}); }\n"
"    catch (e) { throw _fsErr(e, 'glob'); }\n"
"    var excludeFn = null;\n"
"    if (typeof opts.exclude === 'function') {\n"
"      excludeFn = opts.exclude;\n"
"    } else if (Array.isArray(opts.exclude)) {\n"
"      var excludes = opts.exclude;\n"
"      excludeFn = function(p) {\n"
"        for (var i = 0; i < excludes.length; i++) {\n"
"          /* Use rampart.utils.glob's own pattern matcher by re-globbing\n"
"             with cwd set to the parent of `p`.  Heavier than a true regex\n"
"             match, but consistent with the rest of the matching. */\n"
"          var hits = u.glob(excludes[i], {cwd: opts.cwd || '.'});\n"
"          for (var j = 0; j < hits.length; j++) if (hits[j] === p) return true;\n"
"        }\n"
"        return false;\n"
"      };\n"
"    }\n"
"    if (excludeFn) matches = matches.filter(function(p) { return !excludeFn(p); });\n"
"    if (opts.withFileTypes) {\n"
"      return matches.map(function(full) {\n"
"        var lastSlash = full.lastIndexOf('/');\n"
"        var parent = (lastSlash < 0) ? '.' : full.slice(0, lastSlash) || '/';\n"
"        var name   = (lastSlash < 0) ? full : full.slice(lastSlash + 1);\n"
"        return new Dirent(name, parent);\n"
"      });\n"
"    }\n"
"    return matches;\n"
"  }\n"
"\n"
"  function realpathSyncNative(path, opts) { return realpathSync(path, opts); }\n"
"\n"
"  /* opendir / Dir — iterator over a directory. Reads all entries\n"
"     up front (rampart.utils.readdir returns full list) and serves\n"
"     them one-at-a-time via .read() / for-await. */\n"
"  function Dir(path, entries) {\n"
"    this.path = path;\n"
"    /* Node's fs.opendir filters out '.' and '..' (POSIX readdir\n"
"       returns them; the Node wrapper skips them).  fs-extra and\n"
"       other libraries iterate dirents without checking — copying\n"
"       `..` would recurse into the parent directory.  Filter here\n"
"       so Dir.readSync()/read()/asyncIterator all skip them. */\n"
"    this._entries = entries.filter(function(n) { return n !== '.' && n !== '..'; })\n"
"                           .map(function(n) { return new Dirent(n, path); });\n"
"    this._closed = false;\n"
"    this._idx = 0;\n"
"  }\n"
"  Dir.prototype.read = function(cb) {\n"
"    var self = this;\n"
"    if (cb) {\n"
"      setTimeout(function() {\n"
"        try { cb(null, self._readSync()); }\n"
"        catch (e) { cb(e); }\n"
"      }, 0);\n"
"      return;\n"
"    }\n"
"    /* Promise form */\n"
"    var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"    if (!P) return self._readSync();\n"
"    return new P(function(res, rej) {\n"
"      setTimeout(function() { try { res(self._readSync()); } catch(e) { rej(e); } }, 0);\n"
"    });\n"
"  };\n"
"  Dir.prototype.readSync = function() { return this._readSync(); };\n"
"  Dir.prototype._readSync = function() {\n"
"    if (this._closed) { var e = new Error('Dir closed'); e.code = 'ERR_DIR_CLOSED'; throw e; }\n"
"    if (this._idx >= this._entries.length) return null;\n"
"    return this._entries[this._idx++];\n"
"  };\n"
"  Dir.prototype.close = function(cb) {\n"
"    this._closed = true;\n"
"    if (cb) { setTimeout(function() { cb(null); }, 0); return; }\n"
"    var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"    if (P) return new P(function(r) { r(); });\n"
"  };\n"
"  Dir.prototype.closeSync = function() { this._closed = true; };\n"
"  /* Async iterator -- enables `for await (const dirent of dir)`.\n"
"     Only present when Symbol.asyncIterator is available (i.e. under -t/-b\n"
"     where the polyfills install it).  Vanilla duktape predates the\n"
"     well-known symbol so an `if` guard avoids a load-time error. */\n"
"  if (typeof Symbol !== 'undefined' && Symbol.asyncIterator) {\n"
"    Dir.prototype[Symbol.asyncIterator] = function() {\n"
"      var self = this;\n"
"      var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"      return {\n"
"        next: function() {\n"
"          return new P(function(res, rej) {\n"
"            setTimeout(function() {\n"
"              try {\n"
"                var v = self._readSync();\n"
"                if (v === null) { res({value: undefined, done: true}); }\n"
"                else            { res({value: v, done: false}); }\n"
"              } catch (e) { rej(e); }\n"
"            }, 0);\n"
"          });\n"
"        },\n"
"        return: function() { self._closed = true; return P.resolve({value: undefined, done: true}); }\n"
"      };\n"
"    };\n"
"  }\n"
"\n"
"  function opendirSync(path, opts) {\n"
"    _need();\n"
"    var p = _path(path);\n"
"    var entries;\n"
"    try { entries = u.readdir(p, true); }  /* includes dotfiles */\n"
"    catch (e) { throw _fsErr(e, 'opendir', p); }\n"
"    return new Dir(p, entries);\n"
"  }\n"
"\n"
"  /* writev/readv: scatter/gather via the fd API in a loop */\n"
"  function writevSync(fd, buffers, position) {\n"
"    _need();\n"
"    var total = 0;\n"
"    for (var i = 0; i < buffers.length; i++) {\n"
"      var b = buffers[i];\n"
"      var n = (position === undefined || position === null)\n"
"        ? u.write(fd, b)\n"
"        : u.write(fd, b, position + total);\n"
"      total += n;\n"
"      if (n < b.length) break;\n"
"    }\n"
"    return total;\n"
"  }\n"
"  function readvSync(fd, buffers, position) {\n"
"    _need();\n"
"    var total = 0;\n"
"    for (var i = 0; i < buffers.length; i++) {\n"
"      var b = buffers[i];\n"
"      var got = (position === undefined || position === null)\n"
"        ? u.read(fd, b.length)\n"
"        : u.read(fd, b.length, position + total);\n"
"      for (var j = 0; j < got.length; j++) b[j] = got[j];\n"
"      total += got.length;\n"
"      if (got.length < b.length) break;\n"
"    }\n"
"    return total;\n"
"  }\n"
"\n"
"  /* Basic ReadStream / WriteStream — fd-backed, EventEmitter-like.\n"
"     This is NOT a full implementation of node's stream module; it\n"
"     provides the most common pattern (read/end events, write+end\n"
"     methods) so code that does plain file streaming works. */\n"
"  function _Emitter() { this._h = {}; }\n"
"  _Emitter.prototype.on = function(ev, cb) {\n"
"    (this._h[ev] = this._h[ev] || []).push(cb); return this;\n"
"  };\n"
"  _Emitter.prototype.emit = function(ev) {\n"
"    var hs = this._h[ev]; if (!hs) return;\n"
"    var args = Array.prototype.slice.call(arguments, 1);\n"
"    for (var i = 0; i < hs.length; i++) try { hs[i].apply(this, args); } catch (_) {}\n"
"  };\n"
"\n"
"  /* If opts.fd is set, the caller (typically FileHandle.createReadStream)\n"
"     passes in an already-open descriptor; we use it without opening from\n"
"     `path`, and we do NOT close it when the stream ends (the FileHandle\n"
"     owns it). */\n"
"  function ReadStream(path, opts) {\n"
"    _Emitter.call(this);\n"
"    var self = this;\n"
"    opts = opts || {};\n"
"    var chunk = opts.highWaterMark || 65536;\n"
"    var encoding = opts.encoding || null;\n"
"    var ownsFd = (opts.fd === undefined || opts.fd === null);\n"
"    var fd = ownsFd ? openSync(path, opts.flags || 'r') : opts.fd;\n"
"    self.path = path; self.fd = fd; self.bytesRead = 0;\n"
"    self.destroyed = false;\n"
"    self._fdClosed = false;\n"
"    /* done() and ReadStream.prototype.destroy both race to close fd:\n"
"       pump's done() fires on EOF / on destroyed-flag check; destroy()\n"
"       fires when something (npm's `destroy` package, called by `send`\n"
"       via on-finished/cleanup) explicitly closes the stream.  If both\n"
"       fire, the second close hits an fd that the kernel has likely\n"
"       reassigned to a freshly-accepted socket — corrupting libevent's\n"
"       epoll state.  Guard against double-close. */\n"
"    function done() {\n"
"      if (!self._fdClosed && ownsFd) {\n"
"        self._fdClosed = true;\n"
"        try { closeSync(fd); } catch(_){}\n"
"      }\n"
"    }\n"
"    setTimeout(function pump() {\n"
"      if (self.destroyed) { done(); return; }\n"
"      var b;\n"
"      try { b = u.read(fd, chunk); } catch (e) { self.emit('error', e); done(); return; }\n"
"      if (b.length === 0) { done(); self.emit('end'); self.emit('close'); return; }\n"
"      self.bytesRead += b.length;\n"
"      var emit = Buffer.from(b);\n"
"      if (encoding) emit = emit.toString(encoding);\n"
"      self.emit('data', emit);\n"
"      setTimeout(pump, 0);\n"
"    }, 0);\n"
"  }\n"
"  ReadStream.prototype = Object.create(_Emitter.prototype);\n"
"  /* Node-style .pipe(dest) — wire data → dest.write, end → dest.end,\n"
"     error → dest.emit('error').  Used by express.static / send / etc.\n"
"\n"
"     SHIM FAST-PATH: if dest is a rampart ServerResponse (duck-typed by\n"
"     the presence of _buildAndFlush) AND the response is still fresh\n"
"     (no prior writes, not ended), hand the file path to rampart-server\n"
"     via {bin:'@path'} so it can sendfile(2)/mmap directly.  This\n"
"     bypasses dozens of fs.read + JS pipe + Writable round-trips per\n"
"     request — the exact case Express's res.sendFile triggers. */\n"
"  ReadStream.prototype.pipe = function(dest, opts) {\n"
"    var self = this;\n"
"    if (dest && typeof dest._buildAndFlush === 'function'\n"
"     && dest._didWrite === false && dest._ended === false\n"
"     && typeof self.path === 'string' && self.path.length > 0) {\n"
"      dest._sendFilePath = self.path;\n"
"      /* Signal the pump to abort on its next tick.  Pump's done()\n"
"         (owned-fd path) closes our fd cleanly — we must not close\n"
"         it ourselves: a duplicate close races the kernel reusing\n"
"         the fd number for a freshly-accepted socket, breaking\n"
"         libevent's epoll state. */\n"
"      self.destroyed = true;\n"
"      /* Synchronously mark the response ended.  Since pipe() is\n"
"         called from within the user's request handler, the dispatcher\n"
"         (which runs immediately after the handler returns) sees\n"
"         res._ended=true and goes through the sync-buffered\n"
"         _buildAndFlush path.  No deferred mode, no setTimeout\n"
"         race, no extra event emits that could re-enter send's\n"
"         lifecycle hooks and double-close anything. */\n"
"      if (typeof dest.end === 'function') dest.end();\n"
"      if (typeof dest.emit === 'function') dest.emit('pipe', self);\n"
"      return dest;\n"
"    }\n"
"    opts = opts || {};\n"
"    self.on('data', function(chunk) {\n"
"      if (dest && typeof dest.write === 'function') dest.write(chunk);\n"
"    });\n"
"    self.on('end', function() {\n"
"      if (opts.end !== false && dest && typeof dest.end === 'function') dest.end();\n"
"    });\n"
"    self.on('error', function(err) {\n"
"      if (dest && typeof dest.emit === 'function') dest.emit('error', err);\n"
"    });\n"
"    if (dest && typeof dest.emit === 'function') dest.emit('pipe', self);\n"
"    return dest;\n"
"  };\n"
"  ReadStream.prototype.pause   = function() { return this; };\n"
"  ReadStream.prototype.resume  = function() { return this; };\n"
"  ReadStream.prototype.destroy = function(err) {\n"
"    this.destroyed = true;\n"
"    if (!this._fdClosed) {\n"
"      this._fdClosed = true;\n"
"      try { closeSync(this.fd); } catch(_){}\n"
"    }\n"
"    if (err) this.emit('error', err);\n"
"    this.emit('close');\n"
"    return this;\n"
"  };\n"
"\n"
"  function WriteStream(path, opts) {\n"
"    _Emitter.call(this);\n"
"    var self = this;\n"
"    opts = opts || {};\n"
"    var ownsFd = (opts.fd === undefined || opts.fd === null);\n"
"    var fd = ownsFd\n"
"      ? openSync(path, opts.flags || 'w', opts.mode || 0x1a4 /*0o644*/)\n"
"      : opts.fd;\n"
"    self.path = path; self.fd = fd; self.bytesWritten = 0;\n"
"    self._closed = false;\n"
"    self.write = function(chunk, encoding) {\n"
"      if (self._closed) { self.emit('error', new Error('write after end')); return false; }\n"
"      var b = (typeof chunk === 'string') ? Buffer.from(chunk, encoding || 'utf8') : chunk;\n"
"      try {\n"
"        var w = u.write(fd, b);\n"
"        self.bytesWritten += w;\n"
"        return true;\n"
"      } catch (e) { self.emit('error', e); return false; }\n"
"    };\n"
"    self.end = function(chunk, encoding) {\n"
"      if (chunk !== undefined) self.write(chunk, encoding);\n"
"      if (self._closed) return;\n"
"      self._closed = true;\n"
"      if (ownsFd) try { closeSync(fd); } catch(_){}\n"
"      self.emit('finish');\n"
"      self.emit('close');\n"
"    };\n"
"  }\n"
"  WriteStream.prototype = Object.create(_Emitter.prototype);\n"
"\n"
"  function createReadStream(path, opts)  { return new ReadStream(_path(path), opts); }\n"
"  function createWriteStream(path, opts) { return new WriteStream(_path(path), opts); }\n"
"\n"
"  /* node attaches .native variants of realpath; alias to the same impl */\n"
"  realpathSync.native = realpathSync;\n"
"\n"
"  /* ----- Phase 4: watch / watchFile / unwatchFile + FSWatcher / StatWatcher ----- */\n"
"\n"
"  function _basename(p) {\n"
"    var i = p.lastIndexOf('/');\n"
"    return (i < 0) ? p : p.slice(i + 1);\n"
"  }\n"
"\n"
"  function FSWatcher() { _Emitter.call(this); this._w = null; this._closed = false; }\n"
"  FSWatcher.prototype = Object.create(_Emitter.prototype);\n"
"  FSWatcher.prototype.close = function() {\n"
"    if (this._closed) return;\n"
"    this._closed = true;\n"
"    if (this._w) try { this._w.close(); } catch (_) {}\n"
"    this._w = null;\n"
"    this.emit('close');\n"
"  };\n"
"  FSWatcher.prototype.ref = function() { return this; };\n"
"  FSWatcher.prototype.unref = function() { return this; };\n"
"\n"
"  function watch(path, opts, listener) {\n"
"    if (typeof opts === 'function') { listener = opts; opts = undefined; }\n"
"    if (typeof opts === 'string') opts = {encoding: opts};\n"
"    opts = opts || {};\n"
"    _need();\n"
"    var watcher = new FSWatcher();\n"
"    var p = _path(path);\n"
"    try {\n"
"      var underlying = u.watch(p, function(ev) {\n"
"        /* Node maps inotify/kqueue events to 'change' or 'rename'.\n"
"           Approximate: create/delete/rename -> 'rename', else 'change'. */\n"
"        var type = (ev.type === 'change') ? 'change' : 'rename';\n"
"        var filename = _basename(ev.path);\n"
"        watcher.emit('change', type, filename);\n"
"        if (listener) try { listener(type, filename); } catch (_) {}\n"
"      });\n"
"      watcher._w = underlying;\n"
"    } catch (e) {\n"
"      var ferr = _fsErr(e, 'watch', p);\n"
"      setTimeout(function() { watcher.emit('error', ferr); }, 0);\n"
"    }\n"
"    return watcher;\n"
"  }\n"
"\n"
"  /* watchFile: always polling, callback signature is (curr, prev) Stats */\n"
"  var _statWatchers = {};\n"
"  function StatWatcher() { _Emitter.call(this); this._w = null; this._closed = false; }\n"
"  StatWatcher.prototype = Object.create(_Emitter.prototype);\n"
"  StatWatcher.prototype.start = function(path, interval, listener) {\n"
"    var self = this;\n"
"    var prev = null;\n"
"    try { prev = statSync(path); } catch (_) { /* may not exist */ }\n"
"    self._w = u.watch({path: path, poll: true, interval: interval}, function() {\n"
"      var curr;\n"
"      try { curr = statSync(path); } catch (_) { curr = null; }\n"
"      if (listener) try { listener(curr, prev); } catch (_) {}\n"
"      self.emit('change', curr, prev);\n"
"      prev = curr;\n"
"    });\n"
"  };\n"
"  StatWatcher.prototype.close = function() {\n"
"    if (this._closed) return;\n"
"    this._closed = true;\n"
"    if (this._w) try { this._w.close(); } catch (_) {}\n"
"    this._w = null;\n"
"  };\n"
"\n"
"  function watchFile(path, opts, listener) {\n"
"    if (typeof opts === 'function') { listener = opts; opts = undefined; }\n"
"    opts = opts || {};\n"
"    var interval = opts.interval || 5007;\n"
"    var p = _path(path);\n"
"    if (_statWatchers[p]) return _statWatchers[p];\n"
"    var sw = new StatWatcher();\n"
"    sw.start(p, interval, listener);\n"
"    _statWatchers[p] = sw;\n"
"    return sw;\n"
"  }\n"
"\n"
"  function unwatchFile(path, listener) {\n"
"    var p = _path(path);\n"
"    var sw = _statWatchers[p];\n"
"    if (sw) { sw.close(); delete _statWatchers[p]; }\n"
"  }\n"
"\n"
"  /* ----- Callback wrappers ----- */\n"
"  function _runAsync(syncFn, args, cb) {\n"
"    if (typeof cb !== 'function') {\n"
"      var e = new TypeError('Callback must be a function'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    setTimeout(function() {\n"
"      try { cb(null, syncFn.apply(null, args)); }\n"
"      catch (e) { cb(e); }\n"
"    }, 0);\n"
"  }\n"
"\n"
"  /* Most ops: (path, [opts], cb) — split off cb from end of args */\n"
"  function _makeAsync(syncFn) {\n"
"    return function() {\n"
"      var args = Array.prototype.slice.call(arguments);\n"
"      var cb = args.pop();\n"
"      _runAsync(syncFn, args, cb);\n"
"    };\n"
"  }\n"
"\n"
"  /* ----- AbortSignal helpers ----- */\n"
"  function _abortError(reason) {\n"
"    if (reason && (reason instanceof Error || (typeof reason === 'object' && reason.name))) return reason;\n"
"    if (typeof DOMException !== 'undefined') {\n"
"      try { return new DOMException(typeof reason === 'string' ? reason : 'The operation was aborted', 'AbortError'); } catch(_){}\n"
"    }\n"
"    var e = new Error(typeof reason === 'string' ? reason : 'The operation was aborted');\n"
"    e.name = 'AbortError';\n"
"    e.code = 'ABORT_ERR';\n"
"    return e;\n"
"  }\n"
"  function _extractSignal(opts) {\n"
"    if (opts && typeof opts === 'object' && opts.signal\n"
"        && typeof opts.signal.addEventListener === 'function') return opts.signal;\n"
"    return null;\n"
"  }\n"
"  /* Wrap a callback-style async op so it honors opts.signal.\n"
"     'optsIdx' is the index in args where opts live (after path/data). */\n"
"  function _signalAsync(syncFn, optsIdx) {\n"
"    return function() {\n"
"      var args = Array.prototype.slice.call(arguments);\n"
"      var cb = args.pop();\n"
"      if (typeof cb !== 'function') {\n"
"        var e = new TypeError('Callback must be a function');\n"
"        e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"      }\n"
"      var sig = _extractSignal(args[optsIdx]);\n"
"      if (!sig) { _runAsync(syncFn, args, cb); return; }\n"
"      if (sig.aborted) { setTimeout(function(){ cb(_abortError(sig.reason)); }, 0); return; }\n"
"      var done = false;\n"
"      function onAbort() {\n"
"        if (done) return; done = true;\n"
"        cb(_abortError(sig.reason));\n"
"      }\n"
"      sig.addEventListener('abort', onAbort, { once: true });\n"
"      setTimeout(function() {\n"
"        if (done) return;\n"
"        try {\n"
"          var r = syncFn.apply(null, args);\n"
"          if (done) return; done = true;\n"
"          try { sig.removeEventListener('abort', onAbort); } catch(_){}\n"
"          cb(null, r);\n"
"        } catch (err) {\n"
"          if (done) return; done = true;\n"
"          try { sig.removeEventListener('abort', onAbort); } catch(_){}\n"
"          cb(err);\n"
"        }\n"
"      }, 0);\n"
"    };\n"
"  }\n"
"  /* Promise variant of _signalAsync. */\n"
"  function _signalPromise(syncFn, optsIdx) {\n"
"    return function() {\n"
"      var args = arguments;\n"
"      try { if (typeof global !== 'undefined' && global._TrN_Sp && global._TrN_Sp._pP) global._TrN_Sp._pP(); } catch (_) {}\n"
"      var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"      if (!P) throw new Error('fs.promises: Promise not available');\n"
"      var sig = _extractSignal(args[optsIdx]);\n"
"      return new P(function(resolve, reject) {\n"
"        if (sig && sig.aborted) { setTimeout(function(){ reject(_abortError(sig.reason)); }, 0); return; }\n"
"        var done = false;\n"
"        function onAbort() {\n"
"          if (done) return; done = true;\n"
"          reject(_abortError(sig.reason));\n"
"        }\n"
"        if (sig) sig.addEventListener('abort', onAbort, { once: true });\n"
"        setTimeout(function() {\n"
"          if (done) return;\n"
"          try {\n"
"            var r = syncFn.apply(null, args);\n"
"            if (done) return; done = true;\n"
"            if (sig) try { sig.removeEventListener('abort', onAbort); } catch(_){}\n"
"            resolve(r);\n"
"          } catch (err) {\n"
"            if (done) return; done = true;\n"
"            if (sig) try { sig.removeEventListener('abort', onAbort); } catch(_){}\n"
"            reject(err);\n"
"          }\n"
"        }, 0);\n"
"      });\n"
"    };\n"
"  }\n"
"\n"
"  /* readFile(path, opts, cb): opts at index 1.\n"
"     writeFile/appendFile(path, data, opts, cb): opts at index 2. */\n"
"  var readFile      = _signalAsync(readFileSync, 1);\n"
"  var writeFile     = _signalAsync(writeFileSync, 2);\n"
"  var appendFile    = _signalAsync(appendFileSync, 2);\n"
"  var stat          = _makeAsync(statSync);\n"
"  var lstat         = _makeAsync(lstatSync);\n"
"  var access        = _makeAsync(accessSync);\n"
"  var mkdir         = _makeAsync(mkdirSync);\n"
"  var rmdir         = _makeAsync(rmdirSync);\n"
"  var rm            = _makeAsync(rmSync);\n"
"  var unlink        = _makeAsync(unlinkSync);\n"
"  var readdir       = _makeAsync(readdirSync);\n"
"  var copyFile      = _makeAsync(copyFileSync);\n"
"  var rename        = _makeAsync(renameSync);\n"
"  var chmod         = _makeAsync(chmodSync);\n"
"  var chown         = _makeAsync(chownSync);\n"
"  var realpath      = _makeAsync(realpathSync);\n"
"  var link          = _makeAsync(linkSync);\n"
"  var symlink       = _makeAsync(symlinkSync);\n"
"  var readlink      = _makeAsync(readlinkSync);\n"
"  var truncate      = _makeAsync(truncateSync);\n"
"  var utimes        = _makeAsync(utimesSync);\n"
"  var mkdtemp       = _makeAsync(mkdtempSync);\n"
"  var statfs        = _makeAsync(statfsSync);\n"
"  /* Symlink-aware variants */\n"
"  var lchmod        = _makeAsync(lchmodSync);\n"
"  var lchown        = _makeAsync(lchownSync);\n"
"  var lutimes       = _makeAsync(lutimesSync);\n"
"  /* Phase 2 callback variants */\n"
"  var open          = _makeAsync(openSync);\n"
"  var close         = _makeAsync(closeSync);\n"
"  /* read/write have non-trivial callback shapes (bytesRead, buffer) */\n"
"  function read(fd, buffer, offset, length, position, cb) {\n"
"    /* Node also supports read(fd, options, cb). Detect and adapt. */\n"
"    if (typeof offset === 'function')   { cb = offset;   offset = undefined; length = undefined; position = undefined; }\n"
"    else if (typeof length === 'function')   { cb = length;   length = undefined; position = undefined; }\n"
"    else if (typeof position === 'function') { cb = position; position = undefined; }\n"
"    if (typeof cb !== 'function') { var e = new TypeError('Callback must be a function'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e; }\n"
"    setTimeout(function() {\n"
"      try { var n = readSync(fd, buffer, offset, length, position); cb(null, n, buffer); }\n"
"      catch (e) { cb(e); }\n"
"    }, 0);\n"
"  }\n"
"  function write(fd, data, p2, p3, p4, p5) {\n"
"    /* Many overloads; trailing arg is the callback. */\n"
"    var args = Array.prototype.slice.call(arguments, 1);\n"
"    var cb = args.pop();\n"
"    if (typeof cb !== 'function') { var e = new TypeError('Callback must be a function'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e; }\n"
"    setTimeout(function() {\n"
"      try {\n"
"        var n = writeSync.apply(null, [fd].concat(args));\n"
"        cb(null, n, args[0]);\n"
"      } catch (e) { cb(e); }\n"
"    }, 0);\n"
"  }\n"
"  var fsync         = _makeAsync(fsyncSync);\n"
"  var fdatasync     = _makeAsync(fdatasyncSync);\n"
"  var ftruncate     = _makeAsync(ftruncateSync);\n"
"  var fstat         = _makeAsync(fstatSync);\n"
"  var fchmod        = _makeAsync(fchmodSync);\n"
"  var fchown        = _makeAsync(fchownSync);\n"
"  var futimes       = _makeAsync(futimesSync);\n"
"  /* Phase 3 callback variants */\n"
"  var cp            = _makeAsync(cpSync);\n"
"  var glob          = _makeAsync(globSync);\n"
"  var opendir       = _makeAsync(opendirSync);\n"
"  var writev        = _makeAsync(writevSync);\n"
"  var readv         = _makeAsync(readvSync);\n"
"\n"
"  /* exists is special — callback gets (boolean) not (err, result) */\n"
"  function exists(path, cb) {\n"
"    setTimeout(function() { cb(existsSync(path)); }, 0);\n"
"  }\n"
"\n"
"  /* fs.openAsBlob(path[, opts]): Promise<Blob>.  Eager read into a Blob\n"
"     backed by the file's bytes; node's spec backs it with lazy disk reads,\n"
"     but for typical use the difference is just memory usage. */\n"
"  function openAsBlob(path, opts) {\n"
"    var P = (typeof Promise !== 'undefined') ? Promise\n"
"          : (typeof global !== 'undefined' && global.Promise);\n"
"    if (!P) throw new Error('fs.openAsBlob requires Promise');\n"
"    var type = (opts && typeof opts === 'object' && typeof opts.type === 'string') ? opts.type : '';\n"
"    return new P(function(resolve, reject) {\n"
"      setTimeout(function() {\n"
"        try {\n"
"          if (typeof Blob === 'undefined') {\n"
"            var en = new Error('fs.openAsBlob requires global Blob (rampart-whatwg)');\n"
"            en.code = 'ERR_NOT_SUPPORTED'; throw en;\n"
"          }\n"
"          var buf = readFileSync(_path(path));\n"
"          if (!Buffer.isBuffer(buf)) buf = Buffer.from(buf);\n"
"          var u8 = new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);\n"
"          resolve(new Blob([u8], { type: type }));\n"
"        } catch (e) { reject(e); }\n"
"      }, 0);\n"
"    });\n"
"  }\n"
"\n"
"  /* ----- fs.promises mirror — lazily built on first access since\n"
"     Promise isn't available at module-init time (polyfill loaded\n"
"     later by the transpiler). ----- */\n"
"  function _makePromise(syncFn) {\n"
"    return function() {\n"
"      var args = arguments;\n"
"      /* Kick the transpiler polyfill if it hasn't installed Promise yet. */\n"
"      try { if (typeof global !== 'undefined' && global._TrN_Sp && global._TrN_Sp._pP) global._TrN_Sp._pP(); } catch (_) {}\n"
"      var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"      if (!P) throw new Error('fs.promises: Promise not available (run with -t or -b, or ensure a Promise polyfill is loaded)');\n"
"      return new P(function(resolve, reject) {\n"
"        setTimeout(function() {\n"
"          try { resolve(syncFn.apply(null, args)); }\n"
"          catch (e) { reject(e); }\n"
"        }, 0);\n"
"      });\n"
"    };\n"
"  }\n"
"  var _promisesCache = null;\n"
"  function _buildPromises() {\n"
"    if (_promisesCache) return _promisesCache;\n"
"    /* Best-effort: if the transpiler polyfill loader is around, give it\n"
"       a kick to make sure Promise is installed before we try to use it.\n"
"       (Polyfill install can be deferred until first explicit require\n"
"       and may miss native consumers like fs.promises.) */\n"
"    try {\n"
"      if (typeof global !== 'undefined' && global._TrN_Sp && global._TrN_Sp._pP)\n"
"        global._TrN_Sp._pP();\n"
"    } catch (e) {}\n"
"    _promisesCache = {\n"
"      readFile:   _signalPromise(readFileSync, 1),\n"
"      writeFile:  _signalPromise(writeFileSync, 2),\n"
"      appendFile: _signalPromise(appendFileSync, 2),\n"
"      stat:       _makePromise(statSync),\n"
"      lstat:      _makePromise(lstatSync),\n"
"      access:     _makePromise(accessSync),\n"
"      mkdir:      _makePromise(mkdirSync),\n"
"      rmdir:      _makePromise(rmdirSync),\n"
"      rm:         _makePromise(rmSync),\n"
"      unlink:     _makePromise(unlinkSync),\n"
"      readdir:    _makePromise(readdirSync),\n"
"      copyFile:   _makePromise(copyFileSync),\n"
"      rename:     _makePromise(renameSync),\n"
"      chmod:      _makePromise(chmodSync),\n"
"      chown:      _makePromise(chownSync),\n"
"      realpath:   _makePromise(realpathSync),\n"
"      link:       _makePromise(linkSync),\n"
"      symlink:    _makePromise(symlinkSync),\n"
"      readlink:   _makePromise(readlinkSync),\n"
"      truncate:   _makePromise(truncateSync),\n"
"      utimes:     _makePromise(utimesSync),\n"
"      mkdtemp:    _makePromise(mkdtempSync),\n"
"      statfs:     _makePromise(statfsSync),\n"
"      /* Phase 2 — fd-based ops via FileHandle */\n"
"      open: function(path, flags, mode) {\n"
"        var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"        if (!P) throw new Error('fs.promises.open requires Promise');\n"
"        return new P(function(resolve, reject) {\n"
"          setTimeout(function() {\n"
"            try { resolve(new FileHandle(openSync(path, flags, mode))); }\n"
"            catch (e) { reject(e); }\n"
"          }, 0);\n"
"        });\n"
"      },\n"
"      /* Phase 3 */\n"
"      cp:         _makePromise(cpSync),\n"
"      glob:       _makePromise(globSync),\n"
"      opendir:    _makePromise(opendirSync),\n"
"      writev:     _makePromise(writevSync),\n"
"      readv:      _makePromise(readvSync),\n"
"      /* Symlink-aware variants */\n"
"      lchmod:     _makePromise(lchmodSync),\n"
"      lchown:     _makePromise(lchownSync),\n"
"      lutimes:    _makePromise(lutimesSync),\n"
"      /* Standalone fd-keyed promise forms (node also exposes these in\n"
"         addition to the FileHandle methods). */\n"
"      close:      _makePromise(closeSync),\n"
"      read:       _makePromise(readSync),\n"
"      write:      _makePromise(writeSync),\n"
"      fsync:      _makePromise(fsyncSync),\n"
"      fdatasync:  _makePromise(fdatasyncSync),\n"
"      ftruncate:  _makePromise(ftruncateSync),\n"
"      fstat:      _makePromise(fstatSync),\n"
"      fchmod:     _makePromise(fchmodSync),\n"
"      fchown:     _makePromise(fchownSync),\n"
"      futimes:    _makePromise(futimesSync),\n"
"      /* Cosmetic alias for parity with node */\n"
"      constants:  constants\n"
"    };\n"
"    return _promisesCache;\n"
"  }\n"
"\n"
"  /* ----- Constants ----- */\n"
"  var constants = {\n"
"    F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1,\n"
"    O_RDONLY: 0, O_WRONLY: 1, O_RDWR: 2,\n"
"    O_CREAT: 64, O_EXCL: 128, O_NOCTTY: 256, O_TRUNC: 512,\n"
"    O_APPEND: 1024, O_DIRECTORY: 65536, O_NOATIME: 262144,\n"
"    O_NOFOLLOW: 131072, O_SYNC: 1052672, O_DSYNC: 4096, O_DIRECT: 16384,\n"
"    S_IFMT: 0xF000, S_IFREG: 0x8000, S_IFDIR: 0x4000, S_IFCHR: 0x2000,\n"
"    S_IFBLK: 0x6000, S_IFIFO: 0x1000, S_IFLNK: 0xA000, S_IFSOCK: 0xC000,\n"
"    S_IRWXU: 0x1C0, S_IRUSR: 0x100, S_IWUSR: 0x80, S_IXUSR: 0x40,\n"
"    S_IRWXG: 0x38,  S_IRGRP: 0x20,  S_IWGRP: 0x10, S_IXGRP: 0x8,\n"
"    S_IRWXO: 0x7,   S_IROTH: 0x4,   S_IWOTH: 0x2,  S_IXOTH: 0x1,\n"
"    COPYFILE_EXCL: 1, COPYFILE_FICLONE: 2, COPYFILE_FICLONE_FORCE: 4\n"
"  };\n"
"\n"
"  return {\n"
"    /* sync */\n"
"    readFileSync: readFileSync, writeFileSync: writeFileSync, appendFileSync: appendFileSync,\n"
"    statSync: statSync, lstatSync: lstatSync, existsSync: existsSync, accessSync: accessSync,\n"
"    mkdirSync: mkdirSync, rmdirSync: rmdirSync, rmSync: rmSync, unlinkSync: unlinkSync,\n"
"    readdirSync: readdirSync, copyFileSync: copyFileSync, renameSync: renameSync,\n"
"    chmodSync: chmodSync, chownSync: chownSync, realpathSync: realpathSync,\n"
"    linkSync: linkSync, symlinkSync: symlinkSync, readlinkSync: readlinkSync,\n"
"    truncateSync: truncateSync, utimesSync: utimesSync,\n"
"    mkdtempSync: mkdtempSync, statfsSync: statfsSync,\n"
"    lchmodSync: lchmodSync, lchownSync: lchownSync, lutimesSync: lutimesSync,\n"
"    /* Phase 2 — fd-based sync ops */\n"
"    openSync: openSync, closeSync: closeSync, readSync: readSync, writeSync: writeSync,\n"
"    fsyncSync: fsyncSync, fdatasyncSync: fdatasyncSync,\n"
"    ftruncateSync: ftruncateSync, futimesSync: futimesSync,\n"
"    fstatSync: fstatSync, fchmodSync: fchmodSync, fchownSync: fchownSync,\n"
"    /* Phase 3 sync */\n"
"    cpSync: cpSync, globSync: globSync, opendirSync: opendirSync,\n"
"    writevSync: writevSync, readvSync: readvSync,\n"
"    /* async (callback) */\n"
"    readFile: readFile, writeFile: writeFile, appendFile: appendFile,\n"
"    stat: stat, lstat: lstat, exists: exists, access: access,\n"
"    mkdir: mkdir, rmdir: rmdir, rm: rm, unlink: unlink,\n"
"    readdir: readdir, copyFile: copyFile, rename: rename,\n"
"    chmod: chmod, chown: chown, realpath: realpath,\n"
"    link: link, symlink: symlink, readlink: readlink,\n"
"    truncate: truncate, utimes: utimes,\n"
"    mkdtemp: mkdtemp, statfs: statfs,\n"
"    lchmod: lchmod, lchown: lchown, lutimes: lutimes,\n"
"    /* Phase 2 — fd-based async */\n"
"    open: open, close: close, read: read, write: write,\n"
"    fsync: fsync, fdatasync: fdatasync, ftruncate: ftruncate,\n"
"    futimes: futimes, fstat: fstat, fchmod: fchmod, fchown: fchown,\n"
"    /* Phase 3 async + streams */\n"
"    cp: cp, glob: glob, opendir: opendir,\n"
"    writev: writev, readv: readv,\n"
"    createReadStream: createReadStream, createWriteStream: createWriteStream,\n"
"    /* WHATWG Blob bridge */\n"
"    openAsBlob: openAsBlob,\n"
"    /* Phase 4 — file watching */\n"
"    watch: watch, watchFile: watchFile, unwatchFile: unwatchFile,\n"
"    /* promises mirror (lazy: built on first access) */\n"
"    get promises() { return _buildPromises(); },\n"
"    /* classes */\n"
"    Stats: Stats, Dirent: Dirent, FileHandle: FileHandle,\n"
"    Dir: Dir, ReadStream: ReadStream, WriteStream: WriteStream,\n"
"    FSWatcher: FSWatcher, StatWatcher: StatWatcher, StatFs: StatFs,\n"
"    /* constants */\n"
"    constants: constants,\n"
"    F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1\n"
"  };\n"
"}";

static void nodeshim_init_fs(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:fs_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, fs_js);
    duk_call(ctx, 0);
}
/* ============================================================
 * crypto — node's crypto module, wrapping rampart-crypto.
 *
 * rampart-crypto provides one-shot hash/hmac and direct cipher
 * primitives. Node's API is mostly streaming (createHash returns an
 * object you .update() multiple times then .digest()). We buffer
 * intermediate data in JS and call rampart's one-shot at digest time.
 *
 * Covered: createHash, createHmac, randomBytes (sync+async),
 * randomUUID, randomInt, pbkdf2/pbkdf2Sync, timingSafeEqual,
 * getHashes, getCiphers, createCipheriv/createDecipheriv (one-shot),
 * createSign/createVerify (RSA, one-shot).
 * Deferred: ECDH, generateKeyPair (have RSA-only via rampart),
 * KeyObject API, webcrypto.subtle, scrypt, Argon2.
 * ============================================================ */

static const char *crypto_js =
"function() {\n"
"  'use strict';\n"
"  var rc = (typeof rampart !== 'undefined' && rampart.lock) ? null : null;\n"
"  try { rc = require('rampart-crypto'); } catch (e) {\n"
"    /* rampart-crypto not loaded — most ops will throw. */\n"
"  }\n"
"  function _need() {\n"
"    if (!rc) throw new Error('rampart-crypto module is required for nodeshim.crypto');\n"
"  }\n"
"  /* Frozen options object passed to rampart-crypto's hash/hmac/rand to\n"
"     get a node-style Buffer back without an intermediate Uint8Array →\n"
"     Buffer.from() copy. */\n"
"  var _RETURN_BUFFER = { returnType: 'buffer' };\n"
"\n"
"  /* List of OpenSSL hash algorithms rampart-crypto recognizes. */\n"
"  var HASHES = [\n"
"    'md4','md5','mdc2','rmd160','sm3',\n"
"    'sha1','sha224','sha256','sha384','sha512',\n"
"    'sha512-224','sha512-256',\n"
"    'sha3-224','sha3-256','sha3-384','sha3-512',\n"
"    'shake128','shake256',\n"
"    'blake2s256','blake2b512'\n"
"  ];\n"
"  /* Node-style aliases (RSA-MD5, ripemd160, etc.) */\n"
"  var HASH_ALIASES = {\n"
"    'ripemd160': 'rmd160', 'RMD160': 'rmd160',\n"
"    'rsa-md5': 'md5', 'RSA-MD5': 'md5',\n"
"    'rsa-sha1': 'sha1', 'RSA-SHA1': 'sha1',\n"
"    'rsa-sha256': 'sha256', 'RSA-SHA256': 'sha256',\n"
"    'rsa-sha384': 'sha384', 'RSA-SHA384': 'sha384',\n"
"    'rsa-sha512': 'sha512', 'RSA-SHA512': 'sha512',\n"
"    'rsa-sha512_224': 'sha512-224', 'rsa-sha512_256': 'sha512-256',\n"
"    'RSA-SHA3-224': 'sha3-224', 'RSA-SHA3-256': 'sha3-256',\n"
"    'RSA-SHA3-384': 'sha3-384', 'RSA-SHA3-512': 'sha3-512',\n"
"    'SHA256': 'sha256', 'SHA384': 'sha384', 'SHA512': 'sha512'\n"
"  };\n"
"  function _normAlgo(a) {\n"
"    a = String(a).toLowerCase();\n"
"    if (HASH_ALIASES[a]) return HASH_ALIASES[a];\n"
"    /* Lookup case-sensitive form too */\n"
"    return a;\n"
"  }\n"
"\n"
"  /* --- Hash (streaming via buffer-and-flush) --- */\n"
"  function Hash(algo, opts) {\n"
"    if (!(this instanceof Hash)) return new Hash(algo, opts);\n"
"    this._algo = _normAlgo(algo);\n"
"    this._chunks = [];\n"
"    this._totalLen = 0;\n"
"    this._finalized = false;\n"
"  }\n"
"  Hash.prototype.update = function(data, inputEncoding) {\n"
"    if (this._finalized) {\n"
"      var e = new Error('Digest already called'); e.code = 'ERR_CRYPTO_HASH_FINALIZED'; throw e;\n"
"    }\n"
"    var buf;\n"
"    if (Buffer.isBuffer(data)) buf = data;\n"
"    else if (typeof data === 'string') buf = Buffer.from(data, inputEncoding || 'utf8');\n"
"    else if (data instanceof Uint8Array) buf = Buffer.from(data);\n"
"    else {\n"
"      var e = new TypeError('Data must be a string or Buffer.'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    this._chunks.push(buf);\n"
"    this._totalLen += buf.length;\n"
"    return this;\n"
"  };\n"
"  Hash.prototype.digest = function(encoding) {\n"
"    if (this._finalized) {\n"
"      var e = new Error('Digest already called'); e.code = 'ERR_CRYPTO_HASH_FINALIZED'; throw e;\n"
"    }\n"
"    this._finalized = true;\n"
"    _need();\n"
"    var combined = this._chunks.length === 1 ? this._chunks[0] : Buffer.concat(this._chunks, this._totalLen);\n"
"    /* rc.hash supports {returnType:'buffer'} for direct Buffer return,\n"
"       avoiding the copy that Buffer.from(uint8array) would do. */\n"
"    var digestBuf = rc.hash(combined, this._algo, _RETURN_BUFFER);\n"
"    return encoding ? digestBuf.toString(encoding) : digestBuf;\n"
"  };\n"
"  Hash.prototype.copy = function() {\n"
"    var c = new Hash(this._algo);\n"
"    c._chunks = this._chunks.slice();\n"
"    c._totalLen = this._totalLen;\n"
"    return c;\n"
"  };\n"
"\n"
"  function createHash(algo, opts) { return new Hash(algo, opts); }\n"
"\n"
"  /* --- Hmac (streaming via buffer-and-flush) --- */\n"
"  function Hmac(algo, key, opts) {\n"
"    if (!(this instanceof Hmac)) return new Hmac(algo, key, opts);\n"
"    this._algo = _normAlgo(algo);\n"
"    this._key = Buffer.isBuffer(key) ? key :\n"
"                (typeof key === 'string') ? Buffer.from(key, 'utf8') :\n"
"                Buffer.from(key);\n"
"    this._chunks = [];\n"
"    this._totalLen = 0;\n"
"    this._finalized = false;\n"
"  }\n"
"  Hmac.prototype.update = Hash.prototype.update;\n"
"  Hmac.prototype.digest = function(encoding) {\n"
"    if (this._finalized) {\n"
"      var e = new Error('Digest already called'); e.code = 'ERR_CRYPTO_HASH_FINALIZED'; throw e;\n"
"    }\n"
"    this._finalized = true;\n"
"    _need();\n"
"    var combined = this._chunks.length === 1 ? this._chunks[0] : Buffer.concat(this._chunks, this._totalLen);\n"
"    var out = rc.hmac(this._key, combined, this._algo, _RETURN_BUFFER);\n"
"    return encoding ? out.toString(encoding) : out;\n"
"  };\n"
"  function createHmac(algo, key, opts) { return new Hmac(algo, key, opts); }\n"
"\n"
"  /* --- randomBytes --- */\n"
"  function randomBytes(size, cb) {\n"
"    if (typeof size !== 'number' || size < 0 || size > 0x7FFFFFFF) {\n"
"      var e = new TypeError('size must be a non-negative integer'); e.code = 'ERR_OUT_OF_RANGE'; throw e;\n"
"    }\n"
"    _need();\n"
"    var buf;\n"
"    try { buf = rc.rand(size, _RETURN_BUFFER); }\n"
"    catch (err) {\n"
"      if (cb) { setTimeout(function(){ cb(err); }, 0); return; }\n"
"      throw err;\n"
"    }\n"
"    if (cb) { setTimeout(function(){ cb(null, buf); }, 0); return; }\n"
"    return buf;\n"
"  }\n"
"  function randomFillSync(buf, offset, size) {\n"
"    if (!Buffer.isBuffer(buf) && !(buf instanceof Uint8Array)) {\n"
"      var e = new TypeError('buf must be a Buffer or Uint8Array'); throw e;\n"
"    }\n"
"    offset = offset || 0;\n"
"    if (size === undefined) size = buf.length - offset;\n"
"    var src = randomBytes(size);\n"
"    for (var i = 0; i < size; i++) buf[offset + i] = src[i];\n"
"    return buf;\n"
"  }\n"
"  function randomFill(buf, offset, size, cb) {\n"
"    /* node arg shapes: (buf, cb), (buf, offset, cb), (buf, offset, size, cb) */\n"
"    if (typeof offset === 'function') { cb = offset; offset = 0; size = buf.length; }\n"
"    else if (typeof size === 'function') { cb = size; size = buf.length - offset; }\n"
"    try { randomFillSync(buf, offset, size); setTimeout(function(){ cb(null, buf); }, 0); }\n"
"    catch (e) { setTimeout(function(){ cb(e); }, 0); }\n"
"  }\n"
"\n"
"  /* --- randomUUID — RFC 4122 v4 --- */\n"
"  function randomUUID() {\n"
"    _need();\n"
"    var r = rc.rand(16, _RETURN_BUFFER);\n"
"    /* Set version (4) and variant (10) bits */\n"
"    r[6] = (r[6] & 0x0f) | 0x40;\n"
"    r[8] = (r[8] & 0x3f) | 0x80;\n"
"    var h = r.toString('hex');\n"
"    return h.substring(0,8) + '-' + h.substring(8,12) + '-' +\n"
"           h.substring(12,16) + '-' + h.substring(16,20) + '-' + h.substring(20,32);\n"
"  }\n"
"\n"
"  /* --- randomInt --- */\n"
"  function randomInt(min, max, cb) {\n"
"    if (typeof min === 'function') { cb = min; min = 0; max = 0x100000000; }\n"
"    else if (typeof max === 'function') { cb = max; max = min; min = 0; }\n"
"    else if (max === undefined) { max = min; min = 0; }\n"
"    if (!Number.isInteger(min) || !Number.isInteger(max)) {\n"
"      var e = new TypeError('min/max must be integers'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    if (max <= min) {\n"
"      var e = new RangeError('max must be > min'); e.code = 'ERR_OUT_OF_RANGE'; throw e;\n"
"    }\n"
"    var range = max - min;\n"
"    /* Use 4-byte random; rejection-sample to avoid modulo bias */\n"
"    var limit = Math.floor(0x100000000 / range) * range;\n"
"    var r;\n"
"    do {\n"
"      var b = randomBytes(4);\n"
"      r = (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] * 0x1000000)) >>> 0;\n"
"    } while (r >= limit);\n"
"    var result = min + (r % range);\n"
"    if (cb) { setTimeout(function(){ cb(null, result); }, 0); return; }\n"
"    return result;\n"
"  }\n"
"\n"
"  /* --- pbkdf2 — naive HMAC-iter implementation in JS (no rampart native) --- */\n"
"  function pbkdf2Sync(password, salt, iterations, keylen, digest) {\n"
"    _need();\n"
"    if (!Buffer.isBuffer(password)) password = Buffer.from(String(password), 'utf8');\n"
"    if (!Buffer.isBuffer(salt))     salt     = Buffer.from(String(salt), 'utf8');\n"
"    var hLen = _hLenOf(digest);\n"
"    var blocks = Math.ceil(keylen / hLen);\n"
"    var out = Buffer.alloc(blocks * hLen);\n"
"    for (var i = 1; i <= blocks; i++) {\n"
"      var blockIdx = Buffer.alloc(4);\n"
"      blockIdx.writeUInt32BE(i, 0);\n"
"      var U = rc.hmac(password, Buffer.concat([salt, blockIdx]), _normAlgo(digest), _RETURN_BUFFER);\n"
"      var T = Buffer.from(U);\n"
"      for (var j = 1; j < iterations; j++) {\n"
"        U = rc.hmac(password, U, _normAlgo(digest), _RETURN_BUFFER);\n"
"        for (var k = 0; k < hLen; k++) T[k] ^= U[k];\n"
"      }\n"
"      T.copy(out, (i - 1) * hLen);\n"
"    }\n"
"    return out.subarray(0, keylen);\n"
"  }\n"
"  function pbkdf2(password, salt, iterations, keylen, digest, cb) {\n"
"    setTimeout(function() {\n"
"      try { cb(null, pbkdf2Sync(password, salt, iterations, keylen, digest)); }\n"
"      catch (e) { cb(e); }\n"
"    }, 0);\n"
"  }\n"
"  /* Hash output lengths (bytes) */\n"
"  var HLEN = {\n"
"    md5: 16, sha1: 20, sha224: 28, sha256: 32, sha384: 48, sha512: 64,\n"
"    'sha512-224': 28, 'sha512-256': 32,\n"
"    'sha3-224': 28, 'sha3-256': 32, 'sha3-384': 48, 'sha3-512': 64,\n"
"    rmd160: 20, ripemd160: 20,\n"
"    blake2s256: 32, blake2b512: 64\n"
"  };\n"
"  function _hLenOf(d) {\n"
"    var n = _normAlgo(d);\n"
"    if (HLEN[n]) return HLEN[n];\n"
"    /* fallback: do a trial hash to determine length */\n"
"    var t = rc.hash(Buffer.from('test'), n);\n"
"    return (Buffer.isBuffer(t) ? t : Buffer.from(t)).length;\n"
"  }\n"
"\n"
"  /* --- timingSafeEqual --- */\n"
"  function timingSafeEqual(a, b) {\n"
"    if (!Buffer.isBuffer(a) && !(a instanceof Uint8Array)) {\n"
"      var e = new TypeError('a must be a Buffer or Uint8Array'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    if (!Buffer.isBuffer(b) && !(b instanceof Uint8Array)) {\n"
"      var e = new TypeError('b must be a Buffer or Uint8Array'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    if (a.length !== b.length) {\n"
"      var e = new RangeError('Input buffers must have the same byte length'); e.code = 'ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH'; throw e;\n"
"    }\n"
"    var diff = 0;\n"
"    for (var i = 0; i < a.length; i++) diff |= (a[i] ^ b[i]);\n"
"    return diff === 0;\n"
"  }\n"
"\n"
"  /* --- getHashes / getCiphers --- */\n"
"  function getHashes() { return HASHES.slice(); }\n"
"  function getCiphers() {\n"
"    /* rampart's encrypt/decrypt take an algorithm name. Common ones: */\n"
"    return [\n"
"      'aes-128-cbc', 'aes-192-cbc', 'aes-256-cbc',\n"
"      'aes-128-ctr', 'aes-192-ctr', 'aes-256-ctr',\n"
"      'aes-128-gcm', 'aes-192-gcm', 'aes-256-gcm',\n"
"      'aes-128-ecb', 'aes-192-ecb', 'aes-256-ecb',\n"
"      'des-cbc', 'des-ecb', 'des-ede3-cbc',\n"
"      'rc4', 'chacha20', 'chacha20-poly1305'\n"
"    ];\n"
"  }\n"
"\n"
"  /* --- Cipher / Decipher (one-shot via rc.encrypt/decrypt) --- */\n"
"  function Cipher(algo, key, iv, opts, isDecipher) {\n"
"    this._algo = String(algo).toLowerCase();\n"
"    this._key = Buffer.isBuffer(key) ? key : Buffer.from(key);\n"
"    this._iv = iv ? (Buffer.isBuffer(iv) ? iv : Buffer.from(iv)) : Buffer.alloc(0);\n"
"    this._chunks = [];\n"
"    this._totalLen = 0;\n"
"    this._finalized = false;\n"
"    this._isDecipher = !!isDecipher;\n"
"    this._authTag = null;\n"
"  }\n"
"  Cipher.prototype.update = function(data, inputEncoding, outputEncoding) {\n"
"    if (this._finalized) { var e = new Error('Cipher.final already called'); throw e; }\n"
"    var buf;\n"
"    if (Buffer.isBuffer(data)) buf = data;\n"
"    else if (typeof data === 'string') buf = Buffer.from(data, inputEncoding || 'utf8');\n"
"    else buf = Buffer.from(data);\n"
"    this._chunks.push(buf);\n"
"    this._totalLen += buf.length;\n"
"    /* node returns a Buffer per .update; we buffer until final and return empty here.\n"
"       Most callers do all-update-then-final, which matches our buffering. */\n"
"    return outputEncoding ? '' : Buffer.alloc(0);\n"
"  };\n"
"  Cipher.prototype['final'] = function(outputEncoding) {\n"
"    if (this._finalized) { var e = new Error('Cipher.final already called'); throw e; }\n"
"    this._finalized = true;\n"
"    _need();\n"
"    var combined = Buffer.concat(this._chunks, this._totalLen);\n"
"    var out;\n"
"    var opts = { cipher: this._algo, data: combined, key: this._key, iv: this._iv };\n"
"    if (this._isDecipher) out = rc.decrypt(opts);\n"
"    else                  out = rc.encrypt(opts);\n"
"    if (!Buffer.isBuffer(out)) out = Buffer.from(out);\n"
"    return outputEncoding ? out.toString(outputEncoding) : out;\n"
"  };\n"
"  Cipher.prototype.setAutoPadding = function(_) { return this; };\n"
"  Cipher.prototype.getAuthTag = function() { return this._authTag; };\n"
"  Cipher.prototype.setAuthTag = function(tag) { this._authTag = tag; return this; };\n"
"  Cipher.prototype.setAAD = function() { return this; };\n"
"  function createCipheriv(algo, key, iv) { return new Cipher(algo, key, iv, null, false); }\n"
"  function createDecipheriv(algo, key, iv) { return new Cipher(algo, key, iv, null, true); }\n"
"\n"
"  /* --- Sign / Verify (one-shot via RSA primitives) --- */\n"
"  function Sign(algo) {\n"
"    this._algo = _normAlgo(algo);\n"
"    this._chunks = []; this._totalLen = 0; this._finalized = false;\n"
"  }\n"
"  Sign.prototype.update = Hash.prototype.update;\n"
"  Sign.prototype.sign = function(privateKey, outputEncoding) {\n"
"    if (this._finalized) { var e = new Error('sign() already called'); throw e; }\n"
"    this._finalized = true;\n"
"    _need();\n"
"    var combined = Buffer.concat(this._chunks, this._totalLen);\n"
"    var keyBuf = typeof privateKey === 'string' ? privateKey :\n"
"                 (privateKey && privateKey.key) ? privateKey.key :\n"
"                 privateKey;\n"
"    var out = rc.rsa_sign(combined, keyBuf, this._algo);\n"
"    if (!Buffer.isBuffer(out)) out = Buffer.from(out);\n"
"    return outputEncoding ? out.toString(outputEncoding) : out;\n"
"  };\n"
"  function createSign(algo) { return new Sign(algo); }\n"
"\n"
"  function Verify(algo) {\n"
"    this._algo = _normAlgo(algo);\n"
"    this._chunks = []; this._totalLen = 0; this._finalized = false;\n"
"  }\n"
"  Verify.prototype.update = Hash.prototype.update;\n"
"  Verify.prototype.verify = function(publicKey, signature, signatureEncoding) {\n"
"    if (this._finalized) { var e = new Error('verify() already called'); throw e; }\n"
"    this._finalized = true;\n"
"    _need();\n"
"    var combined = Buffer.concat(this._chunks, this._totalLen);\n"
"    var keyBuf = typeof publicKey === 'string' ? publicKey :\n"
"                 (publicKey && publicKey.key) ? publicKey.key :\n"
"                 publicKey;\n"
"    var sigBuf = Buffer.isBuffer(signature) ? signature :\n"
"                 (signatureEncoding) ? Buffer.from(signature, signatureEncoding) :\n"
"                 Buffer.from(signature);\n"
"    return !!rc.rsa_verify(combined, sigBuf, keyBuf, this._algo);\n"
"  };\n"
"  function createVerify(algo) { return new Verify(algo); }\n"
"\n"
"  return {\n"
"    createHash: createHash,\n"
"    createHmac: createHmac,\n"
"    createCipheriv: createCipheriv,\n"
"    createDecipheriv: createDecipheriv,\n"
"    createSign: createSign,\n"
"    createVerify: createVerify,\n"
"    randomBytes: randomBytes,\n"
"    randomFill: randomFill,\n"
"    randomFillSync: randomFillSync,\n"
"    randomUUID: randomUUID,\n"
"    randomInt: randomInt,\n"
"    pbkdf2: pbkdf2,\n"
"    pbkdf2Sync: pbkdf2Sync,\n"
"    timingSafeEqual: timingSafeEqual,\n"
"    getHashes: getHashes,\n"
"    getCiphers: getCiphers,\n"
"    /* Constants — minimal subset */\n"
"    constants: {\n"
"      RSA_PKCS1_PADDING: 1,\n"
"      RSA_NO_PADDING: 3,\n"
"      RSA_PKCS1_OAEP_PADDING: 4,\n"
"      RSA_X931_PADDING: 5,\n"
"      RSA_PKCS1_PSS_PADDING: 6\n"
"    },\n"
"    /* Classes exposed for instanceof checks */\n"
"    Hash: Hash,\n"
"    Hmac: Hmac,\n"
"    Cipher: Cipher,\n"
"    Decipher: Cipher,\n"
"    Sign: Sign,\n"
"    Verify: Verify,\n"
"    /* One-shot hash (node 21+) */\n"
"    hash: function(algorithm, data, encoding) {\n"
"      var h = new Hash(algorithm);\n"
"      h.update(data);\n"
"      return h.digest(encoding || 'hex');\n"
"    },\n"
"    /* Web Crypto getRandomValues -- fills typed array in place. */\n"
"    getRandomValues: function(buf) {\n"
"      if (!buf || typeof buf.length !== 'number')\n"
"        throw new TypeError('getRandomValues: argument must be a TypedArray');\n"
"      var bytes = randomBytes(buf.length);\n"
"      for (var i = 0; i < buf.length; i++) buf[i] = bytes[i];\n"
"      return buf;\n"
"    },\n"
"    /* Curve / FIPS introspection.  rampart-crypto doesn't expose\n"
"       OpenSSL provider-level probes, so we return a static list of\n"
"       commonly-supported curves and report FIPS off. */\n"
"    getCurves: function() {\n"
"      return ['prime192v1','prime256v1','secp256k1','secp384r1','secp521r1',\n"
"              'secp224r1','sect163k1','sect163r2','sect233k1','sect233r1',\n"
"              'sect283k1','sect283r1','sect409k1','sect409r1','sect571k1','sect571r1'];\n"
"    },\n"
"    getFips: function() { return 0; },\n"
"    setFips: function(v) {\n"
"      if (v) throw new Error('FIPS mode is not supported in this build');\n"
"    },\n"
"    /* getCipherInfo(name) -> {name, blockSize, ivLength, keyLength, mode}\n"
"       or undefined for unknown.  Small lookup table of the cipher\n"
"       names rampart-crypto exposes; matches node's shape. */\n"
"    getCipherInfo: function(name) {\n"
"      var table = {\n"
"        'aes-128-cbc': {blockSize:16, ivLength:16, keyLength:16, mode:'cbc'},\n"
"        'aes-192-cbc': {blockSize:16, ivLength:16, keyLength:24, mode:'cbc'},\n"
"        'aes-256-cbc': {blockSize:16, ivLength:16, keyLength:32, mode:'cbc'},\n"
"        'aes-128-ctr': {blockSize:16, ivLength:16, keyLength:16, mode:'ctr'},\n"
"        'aes-192-ctr': {blockSize:16, ivLength:16, keyLength:24, mode:'ctr'},\n"
"        'aes-256-ctr': {blockSize:16, ivLength:16, keyLength:32, mode:'ctr'},\n"
"        'aes-128-gcm': {blockSize:16, ivLength:12, keyLength:16, mode:'gcm'},\n"
"        'aes-192-gcm': {blockSize:16, ivLength:12, keyLength:24, mode:'gcm'},\n"
"        'aes-256-gcm': {blockSize:16, ivLength:12, keyLength:32, mode:'gcm'},\n"
"        'aes-128-ecb': {blockSize:16, ivLength:0,  keyLength:16, mode:'ecb'},\n"
"        'aes-192-ecb': {blockSize:16, ivLength:0,  keyLength:24, mode:'ecb'},\n"
"        'aes-256-ecb': {blockSize:16, ivLength:0,  keyLength:32, mode:'ecb'}\n"
"      };\n"
"      var entry = table[String(name).toLowerCase()];\n"
"      if (!entry) return undefined;\n"
"      return {name: String(name).toLowerCase(),\n"
"              blockSize: entry.blockSize, ivLength: entry.ivLength,\n"
"              keyLength: entry.keyLength, mode: entry.mode};\n"
"    }\n"
"  };\n"
"}";


/* JS snippet that attaches `webcrypto` as a getter on the classic
 * crypto module, reading from globalThis.crypto.  Web Crypto API now
 * lives in rampart-whatwg.so; we just expose it through node's
 * require('crypto').webcrypto namespace.  Getter (not eager read)
 * avoids triggering the rampart-whatwg load during nodeshim init. */
static const char *crypto_webcrypto_attach_js =
"(function (mod) {\n"
"  Object.defineProperty(mod, 'webcrypto', {\n"
"    get: function() { return globalThis.crypto; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"  return mod;\n"
"})";

static void nodeshim_init_crypto(duk_context *ctx)
{
    /* Build the classic crypto module. */
    duk_push_string(ctx, "rampart-nodeshim.c:crypto_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, crypto_js);
    duk_call(ctx, 0);

    /* Attach the `webcrypto` getter that resolves to globalThis.crypto
       (installed lazily by rampart-whatwg).  Pattern matches the URL
       module's globalThis lift. */
    duk_eval_string(ctx, crypto_webcrypto_attach_js);
    duk_dup(ctx, -2);  /* push the classic crypto module as arg */
    duk_call(ctx, 1);
    duk_pop(ctx);      /* attach fn returned mod — same object, discard */
}
/* ============================================================
 * os — node's os module. All native C, mostly POSIX wrappers.
 * Covers: arch, availableParallelism, constants, cpus, endianness,
 * EOL, freemem, getPriority, homedir, hostname, loadavg, machine,
 * networkInterfaces, platform, release, setPriority, tmpdir,
 * totalmem, type, uptime, userInfo, version.
 * ============================================================ */

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(__linux__)
#  include <sys/sysinfo.h>
#endif

static duk_ret_t os_arch(duk_context *ctx)        { duk_push_string(ctx, NS_ARCH); return 1; }
static duk_ret_t os_platform(duk_context *ctx)    { duk_push_string(ctx, NS_PLATFORM); return 1; }

static duk_ret_t os_endianness(duk_context *ctx)
{
    union { uint32_t i; char c[4]; } u = { 0x01020304 };
    duk_push_string(ctx, (u.c[0] == 1) ? "BE" : "LE");
    return 1;
}

static duk_ret_t os_hostname(duk_context *ctx)
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0)
        RP_THROW(ctx, "os.hostname: %s", strerror(errno));
    buf[sizeof(buf) - 1] = '\0';
    duk_push_string(ctx, buf);
    return 1;
}

static duk_ret_t os_homedir(duk_context *ctx)
{
    const char *h = getenv("HOME");
    if (h && *h) { duk_push_string(ctx, h); return 1; }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) { duk_push_string(ctx, pw->pw_dir); return 1; }
    duk_push_string(ctx, "/");
    return 1;
}

/* Pull an env var via globalThis.process.env, so that tests that mutate
   process.env see the change. Falls back to libc getenv() if process.env
   isn't available. Returns NULL if unset/empty. Caller must duk_pop the
   result string ONLY if return is non-NULL (we leave it on the stack so
   the C-string pointer stays alive). */
static const char *ns_env_lookup(duk_context *ctx, const char *name, int *did_push)
{
    *did_push = 0;
    duk_get_global_string(ctx, "process");
    if (duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "env");
        duk_remove(ctx, -2); /* drop process */
        if (duk_is_object(ctx, -1)) {
            duk_get_prop_string(ctx, -1, name);
            duk_remove(ctx, -2); /* drop env */
            if (duk_is_string(ctx, -1)) {
                const char *s = duk_get_string(ctx, -1);
                if (s && *s) { *did_push = 1; return s; }
            }
            duk_pop(ctx); /* drop non-string or empty */
        } else {
            duk_pop(ctx);
        }
    } else {
        duk_pop(ctx);
    }
    /* Fallback to libc */
    const char *s = getenv(name);
    return (s && *s) ? s : NULL;
}

static duk_ret_t os_tmpdir(duk_context *ctx)
{
    int pushed = 0, pushed_any = 0;
    const char *t = ns_env_lookup(ctx, "TMPDIR", &pushed);
    if (pushed) pushed_any = 1;
    if (!t) { if (pushed) duk_pop(ctx); pushed = 0;
              t = ns_env_lookup(ctx, "TMP", &pushed); if (pushed) pushed_any = 1; }
    if (!t) { if (pushed) duk_pop(ctx); pushed = 0;
              t = ns_env_lookup(ctx, "TEMP", &pushed); if (pushed) pushed_any = 1; }
    if (!t) t = "/tmp";
    size_t len = strlen(t);
    while (len > 1 && t[len - 1] == '/') len--;
    duk_push_lstring(ctx, t, len);
    if (pushed_any) duk_remove(ctx, -2);  /* drop the env string under our result */
    return 1;
}

static duk_ret_t os_loadavg(duk_context *ctx)
{
    double load[3] = {0, 0, 0};
    int n = getloadavg(load, 3);
    duk_push_array(ctx);
    for (int i = 0; i < 3; i++) {
        duk_push_number(ctx, n >= i + 1 ? load[i] : 0);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

/* All four of these delegate to the cross-platform helpers exposed via
   the rampart `process` object (rampart-utils.c): process.uptime(),
   process.getTotalMem(), process.getFreeMem(), process.getCpuInfo().
   Those have macOS/FreeBSD/Linux implementations baked in, so we don't
   need to duplicate the platform-specific syscall code here.

   Convention mismatch: node's os.totalmem / os.freemem return BYTES;
   rampart's process.getTotalMem / .getFreeMem return MB.  Multiply by
   1048576 to convert. */
static int _call_process_method(duk_context *ctx, const char *name)
{
    duk_push_global_object(ctx);
    if (!duk_get_prop_string(ctx, -1, "process")) { duk_pop_2(ctx); return 0; }
    if (!duk_get_prop_string(ctx, -1, name))      { duk_pop_3(ctx); return 0; }
    duk_call(ctx, 0);                /* result replaces fn on top */
    duk_remove(ctx, -2);             /* drop process */
    duk_remove(ctx, -2);             /* drop global */
    return 1;
}

static duk_ret_t os_uptime(duk_context *ctx)
{
    /* node's os.uptime() returns SECONDS SINCE OS BOOT — distinct
       from process.uptime() (process lifetime).  rampart-core
       exposes the boot-uptime value as process.systemUptime(). */
    if (_call_process_method(ctx, "systemUptime")) return 1;
    duk_push_number(ctx, 0.0);
    return 1;
}

static duk_ret_t os_totalmem(duk_context *ctx)
{
    if (_call_process_method(ctx, "getTotalMem")) {
        double mb = duk_get_number(ctx, -1);
        duk_pop(ctx);
        duk_push_number(ctx, mb * 1048576.0);
        return 1;
    }
    duk_push_number(ctx, 0.0);
    return 1;
}

static duk_ret_t os_freemem(duk_context *ctx)
{
    if (_call_process_method(ctx, "getFreeMem")) {
        double mb = duk_get_number(ctx, -1);
        duk_pop(ctx);
        duk_push_number(ctx, mb * 1048576.0);
        return 1;
    }
#if 0
    /* Dead-code legacy fallback for reference. */
    long pages = sysconf(_SC_PAGE_SIZE);
    if (pages > 0) { /* ... */ }
#endif
    duk_push_number(ctx, 0.0);
    return 1;
}

static duk_ret_t os_available_parallelism(duk_context *ctx)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    duk_push_int(ctx, (int)n);
    return 1;
}

static duk_ret_t os_type(duk_context *ctx)
{
    struct utsname u;
    if (uname(&u) != 0)
        RP_THROW(ctx, "os.type: %s", strerror(errno));
    duk_push_string(ctx, u.sysname);
    return 1;
}

static duk_ret_t os_release(duk_context *ctx)
{
    struct utsname u;
    if (uname(&u) != 0)
        RP_THROW(ctx, "os.release: %s", strerror(errno));
    duk_push_string(ctx, u.release);
    return 1;
}

static duk_ret_t os_version(duk_context *ctx)
{
    struct utsname u;
    if (uname(&u) != 0)
        RP_THROW(ctx, "os.version: %s", strerror(errno));
    duk_push_string(ctx, u.version);
    return 1;
}

static duk_ret_t os_machine(duk_context *ctx)
{
    struct utsname u;
    if (uname(&u) != 0)
        RP_THROW(ctx, "os.machine: %s", strerror(errno));
    duk_push_string(ctx, u.machine);
    return 1;
}

static duk_ret_t os_user_info(duk_context *ctx)
{
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    duk_push_object(ctx);
    duk_push_int(ctx, (int)uid);
    duk_put_prop_string(ctx, -2, "uid");
    duk_push_int(ctx, (int)getgid());
    duk_put_prop_string(ctx, -2, "gid");
    duk_push_string(ctx, pw && pw->pw_name ? pw->pw_name : "unknown");
    duk_put_prop_string(ctx, -2, "username");
    duk_push_string(ctx, pw && pw->pw_dir ? pw->pw_dir
                         : (getenv("HOME") ? getenv("HOME") : "/"));
    duk_put_prop_string(ctx, -2, "homedir");
    duk_push_string(ctx, pw && pw->pw_shell ? pw->pw_shell : "/bin/sh");
    duk_put_prop_string(ctx, -2, "shell");
    return 1;
}

/* getPriority([pid]) — wraps getpriority(PRIO_PROCESS, pid) */
static duk_ret_t os_get_priority(duk_context *ctx)
{
    int pid = 0;
    if (!duk_is_undefined(ctx, 0)) pid = duk_require_int(ctx, 0);
    errno = 0;
    int prio = getpriority(PRIO_PROCESS, pid);
    if (prio == -1 && errno != 0)
        RP_THROW(ctx, "os.getPriority: %s", strerror(errno));
    duk_push_int(ctx, prio);
    return 1;
}

static duk_ret_t os_set_priority(duk_context *ctx)
{
    int pid, prio;
    /* setPriority(prio) — one effective arg means pid=0 */
    if (duk_is_undefined(ctx, 1)) {
        pid = 0;
        prio = duk_require_int(ctx, 0);
    } else {
        pid = duk_require_int(ctx, 0);
        prio = duk_require_int(ctx, 1);
    }
    if (setpriority(PRIO_PROCESS, pid, prio) != 0)
        RP_THROW(ctx, "os.setPriority: %s", strerror(errno));
    return 0;
}

/* cpus() — delegates to rampart's process.getCpuInfo(), which has the
   per-platform implementations in rampart-utils.c (Linux /proc/stat +
   /proc/cpuinfo; macOS host_processor_info + sysctlbyname; FreeBSD
   kern.cp_times + hw.model + dev.cpu.0.freq).  Returns the array as-is
   — node's os.cpus() shape matches what process.getCpuInfo() produces. */
static duk_ret_t os_cpus(duk_context *ctx)
{
    if (_call_process_method(ctx, "getCpuInfo")) return 1;
    /* Last-resort fallback: stubs sized by sysconf. */
    duk_push_array(ctx);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    for (long i = 0; i < n; i++) {
        duk_push_object(ctx);
        duk_push_string(ctx, "unknown"); duk_put_prop_string(ctx, -2, "model");
        duk_push_int(ctx, 0);            duk_put_prop_string(ctx, -2, "speed");
        duk_push_object(ctx);
        duk_push_number(ctx, 0); duk_put_prop_string(ctx, -2, "user");
        duk_push_number(ctx, 0); duk_put_prop_string(ctx, -2, "nice");
        duk_push_number(ctx, 0); duk_put_prop_string(ctx, -2, "sys");
        duk_push_number(ctx, 0); duk_put_prop_string(ctx, -2, "idle");
        duk_push_number(ctx, 0); duk_put_prop_string(ctx, -2, "irq");
        duk_put_prop_string(ctx, -2, "times");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}

/* networkInterfaces() — iterates getifaddrs.
   Returns { ifname: [ {address, netmask, family, mac, internal, cidr, scopeid?} ], ... } */
static duk_ret_t os_network_interfaces(duk_context *ctx)
{
    duk_push_object(ctx);

    struct ifaddrs *ifa, *p;
    if (getifaddrs(&ifa) != 0) return 1;  /* empty object on failure */

    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr) continue;
        int family = p->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        char host[NI_MAXHOST] = {0};
        char netmask[NI_MAXHOST] = {0};
        socklen_t sl = (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        if (getnameinfo(p->ifa_addr, sl, host, sizeof(host), NULL, 0, NI_NUMERICHOST) != 0)
            continue;
        if (p->ifa_netmask) {
            getnameinfo(p->ifa_netmask, sl, netmask, sizeof(netmask), NULL, 0, NI_NUMERICHOST);
        }
        int internal = (p->ifa_flags & IFF_LOOPBACK) ? 1 : 0;

        /* Compute CIDR prefix length */
        int prefix = 0;
        if (p->ifa_netmask) {
            if (family == AF_INET) {
                uint32_t m = ntohl(((struct sockaddr_in*)p->ifa_netmask)->sin_addr.s_addr);
                while (m & 0x80000000U) { prefix++; m <<= 1; }
            } else {
                uint8_t *b = ((struct sockaddr_in6*)p->ifa_netmask)->sin6_addr.s6_addr;
                for (int i = 0; i < 16; i++) {
                    if (b[i] == 0xFF) { prefix += 8; continue; }
                    uint8_t v = b[i];
                    while (v & 0x80) { prefix++; v <<= 1; }
                    break;
                }
            }
        }

        /* Get or create array for this interface name */
        if (!duk_get_prop_string(ctx, -1, p->ifa_name)) {
            duk_pop(ctx);
            duk_push_array(ctx);
            duk_dup(ctx, -1);
            duk_put_prop_string(ctx, -3, p->ifa_name);
        }
        duk_uarridx_t len = (duk_uarridx_t)duk_get_length(ctx, -1);
        duk_push_object(ctx);
        duk_push_string(ctx, host);    duk_put_prop_string(ctx, -2, "address");
        duk_push_string(ctx, netmask); duk_put_prop_string(ctx, -2, "netmask");
        duk_push_string(ctx, family == AF_INET ? "IPv4" : "IPv6");
        duk_put_prop_string(ctx, -2, "family");
        duk_push_string(ctx, "00:00:00:00:00:00");  /* TODO: real mac via SIOCGIFHWADDR */
        duk_put_prop_string(ctx, -2, "mac");
        duk_push_boolean(ctx, internal);
        duk_put_prop_string(ctx, -2, "internal");
        /* CIDR: address/prefix */
        char cidr[NI_MAXHOST + 8];
        snprintf(cidr, sizeof(cidr), "%s/%d", host, prefix);
        duk_push_string(ctx, cidr); duk_put_prop_string(ctx, -2, "cidr");
        if (family == AF_INET6) {
            duk_push_int(ctx, (int)((struct sockaddr_in6*)p->ifa_addr)->sin6_scope_id);
            duk_put_prop_string(ctx, -2, "scopeid");
        }
        duk_put_prop_index(ctx, -2, len);
        duk_pop(ctx);  /* the array */
    }
    freeifaddrs(ifa);
    return 1;
}

static void nodeshim_init_os(duk_context *ctx)
{
    duk_push_object(ctx);

#define OS_FN(name, fn, nargs) do { \
    duk_push_c_function(ctx, fn, nargs); \
    duk_put_prop_string(ctx, -2, name); \
} while (0)

    OS_FN("arch",                  os_arch,                   0);
    OS_FN("availableParallelism",  os_available_parallelism,  0);
    OS_FN("cpus",                  os_cpus,                   0);
    OS_FN("endianness",            os_endianness,             0);
    OS_FN("freemem",               os_freemem,                0);
    OS_FN("getPriority",           os_get_priority,           1);
    OS_FN("homedir",               os_homedir,                0);
    OS_FN("hostname",              os_hostname,               0);
    OS_FN("loadavg",               os_loadavg,                0);
    OS_FN("machine",               os_machine,                0);
    OS_FN("networkInterfaces",     os_network_interfaces,     0);
    OS_FN("platform",              os_platform,               0);
    OS_FN("release",               os_release,                0);
    OS_FN("setPriority",           os_set_priority,           2);
    OS_FN("tmpdir",                os_tmpdir,                 0);
    OS_FN("totalmem",              os_totalmem,               0);
    OS_FN("type",                  os_type,                   0);
    OS_FN("uptime",                os_uptime,                 0);
    OS_FN("userInfo",              os_user_info,              1);
    OS_FN("version",               os_version,                0);

#undef OS_FN

    /* EOL: \n on posix, \r\n on win32. We're posix-only. */
    duk_push_string(ctx, "\n");
    duk_put_prop_string(ctx, -2, "EOL");

    /* constants: minimal subset — errno/signal/dlopen/priority namespaces */
    duk_push_object(ctx);
        duk_push_object(ctx);  /* errno */
        #define ERRDEF(n) do { duk_push_int(ctx, n); duk_put_prop_string(ctx, -2, #n); } while (0)
        ERRDEF(E2BIG); ERRDEF(EACCES); ERRDEF(EADDRINUSE); ERRDEF(EADDRNOTAVAIL);
        ERRDEF(EAGAIN); ERRDEF(EBADF); ERRDEF(EBUSY); ERRDEF(ECHILD); ERRDEF(EEXIST);
        ERRDEF(EFAULT); ERRDEF(EFBIG); ERRDEF(EINTR); ERRDEF(EINVAL); ERRDEF(EIO);
        ERRDEF(EISDIR); ERRDEF(EMFILE); ERRDEF(EMLINK); ERRDEF(ENAMETOOLONG);
        ERRDEF(ENFILE); ERRDEF(ENODEV); ERRDEF(ENOENT); ERRDEF(ENOEXEC); ERRDEF(ENOMEM);
        ERRDEF(ENOSPC); ERRDEF(ENOSYS); ERRDEF(ENOTDIR); ERRDEF(ENOTEMPTY); ERRDEF(ENOTTY);
        ERRDEF(ENXIO); ERRDEF(EPERM); ERRDEF(EPIPE); ERRDEF(ERANGE); ERRDEF(EROFS);
        ERRDEF(ESPIPE); ERRDEF(ESRCH); ERRDEF(EXDEV);
        ERRDEF(ECONNREFUSED); ERRDEF(ECONNRESET); ERRDEF(ETIMEDOUT); ERRDEF(EHOSTUNREACH);
        ERRDEF(ENETDOWN); ERRDEF(ENETUNREACH);
        #undef ERRDEF
        duk_put_prop_string(ctx, -2, "errno");

        duk_push_object(ctx);  /* signals */
        #define SIGDEF(n) do { duk_push_int(ctx, n); duk_put_prop_string(ctx, -2, #n); } while (0)
        SIGDEF(SIGHUP); SIGDEF(SIGINT); SIGDEF(SIGQUIT); SIGDEF(SIGILL); SIGDEF(SIGABRT);
        SIGDEF(SIGFPE); SIGDEF(SIGKILL); SIGDEF(SIGSEGV); SIGDEF(SIGPIPE); SIGDEF(SIGALRM);
        SIGDEF(SIGTERM); SIGDEF(SIGCHLD); SIGDEF(SIGCONT); SIGDEF(SIGSTOP); SIGDEF(SIGTSTP);
        SIGDEF(SIGTTIN); SIGDEF(SIGTTOU); SIGDEF(SIGURG); SIGDEF(SIGXCPU); SIGDEF(SIGXFSZ);
        SIGDEF(SIGUSR1); SIGDEF(SIGUSR2);
        #undef SIGDEF
        duk_put_prop_string(ctx, -2, "signals");

        duk_push_object(ctx);  /* priority */
        duk_push_int(ctx, -20); duk_put_prop_string(ctx, -2, "PRIORITY_HIGHEST");
        duk_push_int(ctx, -14); duk_put_prop_string(ctx, -2, "PRIORITY_HIGH");
        duk_push_int(ctx, -7);  duk_put_prop_string(ctx, -2, "PRIORITY_ABOVE_NORMAL");
        duk_push_int(ctx, 0);   duk_put_prop_string(ctx, -2, "PRIORITY_NORMAL");
        duk_push_int(ctx, 10);  duk_put_prop_string(ctx, -2, "PRIORITY_BELOW_NORMAL");
        duk_push_int(ctx, 19);  duk_put_prop_string(ctx, -2, "PRIORITY_LOW");
        duk_put_prop_string(ctx, -2, "priority");

    duk_put_prop_string(ctx, -2, "constants");

    /* Also: devNull (added in node 16) */
    duk_push_string(ctx, "/dev/null");
    duk_put_prop_string(ctx, -2, "devNull");
}
/* ============================================================
 * process — node-style process object.
 * Native C for syscalls (cwd, chdir, hrtime, kill, memoryUsage,
 * platform/arch), inline JS for the assembly + EventEmitter
 * inheritance. Inherits the EventEmitter from nodeshim.events so
 * process.on('exit', fn) works.
 * Deferred: process.report, process.dlopen, full signal handling,
 * cluster integration, worker_threads integration.
 * ============================================================ */

static duk_ret_t proc_cwd(duk_context *ctx)
{
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) == NULL)
        RP_THROW(ctx, "process.cwd: getcwd failed: %s", strerror(errno));
    duk_push_string(ctx, buf);
    return 1;
}

static duk_ret_t proc_chdir(duk_context *ctx)
{
    const char *dir = ns_require_string(ctx, 0, "directory", NULL);
    if (chdir(dir) != 0)
        RP_THROW(ctx, "process.chdir: %s: %s", dir, strerror(errno));
    return 0;
}

static duk_ret_t proc_kill(duk_context *ctx)
{
    int pid = (int)duk_require_number(ctx, 0);
    int sig = SIGTERM;
    if (duk_is_string(ctx, 1)) {
        const char *s = duk_get_string(ctx, 1);
        /* recognize common signal names */
        if      (strcmp(s, "SIGHUP")  == 0) sig = SIGHUP;
        else if (strcmp(s, "SIGINT")  == 0) sig = SIGINT;
        else if (strcmp(s, "SIGQUIT") == 0) sig = SIGQUIT;
        else if (strcmp(s, "SIGKILL") == 0) sig = SIGKILL;
        else if (strcmp(s, "SIGUSR1") == 0) sig = SIGUSR1;
        else if (strcmp(s, "SIGUSR2") == 0) sig = SIGUSR2;
        else if (strcmp(s, "SIGTERM") == 0) sig = SIGTERM;
        else if (strcmp(s, "SIGCONT") == 0) sig = SIGCONT;
        else if (strcmp(s, "SIGSTOP") == 0) sig = SIGSTOP;
        else if (strcmp(s, "SIGTSTP") == 0) sig = SIGTSTP;
        else if (strcmp(s, "SIGPIPE") == 0) sig = SIGPIPE;
        else if (strcmp(s, "SIGCHLD") == 0) sig = SIGCHLD;
        else RP_THROW(ctx, "process.kill: unknown signal '%s'", s);
    } else if (duk_is_number(ctx, 1)) {
        sig = (int)duk_get_number(ctx, 1);
    }
    if (kill(pid, sig) != 0)
        RP_THROW(ctx, "process.kill: %s", strerror(errno));
    duk_push_boolean(ctx, 1);
    return 1;
}

/* process.hrtime([prev]) — returns [seconds, nanoseconds] using
   CLOCK_MONOTONIC. If prev is given, returns the diff. */
static duk_ret_t proc_hrtime(duk_context *ctx)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        RP_THROW(ctx, "process.hrtime: clock_gettime failed: %s", strerror(errno));
    double s = (double)ts.tv_sec, ns = (double)ts.tv_nsec;
    if (duk_is_array(ctx, 0)) {
        duk_get_prop_index(ctx, 0, 0);
        double ps = duk_get_number(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_index(ctx, 0, 1);
        double pns = duk_get_number(ctx, -1);
        duk_pop(ctx);
        double diff_ns = (s - ps) * 1e9 + (ns - pns);
        double diff_s = diff_ns < 0 ? 0 : (double)((long)(diff_ns / 1e9));
        double rem_ns = diff_ns - diff_s * 1e9;
        duk_push_array(ctx);
        duk_push_number(ctx, diff_s); duk_put_prop_index(ctx, -2, 0);
        duk_push_number(ctx, rem_ns); duk_put_prop_index(ctx, -2, 1);
        return 1;
    }
    duk_push_array(ctx);
    duk_push_number(ctx, s);  duk_put_prop_index(ctx, -2, 0);
    duk_push_number(ctx, ns); duk_put_prop_index(ctx, -2, 1);
    return 1;
}

/* process.hrtime.bigint() — current monotonic time in nanoseconds, as a
   BigInt.  Node's docs: no `prev` argument; do diffs by subtraction.
   We assemble the digit string manually (tv_sec * 1e9 + tv_nsec) so we
   never go through a double — losing precision around bit 53 would
   defeat the point of a nanosecond clock.  Then hand the string to the
   JS BigInt constructor (duktape fork's v1 BigInt). */
static duk_ret_t proc_hrtime_bigint(duk_context *ctx)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        RP_THROW(ctx, "process.hrtime.bigint: clock_gettime failed: %s", strerror(errno));
    /* tv_nsec is 0..999_999_999, exactly 9 decimal digits when zero-padded. */
    char buf[40];
    snprintf(buf, sizeof(buf), "%lld%09ld",
        (long long)ts.tv_sec, (long)ts.tv_nsec);
    duk_get_global_string(ctx, "BigInt");
    duk_push_string(ctx, buf);
    duk_call(ctx, 1);
    return 1;
}

/* process.memoryUsage() — returns { rss, heapTotal, heapUsed, external, arrayBuffers } */
static duk_ret_t proc_memory_usage(duk_context *ctx)
{
    struct rusage ru;
    long rss = 0;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#ifdef __APPLE__
        rss = ru.ru_maxrss;          /* bytes on macOS */
#else
        rss = ru.ru_maxrss * 1024;   /* kilobytes on linux */
#endif
    }
    duk_push_object(ctx);
    duk_push_number(ctx, (double)rss);   duk_put_prop_string(ctx, -2, "rss");
    duk_push_number(ctx, (double)rss);   duk_put_prop_string(ctx, -2, "heapTotal");
    duk_push_number(ctx, (double)rss);   duk_put_prop_string(ctx, -2, "heapUsed");
    duk_push_number(ctx, 0.0);           duk_put_prop_string(ctx, -2, "external");
    duk_push_number(ctx, 0.0);           duk_put_prop_string(ctx, -2, "arrayBuffers");
    return 1;
}

/* process.uptime() — seconds since startup. Approximated via CLOCK_MONOTONIC */
static struct timespec proc_uptime_origin;
static int proc_uptime_origin_set = 0;
static duk_ret_t proc_uptime(duk_context *ctx)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        RP_THROW(ctx, "process.uptime: clock_gettime failed: %s", strerror(errno));
    if (!proc_uptime_origin_set) {
        proc_uptime_origin = ts;
        proc_uptime_origin_set = 1;
    }
    double s = (ts.tv_sec - proc_uptime_origin.tv_sec)
             + (ts.tv_nsec - proc_uptime_origin.tv_nsec) / 1e9;
    duk_push_number(ctx, s);
    return 1;
}

/* process.umask([mask]) — get/set umask */
static duk_ret_t proc_umask(duk_context *ctx)
{
    if (duk_get_top(ctx) == 0 || duk_is_undefined(ctx, 0)) {
        mode_t old = umask(0);
        umask(old);
        duk_push_int(ctx, (int)old);
        return 1;
    }
    mode_t m = (mode_t)duk_require_int(ctx, 0);
    mode_t old = umask(m);
    duk_push_int(ctx, (int)old);
    return 1;
}

/* ---- getrusage helpers for cpuUsage / resourceUsage ---- */
#include <sys/resource.h>

/* Convert timeval to microseconds (int64). */
static int64_t _ru_tv_to_us(const struct timeval *tv)
{
    return (int64_t)tv->tv_sec * 1000000LL + (int64_t)tv->tv_usec;
}

/* process.cpuUsage([previousValue]) -> {user, system}  (microseconds) */
static duk_ret_t proc_cpu_usage(duk_context *ctx)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        RP_THROW(ctx, "getrusage: %s", strerror(errno));

    int64_t user = _ru_tv_to_us(&ru.ru_utime);
    int64_t sys  = _ru_tv_to_us(&ru.ru_stime);

    /* Subtract previous measurement if supplied. */
    if (duk_is_object(ctx, 0)) {
        if (duk_get_prop_string(ctx, 0, "user")) user -= (int64_t)duk_get_number(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "system")) sys -= (int64_t)duk_get_number(ctx, -1);
        duk_pop(ctx);
    }

    duk_push_object(ctx);
    duk_push_number(ctx, (double)user); duk_put_prop_string(ctx, -2, "user");
    duk_push_number(ctx, (double)sys);  duk_put_prop_string(ctx, -2, "system");
    return 1;
}

/* process.resourceUsage() -> full getrusage breakdown */
static duk_ret_t proc_resource_usage(duk_context *ctx)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        RP_THROW(ctx, "getrusage: %s", strerror(errno));

#define PUT_NUM(name, val) do { \
    duk_push_number(ctx, (double)(val)); \
    duk_put_prop_string(ctx, -2, name); \
} while(0)

    duk_push_object(ctx);
    PUT_NUM("userCPUTime",          _ru_tv_to_us(&ru.ru_utime));
    PUT_NUM("systemCPUTime",        _ru_tv_to_us(&ru.ru_stime));
    PUT_NUM("maxRSS",               ru.ru_maxrss);
    PUT_NUM("sharedMemorySize",     ru.ru_ixrss);
    PUT_NUM("unsharedDataSize",     ru.ru_idrss);
    PUT_NUM("unsharedStackSize",    ru.ru_isrss);
    PUT_NUM("minorPageFault",       ru.ru_minflt);
    PUT_NUM("majorPageFault",       ru.ru_majflt);
    PUT_NUM("swappedOut",           ru.ru_nswap);
    PUT_NUM("fsRead",               ru.ru_inblock);
    PUT_NUM("fsWrite",              ru.ru_oublock);
    PUT_NUM("ipcSent",              ru.ru_msgsnd);
    PUT_NUM("ipcReceived",          ru.ru_msgrcv);
    PUT_NUM("signalsCount",         ru.ru_nsignals);
    PUT_NUM("voluntaryContextSwitches",   ru.ru_nvcsw);
    PUT_NUM("involuntaryContextSwitches", ru.ru_nivcsw);
#undef PUT_NUM
    return 1;
}

/* process.availableMemory() -> approximate free bytes  (sysconf based) */
static duk_ret_t proc_available_memory(duk_context *ctx)
{
#if defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long psize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psize > 0) {
        duk_push_number(ctx, (double)pages * (double)psize);
        return 1;
    }
#endif
    duk_push_number(ctx, 0);
    return 1;
}

/* process.setuid / setgid / setegid / seteuid -- accept name or numeric id. */
static int _resolve_uid(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_number(ctx, idx)) return duk_get_int(ctx, idx);
    if (duk_is_string(ctx, idx)) {
        struct passwd *pw = getpwnam(duk_get_string(ctx, idx));
        if (!pw) RP_THROW(ctx, "unknown user '%s'", duk_get_string(ctx, idx));
        return (int)pw->pw_uid;
    }
    RP_THROW(ctx, "uid must be a number or user name");
}
static int _resolve_gid(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_number(ctx, idx)) return duk_get_int(ctx, idx);
    if (duk_is_string(ctx, idx)) {
        struct group *gr = getgrnam(duk_get_string(ctx, idx));
        if (!gr) RP_THROW(ctx, "unknown group '%s'", duk_get_string(ctx, idx));
        return (int)gr->gr_gid;
    }
    RP_THROW(ctx, "gid must be a number or group name");
}

static duk_ret_t proc_setuid(duk_context *ctx)
{
    uid_t u = (uid_t)_resolve_uid(ctx, 0);
    if (setuid(u) != 0) RP_THROW(ctx, "setuid: %s", strerror(errno));
    return 0;
}
static duk_ret_t proc_seteuid(duk_context *ctx)
{
    uid_t u = (uid_t)_resolve_uid(ctx, 0);
    if (seteuid(u) != 0) RP_THROW(ctx, "seteuid: %s", strerror(errno));
    return 0;
}
static duk_ret_t proc_setgid(duk_context *ctx)
{
    gid_t g = (gid_t)_resolve_gid(ctx, 0);
    if (setgid(g) != 0) RP_THROW(ctx, "setgid: %s", strerror(errno));
    return 0;
}
static duk_ret_t proc_setegid(duk_context *ctx)
{
    gid_t g = (gid_t)_resolve_gid(ctx, 0);
    if (setegid(g) != 0) RP_THROW(ctx, "setegid: %s", strerror(errno));
    return 0;
}
static duk_ret_t proc_setgroups(duk_context *ctx)
{
    if (!duk_is_array(ctx, 0))
        RP_THROW(ctx, "setgroups: argument must be an array of gids/names");
    duk_size_t n = duk_get_length(ctx, 0);
    if (n > NGROUPS_MAX) RP_THROW(ctx, "setgroups: too many groups");
    gid_t *gids = (gid_t *)malloc(n * sizeof(gid_t));
    if (!gids) RP_THROW(ctx, "setgroups: oom");
    for (duk_size_t i = 0; i < n; i++) {
        duk_get_prop_index(ctx, 0, (duk_uarridx_t)i);
        gids[i] = (gid_t)_resolve_gid(ctx, -1);
        duk_pop(ctx);
    }
    int r = setgroups(n, gids);
    int save = errno;
    free(gids);
    if (r != 0) RP_THROW(ctx, "setgroups: %s", strerror(save));
    return 0;
}

/* Plain writes to stdout / stderr — bypass console.log/error, which in
   rampart format with "Error:" prefix + stack trace.  Node's
   process.stdout.write / process.stderr.write are just fputs to the
   respective fd with no decoration. */
static duk_ret_t proc_stdout_write(duk_context *ctx)
{
    duk_size_t len;
    const char *s = duk_safe_to_lstring(ctx, 0, &len);
    if (s && len) fwrite(s, 1, len, stdout);
    fflush(stdout);
    duk_push_true(ctx);
    return 1;
}

static duk_ret_t proc_stderr_write(duk_context *ctx)
{
    duk_size_t len;
    const char *s = duk_safe_to_lstring(ctx, 0, &len);
    if (s && len) fwrite(s, 1, len, stderr);
    fflush(stderr);
    duk_push_true(ctx);
    return 1;
}

/* Inline JS that takes a `natives` object containing C helpers + the
   rampart-process source, and assembles a node-style process. Made an
   EventEmitter by inheriting from nodeshim.events. */
static const char *process_js =
"function(natives, rampartProc, rampartVer, EventEmitter) {\n"
"  'use strict';\n"
"  /* Build process as an EventEmitter instance */\n"
"  function Process() { EventEmitter.call(this); }\n"
"  Process.prototype = Object.create(EventEmitter.prototype);\n"
"  Process.prototype.constructor = Process;\n"
"  var p = new Process();\n"
"\n"
"  /* Properties copied from rampart's process */\n"
"  p.argv = rampartProc.argv ? rampartProc.argv.slice() : [];\n"
"  p.argv0 = rampartProc.argv0 || (p.argv[0] || 'rampart');\n"
"  p.execPath = rampartProc.installPathBin\n"
"             ? (rampartProc.installPathBin + '/rampart')\n"
"             : (p.argv0 || 'rampart');\n"
"  p.execArgv = [];\n"
"  p.env = rampartProc.env || {};\n"
"  p.pid = (rampartProc.getpid ? rampartProc.getpid() : 0);\n"
"  p.ppid = (rampartProc.getppid ? rampartProc.getppid() : 0);\n"
"  p.title = 'rampart';\n"
"  p.platform = natives.platform;\n"
"  p.arch = natives.arch;\n"
"  p.version = 'v' + rampartVer + '-nodeshim';\n"
"  p.versions = {\n"
"    node:    '0.0.0-nodeshim',\n"
"    rampart: rampartVer,\n"
"    duktape: '2.x'\n"
"  };\n"
"  p.release = { name: 'rampart-nodeshim', sourceUrl: '', headersUrl: '' };\n"
"  p.config = {};\n"
"  p.allowedNodeEnvironmentFlags = new Set ? new Set() : { has: function(){ return false; } };\n"
"  p.exitCode = 0;\n"
"\n"
"  /* Methods: prefer native C; fall back to rampart's if absent */\n"
"  p.cwd = natives.cwd;\n"
"  p.chdir = natives.chdir;\n"
"  p.hrtime = natives.hrtime;\n"
"  /* node-compat: process.hrtime.bigint() returns a BigInt of nanoseconds. */\n"
"  try { p.hrtime.bigint = natives.hrtimeBigint; } catch (_) {}\n"
"  p.memoryUsage = natives.memoryUsage;\n"
"  p.uptime = natives.uptime;\n"
"  p.umask = natives.umask;\n"
"  p.kill = natives.kill;\n"
"  p.cpuUsage = natives.cpuUsage;\n"
"  p.resourceUsage = natives.resourceUsage;\n"
"  p.availableMemory = natives.availableMemory;\n"
"  p.setuid = natives.setuid;\n"
"  p.seteuid = natives.seteuid;\n"
"  p.setgid = natives.setgid;\n"
"  p.setegid = natives.setegid;\n"
"  p.setgroups = natives.setgroups;\n"
"  /* getBuiltinModule(name) -- node 22+.  Looks up a rampart-nodeshim\n"
"     submodule by name and returns it, or undefined. */\n"
"  p.getBuiltinModule = function(name) {\n"
"    try { return require('rampart-nodeshim')[name]; }\n"
"    catch (e) { return undefined; }\n"
"  };\n"
"  /* mainModule -- legacy alias; rampart doesn't have node-style module\n"
"     objects, but expose `{ filename }` for compat. */\n"
"  if (typeof rampartProc !== 'undefined' && rampartProc.script)\n"
"    p.mainModule = { filename: rampartProc.script, exports: {} };\n"
"  /* loadEnvFile([path]) -- node 20+.  Reads a .env-style file and\n"
"     merges into process.env.  Uses util.parseEnv (added below). */\n"
"  p.loadEnvFile = function(path) {\n"
"    if (path === undefined) path = '.env';\n"
"    var fs = require('rampart-nodeshim').fs;\n"
"    var util = require('rampart-nodeshim').util;\n"
"    var content = fs.readFileSync(path, 'utf8');\n"
"    var parsed = util.parseEnv(content);\n"
"    for (var k in parsed) p.env[k] = parsed[k];\n"
"  };\n"
"  /* node deprecation flags -- writable booleans honored by emitWarning.\n"
"     rampart-nodeshim doesn't auto-emit deprecation warnings yet, but\n"
"     code that checks these flags (or sets them defensively) shouldn't\n"
"     fail. */\n"
"  p.noDeprecation    = false;\n"
"  p.throwDeprecation = false;\n"
"  p.traceDeprecation = false;\n"
"  p.exit = function(code) {\n"
"    if (arguments.length === 0 || code === undefined) code = p.exitCode || 0;\n"
"    p.exitCode = code | 0;\n"
"    try { p.emit('exit', p.exitCode); } catch (e) {}\n"
"    if (rampartProc && typeof rampartProc.exit === 'function') return rampartProc.exit(code | 0);\n"
"    throw new Error('process.exit(' + code + ')');\n"
"  };\n"
"  p.abort = function() {\n"
"    if (rampartProc && typeof rampartProc.exit === 'function') return rampartProc.exit(134);\n"
"    throw new Error('process.abort()');\n"
"  };\n"
"  p.nextTick = function(fn /*, ...args */) {\n"
"    if (typeof fn !== 'function') {\n"
"      var e = new TypeError('The \"callback\" argument must be of type function. Received type ' + typeof fn);\n"
"      e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    var args = [];\n"
"    for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);\n"
"    if (typeof Promise !== 'undefined' && Promise.resolve) {\n"
"      Promise.resolve().then(function() { fn.apply(null, args); });\n"
"    } else if (typeof setTimeout === 'function') {\n"
"      setTimeout(function() { fn.apply(null, args); }, 0);\n"
"    } else {\n"
"      fn.apply(null, args);\n"
"    }\n"
"  };\n"
"  p.emitWarning = function(warning, type, code) {\n"
"    if (typeof console !== 'undefined' && console.warn) {\n"
"      var msg = typeof warning === 'string' ? warning :\n"
"                (warning && warning.message) ? warning.message : String(warning);\n"
"      console.warn((type || 'Warning') + ': ' + msg + (code ? ' [' + code + ']' : ''));\n"
"    }\n"
"  };\n"
"  p.binding = function(name) {\n"
"    throw new Error('process.binding(\"' + name + '\") not supported in rampart-nodeshim');\n"
"  };\n"
"  /* uid/gid stubs — only succeed when present in rampart bindings */\n"
"  p.getuid = function() { return typeof natives.getuid === 'function' ? natives.getuid() : 0; };\n"
"  p.geteuid = p.getuid;\n"
"  p.getgid = function() { return typeof natives.getgid === 'function' ? natives.getgid() : 0; };\n"
"  p.getegid = p.getgid;\n"
"  p.getgroups = function() { return []; };\n"
"\n"
"  /* Minimal stdout/stderr/stdin streams.  .write is fputs to the\n"
"     respective fd via the native writers in `natives` (no Error:\n"
"     prefix or stack-trace decoration that console.error would add). */\n"
"  function makeStream(fd, writer) {\n"
"    var s = new EventEmitter();\n"
"    s.write = writer\n"
"      ? function(chunk) { writer(typeof chunk === 'string' ? chunk : String(chunk)); return true; }\n"
"      : function() { return true; };\n"
"    s.end = function() {};\n"
"    s.isTTY = false;\n"
"    s.fd = fd;\n"
"    return s;\n"
"  }\n"
"  p.stdout = makeStream(1, natives.stdoutWrite);\n"
"  p.stderr = makeStream(2, natives.stderrWrite);\n"
"  p.stdin  = makeStream(0, null);\n"
"\n"
"  return p;\n"
"}";

static void nodeshim_init_process(duk_context *ctx)
{
    /* Compile process_js as a function that takes (natives, rampartProc, rampartVer, EventEmitter) */
    duk_push_string(ctx, "rampart-nodeshim.c:process_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, process_js);

    /* arg 1: natives object */
    duk_push_object(ctx);
    duk_push_c_function(ctx, proc_cwd,          0); duk_put_prop_string(ctx, -2, "cwd");
    duk_push_c_function(ctx, proc_chdir,        1); duk_put_prop_string(ctx, -2, "chdir");
    duk_push_c_function(ctx, proc_kill,         2); duk_put_prop_string(ctx, -2, "kill");
    duk_push_c_function(ctx, proc_hrtime,       1); duk_put_prop_string(ctx, -2, "hrtime");
    duk_push_c_function(ctx, proc_hrtime_bigint,0); duk_put_prop_string(ctx, -2, "hrtimeBigint");
    duk_push_c_function(ctx, proc_memory_usage, 0); duk_put_prop_string(ctx, -2, "memoryUsage");
    duk_push_c_function(ctx, proc_uptime,       0); duk_put_prop_string(ctx, -2, "uptime");
    duk_push_c_function(ctx, proc_umask,        1); duk_put_prop_string(ctx, -2, "umask");
    duk_push_c_function(ctx, proc_cpu_usage,    1); duk_put_prop_string(ctx, -2, "cpuUsage");
    duk_push_c_function(ctx, proc_resource_usage, 0); duk_put_prop_string(ctx, -2, "resourceUsage");
    duk_push_c_function(ctx, proc_available_memory, 0); duk_put_prop_string(ctx, -2, "availableMemory");
    duk_push_c_function(ctx, proc_setuid,       1); duk_put_prop_string(ctx, -2, "setuid");
    duk_push_c_function(ctx, proc_seteuid,      1); duk_put_prop_string(ctx, -2, "seteuid");
    duk_push_c_function(ctx, proc_setgid,       1); duk_put_prop_string(ctx, -2, "setgid");
    duk_push_c_function(ctx, proc_setegid,      1); duk_put_prop_string(ctx, -2, "setegid");
    duk_push_c_function(ctx, proc_setgroups,    1); duk_put_prop_string(ctx, -2, "setgroups");
    duk_push_c_function(ctx, proc_stdout_write, 1); duk_put_prop_string(ctx, -2, "stdoutWrite");
    duk_push_c_function(ctx, proc_stderr_write, 1); duk_put_prop_string(ctx, -2, "stderrWrite");
    duk_push_string(ctx, NS_PLATFORM); duk_put_prop_string(ctx, -2, "platform");
    duk_push_string(ctx, NS_ARCH);     duk_put_prop_string(ctx, -2, "arch");

    /* arg 2: rampart's process (for argv/env/pid/etc.) */
    duk_get_global_string(ctx, "process");
    if (!duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    /* arg 3: rampart version string */
    duk_get_global_string(ctx, "rampart");
    if (duk_get_prop_string(ctx, -1, "version") && duk_is_string(ctx, -1)) {
        duk_remove(ctx, -2);
    } else {
        duk_pop(ctx);
        duk_pop(ctx);
        duk_push_string(ctx, "0.0.0");
    }

    /* arg 4: EventEmitter class — pulled from the partially-built
       top-level object so we don't re-enter the module loader. The
       slot ordering in duk_open_module ensures `events` is populated
       before `process`. */
    duk_get_prop_string(ctx, -5 /* top-level at this depth */, "events");

    duk_call(ctx, 4);
}
/* ============================================================
 * buffer — node's buffer module.
 *
 * Returns an object whose properties mirror node:
 *   - Buffer (the class) → global Buffer (extended in
 *     duktape/globals/rampart-buffer.c with encoding-aware
 *     toString/from/byteLength/write, indexOf, swap*, etc.)
 *   - kMaxLength, kStringMaxLength, INSPECT_MAX_BYTES
 *   - atob/btoa (browser-style base64)
 *   - constants.MAX_LENGTH / MAX_STRING_LENGTH
 *
 * The bulk of correctness lives in rampart-buffer.c, since fixing
 * encoding handling on the global Buffer benefits all rampart code,
 * not just nodeshim consumers.
 * ============================================================ */

static const char *buffer_js =
"function() {\n"
"  'use strict';\n"
"  function atob(s) {\n"
"    if (typeof s !== 'string') s = String(s);\n"
"    return Buffer.from(s, 'base64').toString('latin1');\n"
"  }\n"
"  function btoa(s) {\n"
"    if (typeof s !== 'string') s = String(s);\n"
"    return Buffer.from(s, 'latin1').toString('base64');\n"
"  }\n"
"  /* SlowBuffer is deprecated in node; alias to Buffer for compat */\n"
"  function SlowBuffer(size) { return Buffer.alloc(size); }\n"
"  return {\n"
"    Buffer: Buffer,\n"
"    SlowBuffer: SlowBuffer,\n"
"    kMaxLength: 0x7FFFFFFF,\n"
"    kStringMaxLength: 0x1FFFFFE8,\n"
"    INSPECT_MAX_BYTES: 50,\n"
"    atob: atob,\n"
"    btoa: btoa,\n"
"    constants: {\n"
"      MAX_LENGTH: 0x7FFFFFFF,\n"
"      MAX_STRING_LENGTH: 0x1FFFFFE8\n"
"    },\n"
"    /* Blob and File live in rampart-whatwg.so (lazy-loaded).\n"
"       Eagerly reading `Blob` here would trigger the lazy getter\n"
"       DURING nodeshim's own load chain — which then tries to\n"
"       require nodeshim (cyclic) and crashes.  Defer via a getter\n"
"       so the lookup only fires on actual user access. */\n"
"    resolveObjectURL: function() { return undefined; },\n"
"    isUtf8: function(buf) { return true; },  /* approximation */\n"
"    isAscii: function(buf) {\n"
"      if (!Buffer.isBuffer(buf)) return false;\n"
"      for (var i = 0; i < buf.length; i++) if (buf[i] > 127) return false;\n"
"      return true;\n"
"    },\n"
"    transcode: function() { throw new Error('Buffer.transcode not supported in rampart-nodeshim'); }\n"
"  };\n"
"}";

static void nodeshim_init_buffer(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:buffer_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, buffer_js);
    duk_call(ctx, 0);
    /* Attach Blob and File as lazy getters: lookup happens only on
     * actual access to `require('buffer').Blob`, not during nodeshim
     * load.  Otherwise the cyclic load (nodeshim → buffer_js → Blob
     * getter → require whatwg → require nodeshim partial) crashes. */
    duk_eval_string(ctx,
        "(function(bufMod) {"
        "  Object.defineProperty(bufMod, 'Blob', {"
        "    configurable: true, enumerable: true,"
        "    get: function(){ return globalThis.Blob; }"
        "  });"
        "  Object.defineProperty(bufMod, 'File', {"
        "    configurable: true, enumerable: true,"
        "    get: function(){ return globalThis.File; }"
        "  });"
        "  return bufMod;"
        "})");
    duk_dup(ctx, -2);          /* dup the buffer module exports */
    duk_call(ctx, 1);
    duk_pop(ctx);              /* pop the returned bufMod (same as -2) */
}
/* ============================================================
 * events — node-style EventEmitter.
 * ------------------------------------------------------------
 * Implemented as inline JS (Pattern 2). Justification:
 * EventEmitter is essentially object/array bookkeeping; the work
 * is duktape hashtable access whether we drive it from C stack
 * manipulation or from JS property access. JS form is ~150 lines
 * vs ~600 lines of C with no meaningful perf delta. C is reserved
 * for byte-level / numeric paths where it actually wins (path,
 * crypto, buffer, fs syscall wrappers).
 *
 * Deferred for now (would need full Symbol + reliable Promise
 * available at module-init time):
 *   - EventEmitter.errorMonitor (Symbol)
 *   - EventEmitter.captureRejectionSymbol (Symbol)
 *   - EventEmitter.once(emitter, name) Promise helper
 *   - EventEmitter.on(emitter, name) async iterator helper
 * ============================================================ */

static const char *ee_js =
"function() {\n"
"  'use strict';\n"
"  function EventEmitter(opts) {\n"
"    EventEmitter.init.call(this, opts);\n"
"  }\n"
"  EventEmitter.EventEmitter = EventEmitter;\n"
"  EventEmitter.defaultMaxListeners = 10;\n"
"\n"
"  EventEmitter.init = function(opts) {\n"
"    if (this._events === undefined ||\n"
"        this._events === Object.getPrototypeOf(this)._events) {\n"
"      this._events = Object.create(null);\n"
"      this._eventsCount = 0;\n"
"    }\n"
"    this._maxListeners = this._maxListeners || undefined;\n"
"  };\n"
"\n"
"  EventEmitter.prototype._events = undefined;\n"
"  EventEmitter.prototype._eventsCount = 0;\n"
"  EventEmitter.prototype._maxListeners = undefined;\n"
"\n"
"  function _inspect(v) {\n"
"    if (v === null) return 'null';\n"
"    if (v === undefined) return 'undefined';\n"
"    if (typeof v === 'string') return \"'\" + v.replace(/'/g, \"\\\\'\") + \"'\";\n"
"    if (typeof v === 'number' || typeof v === 'boolean') return String(v);\n"
"    if (typeof v === 'function') return '[Function: ' + (v.name || 'anonymous') + ']';\n"
"    if (v instanceof Error) return v.toString();\n"
"    if (Array.isArray(v)) {\n"
"      var items = [];\n"
"      for (var i = 0; i < v.length; i++) items.push(_inspect(v[i]));\n"
"      return '[ ' + items.join(', ') + ' ]';\n"
"    }\n"
"    if (typeof v === 'object') {\n"
"      /* honor util.inspect.custom-style hook: if the object has a function\n"
"         under the well-known custom-inspect key, call it. If it throws,\n"
"         fall back to '[object Object]'. node uses a Symbol for this; until\n"
"         we have Symbol support, the harness's util shim uses a string key. */\n"
"      var customKey = '__nodeshim_util_inspect_custom__';\n"
"      try {\n"
"        if (typeof v[customKey] === 'function') {\n"
"          try { return v[customKey](); }\n"
"          catch (e) { return '[object Object]'; }\n"
"        }\n"
"        var keys = Object.keys(v);\n"
"        /* skip the custom-key (Symbol equivalent — non-enumerable in node) */\n"
"        var visible = [];\n"
"        for (var i = 0; i < keys.length; i++)\n"
"          if (keys[i] !== customKey) visible.push(keys[i]);\n"
"        if (visible.length === 0) return '{}';\n"
"        var pairs = [];\n"
"        for (var j = 0; j < visible.length; j++)\n"
"          pairs.push(visible[j] + ': ' + _inspect(v[visible[j]]));\n"
"        return '{ ' + pairs.join(', ') + ' }';\n"
"      } catch (e) { return '[object Object]'; }\n"
"    }\n"
"    return String(v);\n"
"  }\n"
"\n"
"  function _describeType(v) {\n"
"    if (v === null) return 'null';\n"
"    if (Array.isArray(v)) return 'object';\n"
"    return typeof v;\n"
"  }\n"
"  function _argTypeSuffix(v) {\n"
"    if (v == null) return ' Received ' + v;\n"
"    if (typeof v === 'function') return ' Received function ' + (v.name || '');\n"
"    if (typeof v === 'object') {\n"
"      var c = v.constructor && v.constructor.name;\n"
"      return c ? ' Received an instance of ' + c : ' Received [object Object]';\n"
"    }\n"
"    var s = _inspect(v);\n"
"    if (s.length > 28) s = s.slice(0, 25) + '...';\n"
"    return ' Received type ' + (typeof v) + ' (' + s + ')';\n"
"  }\n"
"\n"
"  function checkListener(listener) {\n"
"    if (typeof listener !== 'function') {\n"
"      var e = new TypeError('The \"listener\" argument must be of type function.' + _argTypeSuffix(listener));\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"  }\n"
"\n"
"  EventEmitter.prototype.setMaxListeners = function setMaxListeners(n) {\n"
"    if (typeof n !== 'number' || n < 0 || n !== n) {\n"
"      var e = new RangeError('The value of \"n\" is out of range. It must be a non-negative number. Received ' + n);\n"
"      e.code = 'ERR_OUT_OF_RANGE';\n"
"      throw e;\n"
"    }\n"
"    this._maxListeners = n;\n"
"    return this;\n"
"  };\n"
"\n"
"  function _getMaxListeners(that) {\n"
"    if (that._maxListeners === undefined) return EventEmitter.defaultMaxListeners;\n"
"    return that._maxListeners;\n"
"  }\n"
"\n"
"  EventEmitter.prototype.getMaxListeners = function getMaxListeners() {\n"
"    return _getMaxListeners(this);\n"
"  };\n"
"\n"
"  EventEmitter.prototype.emit = function emit(type) {\n"
"    var args = [];\n"
"    for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);\n"
"\n"
"    var doError = (type === 'error');\n"
"    var events = this._events;\n"
"    if (events !== undefined) {\n"
"      if (doError && events.error !== undefined) doError = false;\n"
"    } else if (!doError) {\n"
"      return false;\n"
"    }\n"
"\n"
"    if (doError) {\n"
"      var er;\n"
"      if (args.length > 0) er = args[0];\n"
"      if (er instanceof Error) throw er;\n"
"      var err = new Error('Unhandled error.' + (er !== undefined ? ' (' + _inspect(er) + ')' : ''));\n"
"      err.context = er;\n"
"      err.code = 'ERR_UNHANDLED_ERROR';\n"
"      throw err;\n"
"    }\n"
"\n"
"    var handler = events[type];\n"
"    if (handler === undefined) return false;\n"
"\n"
"    if (typeof handler === 'function') {\n"
"      handler.apply(this, args);\n"
"    } else {\n"
"      var len = handler.length;\n"
"      var listeners = arrayClone(handler);\n"
"      for (var j = 0; j < len; ++j) {\n"
"        listeners[j].apply(this, args);\n"
"      }\n"
"    }\n"
"    return true;\n"
"  };\n"
"\n"
"  function _addListener(target, type, listener, prepend) {\n"
"    checkListener(listener);\n"
"    var events = target._events;\n"
"    if (events === undefined) {\n"
"      events = target._events = Object.create(null);\n"
"      target._eventsCount = 0;\n"
"    } else {\n"
"      if (events.newListener !== undefined) {\n"
"        target.emit('newListener', type,\n"
"                    listener.listener ? listener.listener : listener);\n"
"        events = target._events;\n"
"      }\n"
"    }\n"
"    var existing = events[type];\n"
"    if (existing === undefined) {\n"
"      events[type] = listener;\n"
"      ++target._eventsCount;\n"
"    } else {\n"
"      if (typeof existing === 'function') {\n"
"        existing = events[type] = prepend ? [listener, existing] : [existing, listener];\n"
"      } else if (prepend) {\n"
"        existing.unshift(listener);\n"
"      } else {\n"
"        existing.push(listener);\n"
"      }\n"
"      var m = _getMaxListeners(target);\n"
"      if (m > 0 && existing.length > m && !existing.warned) {\n"
"        existing.warned = true;\n"
"        var w = new Error('Possible EventEmitter memory leak detected. ' +\n"
"          existing.length + ' ' + String(type) +\n"
"          ' listeners added to ' + String(target) +\n"
"          '. Use emitter.setMaxListeners() to increase limit');\n"
"        w.name = 'MaxListenersExceededWarning';\n"
"        w.emitter = target;\n"
"        w.type = type;\n"
"        w.count = existing.length;\n"
"        if (typeof console !== 'undefined' && console.warn) console.warn(w.message);\n"
"      }\n"
"    }\n"
"    return target;\n"
"  }\n"
"\n"
"  EventEmitter.prototype.addListener = function addListener(type, listener) {\n"
"    return _addListener(this, type, listener, false);\n"
"  };\n"
"  EventEmitter.prototype.on = EventEmitter.prototype.addListener;\n"
"  EventEmitter.prototype.prependListener = function prependListener(type, listener) {\n"
"    return _addListener(this, type, listener, true);\n"
"  };\n"
"\n"
"  function _onceWrap(target, type, listener) {\n"
"    var state = { fired: false, wrapFn: undefined, target: target, type: type, listener: listener };\n"
"    function wrapper() {\n"
"      if (!state.fired) {\n"
"        target.removeListener(type, state.wrapFn);\n"
"        state.fired = true;\n"
"        return listener.apply(target, arguments);\n"
"      }\n"
"    }\n"
"    wrapper.listener = listener;\n"
"    state.wrapFn = wrapper;\n"
"    return wrapper;\n"
"  }\n"
"\n"
"  EventEmitter.prototype.once = function once(type, listener) {\n"
"    checkListener(listener);\n"
"    this.on(type, _onceWrap(this, type, listener));\n"
"    return this;\n"
"  };\n"
"  EventEmitter.prototype.prependOnceListener = function prependOnceListener(type, listener) {\n"
"    checkListener(listener);\n"
"    this.prependListener(type, _onceWrap(this, type, listener));\n"
"    return this;\n"
"  };\n"
"\n"
"  EventEmitter.prototype.removeListener = function removeListener(type, listener) {\n"
"    checkListener(listener);\n"
"    var events = this._events;\n"
"    if (events === undefined) return this;\n"
"    var list = events[type];\n"
"    if (list === undefined) return this;\n"
"\n"
"    if (list === listener || list.listener === listener) {\n"
"      if (--this._eventsCount === 0) {\n"
"        this._events = Object.create(null);\n"
"      } else {\n"
"        delete events[type];\n"
"        if (events.removeListener !== undefined)\n"
"          this.emit('removeListener', type, list.listener || listener);\n"
"      }\n"
"    } else if (typeof list !== 'function') {\n"
"      var position = -1;\n"
"      for (var i = list.length - 1; i >= 0; i--) {\n"
"        if (list[i] === listener || list[i].listener === listener) {\n"
"          position = i;\n"
"          break;\n"
"        }\n"
"      }\n"
"      if (position < 0) return this;\n"
"      if (position === 0) list.shift();\n"
"      else list.splice(position, 1);\n"
"      if (list.length === 1) events[type] = list[0];\n"
"      if (events.removeListener !== undefined)\n"
"        this.emit('removeListener', type, listener);\n"
"    }\n"
"    return this;\n"
"  };\n"
"  EventEmitter.prototype.off = EventEmitter.prototype.removeListener;\n"
"\n"
"  EventEmitter.prototype.removeAllListeners = function removeAllListeners(type) {\n"
"    var events = this._events;\n"
"    if (events === undefined) return this;\n"
"\n"
"    if (events.removeListener === undefined) {\n"
"      if (arguments.length === 0) {\n"
"        this._events = Object.create(null);\n"
"        this._eventsCount = 0;\n"
"      } else if (events[type] !== undefined) {\n"
"        if (--this._eventsCount === 0) this._events = Object.create(null);\n"
"        else delete events[type];\n"
"      }\n"
"      return this;\n"
"    }\n"
"\n"
"    if (arguments.length === 0) {\n"
"      var keys = Object.keys(events);\n"
"      for (var i = 0; i < keys.length; ++i) {\n"
"        if (keys[i] === 'removeListener') continue;\n"
"        this.removeAllListeners(keys[i]);\n"
"      }\n"
"      this.removeAllListeners('removeListener');\n"
"      this._events = Object.create(null);\n"
"      this._eventsCount = 0;\n"
"      return this;\n"
"    }\n"
"\n"
"    var listeners = events[type];\n"
"    if (typeof listeners === 'function') {\n"
"      this.removeListener(type, listeners);\n"
"    } else if (listeners !== undefined) {\n"
"      for (var k = listeners.length - 1; k >= 0; k--) {\n"
"        this.removeListener(type, listeners[k]);\n"
"      }\n"
"    }\n"
"    return this;\n"
"  };\n"
"\n"
"  function _listeners(target, type, unwrap) {\n"
"    var events = target._events;\n"
"    if (events === undefined) return [];\n"
"    var evl = events[type];\n"
"    if (evl === undefined) return [];\n"
"    if (typeof evl === 'function')\n"
"      return unwrap ? [evl.listener || evl] : [evl];\n"
"    return unwrap ? unwrapListeners(evl) : arrayClone(evl);\n"
"  }\n"
"\n"
"  EventEmitter.prototype.listeners = function listeners(type) {\n"
"    return _listeners(this, type, true);\n"
"  };\n"
"  EventEmitter.prototype.rawListeners = function rawListeners(type) {\n"
"    return _listeners(this, type, false);\n"
"  };\n"
"\n"
"  function _listenerCount(emitter, type) {\n"
"    var events = emitter._events;\n"
"    if (events !== undefined) {\n"
"      var evl = events[type];\n"
"      if (typeof evl === 'function') return 1;\n"
"      if (evl !== undefined) return evl.length;\n"
"    }\n"
"    return 0;\n"
"  }\n"
"  EventEmitter.prototype.listenerCount = function listenerCount(type) {\n"
"    return _listenerCount(this, type);\n"
"  };\n"
"  EventEmitter.listenerCount = function(emitter, type) {\n"
"    if (typeof emitter.listenerCount === 'function')\n"
"      return emitter.listenerCount(type);\n"
"    return _listenerCount(emitter, type);\n"
"  };\n"
"\n"
"  EventEmitter.prototype.eventNames = function eventNames() {\n"
"    return this._eventsCount > 0 ? Object.keys(this._events) : [];\n"
"  };\n"
"\n"
"  function arrayClone(arr) {\n"
"    var copy = new Array(arr.length);\n"
"    for (var i = 0; i < arr.length; ++i) copy[i] = arr[i];\n"
"    return copy;\n"
"  }\n"
"  function unwrapListeners(arr) {\n"
"    var ret = new Array(arr.length);\n"
"    for (var i = 0; i < ret.length; ++i) ret[i] = arr[i].listener || arr[i];\n"
"    return ret;\n"
"  }\n"
"\n"
"  /* ----- top-level helpers (node-compatible statics on the class) ----- */\n"
"\n"
"  /* events.once(emitter, name) -> Promise that resolves with the next\n"
"     event's args (or rejects on 'error'). */\n"
"  EventEmitter.once = function(emitter, name) {\n"
"    var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"    if (!P) throw new Error('events.once requires Promise');\n"
"    return new P(function(resolve, reject) {\n"
"      function cleanup() {\n"
"        emitter.removeListener(name, onEvent);\n"
"        if (name !== 'error') emitter.removeListener('error', onError);\n"
"      }\n"
"      function onEvent() {\n"
"        cleanup();\n"
"        resolve(Array.prototype.slice.call(arguments));\n"
"      }\n"
"      function onError(err) { cleanup(); reject(err); }\n"
"      emitter.on(name, onEvent);\n"
"      if (name !== 'error') emitter.on('error', onError);\n"
"    });\n"
"  };\n"
"\n"
"  /* events.getEventListeners(emitter, name) -- listener array snapshot. */\n"
"  EventEmitter.getEventListeners = function(emitter, name) {\n"
"    if (typeof emitter.listeners === 'function')\n"
"      return emitter.listeners(name).slice();\n"
"    return [];\n"
"  };\n"
"\n"
"  /* events.setMaxListeners(n, ...emitters)\n"
"     With no emitters: sets the module default.\n"
"     With emitters: applies n to each. */\n"
"  EventEmitter.setMaxListeners = function(n) {\n"
"    if (arguments.length === 1) {\n"
"      EventEmitter.defaultMaxListeners = n;\n"
"      return;\n"
"    }\n"
"    for (var i = 1; i < arguments.length; i++) {\n"
"      var e = arguments[i];\n"
"      if (e && typeof e.setMaxListeners === 'function') e.setMaxListeners(n);\n"
"    }\n"
"  };\n"
"\n"
"  /* events.addAbortListener(signal, listener)\n"
"     Registers a one-shot 'abort' listener on the WHATWG AbortSignal.\n"
"     If signal is already aborted, listener is invoked via queueMicrotask.\n"
"     Returns a Disposable (object with Symbol.dispose method) that removes\n"
"     the listener when disposed.  Per node spec.  Throws on bad args. */\n"
"  EventEmitter.addAbortListener = function(signal, listener) {\n"
"    if (!signal || typeof signal.addEventListener !== 'function')\n"
"      throw new TypeError('events.addAbortListener: signal must be an AbortSignal');\n"
"    if (typeof listener !== 'function')\n"
"      throw new TypeError('events.addAbortListener: listener must be a function');\n"
"    if (signal.aborted) {\n"
"      var qm = (typeof queueMicrotask === 'function') ? queueMicrotask\n"
"             : function(f){ Promise.resolve().then(f); };\n"
"      qm(function() { try { listener.call(null, { target: signal }); } catch(_){} });\n"
"    } else {\n"
"      signal.addEventListener('abort', listener, { once: true });\n"
"    }\n"
"    var disposeKey = (typeof Symbol === 'function' && Symbol.dispose)\n"
"                   ? Symbol.dispose : '@@dispose';\n"
"    var d = {};\n"
"    d[disposeKey] = function() {\n"
"      try { signal.removeEventListener('abort', listener); } catch(_){}\n"
"    };\n"
"    return d;\n"
"  };\n"
"\n"
"  /* Node's events module also exposes the class as a `.EventEmitter`\n"
"     property so `var { EventEmitter } = require('events')` works. */\n"
"  EventEmitter.EventEmitter = EventEmitter;\n"
"\n"
"  return EventEmitter;\n"
"}";

static void nodeshim_init_events(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:ee_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, ee_js);
    duk_call(ctx, 0);
    /* Result is the EventEmitter class — left on stack. */
}
/* ============================================================
 * util — node's util module.
 * Inline JS (Pattern 2) — same justification as events; this is
 * all object/function manipulation, no byte-level work to win on.
 *
 * Covers: format, formatWithOptions, inspect (+.custom), promisify
 * (+.custom), callbackify, inherits, deprecate, debuglog, debug,
 * isDeepStrictEqual, types.*, and the legacy is* checks.
 * Re-exports the global TextEncoder/TextDecoder (which rampart core
 * extends in rampart-textencoding.c to properly support latin1,
 * utf-16le/be, ascii, BOM/fatal, and encodeInto).
 * Deferred: parseArgs, parseEnv, styleText, MIMEType,
 * aborted/transferableAbort*.
 * ============================================================ */

static const char *util_js =
"function() {\n"
"  'use strict';\n"
"  var inspectCustom = '__nodeshim_util_inspect_custom__';\n"
"  var promisifyCustom = '__nodeshim_util_promisify_custom__';\n"
"\n"
"  function _inspect(v, opts, depth) {\n"
"    opts = opts || {};\n"
"    if (depth === undefined) depth = (opts.depth === null ? Infinity : (opts.depth !== undefined ? opts.depth : 2));\n"
"    if (v === null) return 'null';\n"
"    if (v === undefined) return 'undefined';\n"
"    if (typeof v === 'string') {\n"
"      return \"'\" + v.replace(/\\\\/g, '\\\\\\\\').replace(/'/g, \"\\\\'\") + \"'\";\n"
"    }\n"
"    if (typeof v === 'number') return v !== v ? 'NaN' : (v === Infinity ? 'Infinity' : v === -Infinity ? '-Infinity' : String(v));\n"
"    if (typeof v === 'boolean' || typeof v === 'bigint') return String(v);\n"
"    if (typeof v === 'function') return '[Function: ' + (v.name || 'anonymous') + ']';\n"
"    if (typeof v === 'symbol') { try { return v.toString(); } catch (e) { return 'Symbol(?)'; } }\n"
"    /* check custom inspect hook */\n"
"    try {\n"
"      if (v && typeof v[inspectCustom] === 'function') {\n"
"        var r = v[inspectCustom](depth, opts);\n"
"        return typeof r === 'string' ? r : _inspect(r, opts, depth);\n"
"      }\n"
"    } catch (e) { return '[object Object]'; }\n"
"    if (v instanceof RegExp) return v.toString();\n"
"    if (v instanceof Date) { try { return v.toISOString(); } catch (e) { return 'Invalid Date'; } }\n"
"    if (v instanceof Error) return v.stack || (v.name + ': ' + v.message);\n"
"    if (depth < 0) {\n"
"      if (Array.isArray(v)) return '[Array]';\n"
"      return '[Object]';\n"
"    }\n"
"    if (Array.isArray(v)) {\n"
"      if (v.length === 0) return '[]';\n"
"      var max = opts.maxArrayLength !== undefined ? opts.maxArrayLength : 100;\n"
"      var items = [];\n"
"      for (var i = 0; i < v.length && i < max; i++) items.push(_inspect(v[i], opts, depth - 1));\n"
"      if (v.length > max) items.push('... ' + (v.length - max) + ' more items');\n"
"      return '[ ' + items.join(', ') + ' ]';\n"
"    }\n"
"    if (typeof v === 'object') {\n"
"      try {\n"
"        var keys = Object.keys(v);\n"
"        var visible = [];\n"
"        for (var i = 0; i < keys.length; i++) if (keys[i] !== inspectCustom) visible.push(keys[i]);\n"
"        if (visible.length === 0) return '{}';\n"
"        var pairs = [];\n"
"        for (var j = 0; j < visible.length; j++) pairs.push(visible[j] + ': ' + _inspect(v[visible[j]], opts, depth - 1));\n"
"        return '{ ' + pairs.join(', ') + ' }';\n"
"      } catch (e) { return '[object Object]'; }\n"
"    }\n"
"    return String(v);\n"
"  }\n"
"\n"
"  function inspect(v, opts) {\n"
"    /* Old signature: inspect(obj, showHidden, depth, colors) */\n"
"    if (typeof opts === 'boolean') {\n"
"      var o = { showHidden: opts };\n"
"      if (arguments[2] !== undefined) o.depth = arguments[2];\n"
"      if (arguments[3] !== undefined) o.colors = arguments[3];\n"
"      opts = o;\n"
"    }\n"
"    return _inspect(v, opts || {});\n"
"  }\n"
"  inspect.custom = inspectCustom;\n"
"  inspect.defaultOptions = {\n"
"    showHidden: false, depth: 2, colors: false, customInspect: true, showProxy: false,\n"
"    maxArrayLength: 100, maxStringLength: 10000, breakLength: 80, compact: 3,\n"
"    sorted: false, getters: false, numericSeparator: false\n"
"  };\n"
"\n"
"  function format(f) {\n"
"    var args = [];\n"
"    for (var k = 1; k < arguments.length; k++) args.push(arguments[k]);\n"
"    if (typeof f !== 'string') {\n"
"      var parts = [];\n"
"      for (var k = 0; k < arguments.length; k++) {\n"
"        parts.push(typeof arguments[k] === 'string' ? arguments[k] : inspect(arguments[k]));\n"
"      }\n"
"      return parts.join(' ');\n"
"    }\n"
"    /* Per node spec: if no additional args, the format string is\n"
"       returned unchanged (no specifier processing — %%, %s, %d all\n"
"       stay literal).  Specifier substitution only happens when at\n"
"       least one arg is present. */\n"
"    if (args.length === 0) return f;\n"
"    var i = 0, str = '', j = 0;\n"
"    while (j < f.length) {\n"
"      if (f.charCodeAt(j) === 37 && j + 1 < f.length) {\n"
"        var spec = f.charAt(j + 1);\n"
"        if (spec === '%') { str += '%'; j += 2; continue; }\n"
"        if (i < args.length) {\n"
"          if (spec === 's') { str += (args[i] == null ? String(args[i]) : (typeof args[i] === 'string' ? args[i] : (typeof args[i] === 'object' ? inspect(args[i]) : String(args[i])))); i++; j += 2; continue; }\n"
"          if (spec === 'd') { str += String(Number(args[i])); i++; j += 2; continue; }\n"
"          if (spec === 'i') { str += String(parseInt(args[i], 10)); i++; j += 2; continue; }\n"
"          if (spec === 'f') { str += String(parseFloat(args[i])); i++; j += 2; continue; }\n"
"          if (spec === 'j') { try { str += JSON.stringify(args[i]); } catch (e) { str += '[Circular]'; } i++; j += 2; continue; }\n"
"          if (spec === 'o' || spec === 'O') { str += inspect(args[i]); i++; j += 2; continue; }\n"
"          if (spec === 'c') { i++; j += 2; continue; }\n"
"        }\n"
"      }\n"
"      str += f.charAt(j++);\n"
"    }\n"
"    for (; i < args.length; i++) {\n"
"      str += ' ' + (typeof args[i] === 'string' ? args[i] : inspect(args[i]));\n"
"    }\n"
"    return str;\n"
"  }\n"
"  function formatWithOptions(_opts) {\n"
"    var args = [];\n"
"    for (var k = 1; k < arguments.length; k++) args.push(arguments[k]);\n"
"    return format.apply(null, args);\n"
"  }\n"
"\n"
"  function promisify(original) {\n"
"    if (typeof original !== 'function') {\n"
"      var e = new TypeError('The \"original\" argument must be of type function.' + (original === null ? ' Received null' : ' Received ' + typeof original));\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"    if (original[promisifyCustom]) {\n"
"      var fn = original[promisifyCustom];\n"
"      if (typeof fn !== 'function') {\n"
"        var e = new TypeError('The \"util.promisify.custom\" property must be of type function');\n"
"        e.code = 'ERR_INVALID_ARG_TYPE';\n"
"        throw e;\n"
"      }\n"
"      return fn;\n"
"    }\n"
"    function wrapped() {\n"
"      var args = [];\n"
"      for (var i = 0; i < arguments.length; i++) args.push(arguments[i]);\n"
"      var self = this;\n"
"      return new Promise(function(resolve, reject) {\n"
"        args.push(function(err) {\n"
"          if (err) return reject(err);\n"
"          var rest = [];\n"
"          for (var k = 1; k < arguments.length; k++) rest.push(arguments[k]);\n"
"          if (rest.length <= 1) return resolve(rest[0]);\n"
"          resolve(rest);\n"
"        });\n"
"        try { original.apply(self, args); } catch (err) { reject(err); }\n"
"      });\n"
"    }\n"
"    try { Object.setPrototypeOf(wrapped, Object.getPrototypeOf(original)); } catch (e) {}\n"
"    /* Preserve original's name and arity (minus the callback arg) */\n"
"    try { Object.defineProperty(wrapped, 'name', { value: original.name, configurable: true }); } catch (e) {}\n"
"    try { Object.defineProperty(wrapped, 'length', { value: Math.max(original.length - 1, 0), configurable: true }); } catch (e) {}\n"
"    return wrapped;\n"
"  }\n"
"  promisify.custom = promisifyCustom;\n"
"\n"
"  function callbackify(original) {\n"
"    if (typeof original !== 'function') {\n"
"      var e = new TypeError('The \"original\" argument must be of type function.' + (original === null ? ' Received null' : ' Received ' + typeof original));\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"    function wrapped() {\n"
"      var args = [];\n"
"      for (var i = 0; i < arguments.length - 1; i++) args.push(arguments[i]);\n"
"      var cb = arguments[arguments.length - 1];\n"
"      if (typeof cb !== 'function') {\n"
"        var e = new TypeError('The last argument must be of type function');\n"
"        e.code = 'ERR_INVALID_ARG_TYPE';\n"
"        throw e;\n"
"      }\n"
"      var self = this;\n"
"      try {\n"
"        var result = original.apply(self, args);\n"
"        Promise.resolve(result).then(\n"
"          function(value) { cb.call(self, null, value); },\n"
"          function(err) {\n"
"            if (!err) {\n"
"              var rejValue = err;\n"
"              err = new Error('Promise was rejected with falsy value');\n"
"              err.code = 'ERR_FALSY_VALUE_REJECTION';\n"
"              err.reason = rejValue;\n"
"            }\n"
"            cb.call(self, err);\n"
"          }\n"
"        );\n"
"      } catch (err) { cb.call(self, err); }\n"
"    }\n"
"    /* Don't copy original's prototype — node's callbackify leaves it as\n"
"       Function.prototype, so callbackify(asyncFn) doesn't get the\n"
"       AsyncFunction prototype. */\n"
"    try { Object.defineProperty(wrapped, 'name', { value: original.name + 'Callbackified', configurable: true }); } catch (e) {}\n"
"    try { Object.defineProperty(wrapped, 'length', { value: original.length + 1, configurable: true }); } catch (e) {}\n"
"    return wrapped;\n"
"  }\n"
"\n"
"  function _typeOfDescr(v) {\n"
"    if (v === null) return 'null';\n"
"    return typeof v;\n"
"  }\n"
"  function _argTypeErr(argName, expectedType, value) {\n"
"    var rec;\n"
"    if (value === null) rec = ' Received null';\n"
"    else if (value === undefined) rec = ' Received undefined';\n"
"    else if (typeof value === 'function') rec = ' Received function ' + (value.name || '');\n"
"    else if (typeof value === 'object') {\n"
"      var c = value.constructor && value.constructor.name;\n"
"      rec = c ? ' Received an instance of ' + c : ' Received [object Object]';\n"
"    } else {\n"
"      var s = _inspect(value);\n"
"      if (s.length > 28) s = s.slice(0, 25) + '...';\n"
"      rec = ' Received type ' + typeof value + ' (' + s + ')';\n"
"    }\n"
"    var e = new TypeError('The \"' + argName + '\" ' +\n"
"        (argName.indexOf('.') >= 0 ? 'property' : 'argument') +\n"
"        ' must be of type ' + expectedType + '.' + rec);\n"
"    e.code = 'ERR_INVALID_ARG_TYPE';\n"
"    return e;\n"
"  }\n"
"  function inherits(ctor, superCtor) {\n"
"    if (typeof ctor !== 'function') throw _argTypeErr('ctor', 'function', ctor);\n"
"    if (superCtor === null || superCtor === undefined)\n"
"      throw _argTypeErr('superCtor', 'function', superCtor);\n"
"    if (typeof superCtor.prototype !== 'object')\n"
"      throw _argTypeErr('superCtor.prototype', 'object', superCtor.prototype);\n"
"    Object.defineProperty(ctor, 'super_', {\n"
"      value: superCtor, enumerable: false, configurable: true, writable: true\n"
"    });\n"
"    Object.setPrototypeOf(ctor.prototype, superCtor.prototype);\n"
"  }\n"
"\n"
"  function deprecate(fn, msg, code) {\n"
"    var warned = false;\n"
"    function deprecated() {\n"
"      if (!warned) {\n"
"        warned = true;\n"
"        if (typeof console !== 'undefined' && console.warn) {\n"
"          console.warn('DeprecationWarning: ' + msg + (code ? ' [' + code + ']' : ''));\n"
"        }\n"
"      }\n"
"      return fn.apply(this, arguments);\n"
"    }\n"
"    return deprecated;\n"
"  }\n"
"\n"
"  function debuglog(set) {\n"
"    var enabled = false;\n"
"    try {\n"
"      var env = (typeof process !== 'undefined' && process.env) ? process.env : null;\n"
"      if (env && env.NODE_DEBUG) {\n"
"        var dbg = env.NODE_DEBUG.toLowerCase().split(/[,\\s]+/);\n"
"        enabled = dbg.indexOf(set.toLowerCase()) !== -1;\n"
"      }\n"
"    } catch (e) {}\n"
"    if (!enabled) return function() {};\n"
"    return function() {\n"
"      var msg = format.apply(null, arguments);\n"
"      var pid = (typeof process !== 'undefined' && process.pid) ? process.pid : 0;\n"
"      console.error(set.toUpperCase() + ' ' + pid + ': ' + msg);\n"
"    };\n"
"  }\n"
"\n"
"  function _deepEq(a, b, strict, seen) {\n"
"    if (strict ? a === b : a == b) return true;\n"
"    if (a === null || b === null || a === undefined || b === undefined) return false;\n"
"    if (typeof a !== 'object' || typeof b !== 'object') {\n"
"      if (strict) return false;\n"
"      return a == b;\n"
"    }\n"
"    if (strict && Object.getPrototypeOf(a) !== Object.getPrototypeOf(b)) return false;\n"
"    if (a instanceof Date) return b instanceof Date && a.getTime() === b.getTime();\n"
"    if (a instanceof RegExp) return b instanceof RegExp && a.toString() === b.toString();\n"
"    if (Array.isArray(a)) {\n"
"      if (!Array.isArray(b) || a.length !== b.length) return false;\n"
"      for (var i = 0; i < a.length; i++) if (!_deepEq(a[i], b[i], strict, seen)) return false;\n"
"      return true;\n"
"    }\n"
"    for (var i = 0; i < seen.length; i++) if (seen[i] === a) return true;\n"
"    seen = seen.concat([a]);\n"
"    var ak = Object.keys(a), bk = Object.keys(b);\n"
"    if (ak.length !== bk.length) return false;\n"
"    ak.sort(); bk.sort();\n"
"    for (var i = 0; i < ak.length; i++) {\n"
"      if (ak[i] !== bk[i]) return false;\n"
"      if (!_deepEq(a[ak[i]], b[bk[i]], strict, seen)) return false;\n"
"    }\n"
"    return true;\n"
"  }\n"
"  function isDeepStrictEqual(a, b) { return _deepEq(a, b, true, []); }\n"
"\n"
"  function _safeInstance(v, ctorName) {\n"
"    try { var C = (typeof eval(ctorName) === 'function') ? eval(ctorName) : null; return C ? v instanceof C : false; } catch (e) { return false; }\n"
"  }\n"
"  var types = {\n"
"    isArray: Array.isArray,\n"
"    isDate: function(v) { return v instanceof Date; },\n"
"    isMap: function(v) { return _safeInstance(v, 'Map'); },\n"
"    isSet: function(v) { return _safeInstance(v, 'Set'); },\n"
"    isWeakMap: function(v) { return _safeInstance(v, 'WeakMap'); },\n"
"    isWeakSet: function(v) { return _safeInstance(v, 'WeakSet'); },\n"
"    isPromise: function(v) { return v != null && typeof v.then === 'function' && typeof v['catch'] === 'function'; },\n"
"    isRegExp: function(v) { return v instanceof RegExp; },\n"
"    isNativeError: function(v) { return v instanceof Error; },\n"
"    isAsyncFunction: function(v) { return typeof v === 'function' && /^\\s*async/.test(Function.prototype.toString.call(v)); },\n"
"    isGeneratorFunction: function(v) { return typeof v === 'function' && /^\\s*function\\s*\\*/.test(Function.prototype.toString.call(v)); },\n"
"    isGeneratorObject: function(v) {\n"
"      return v != null && typeof v === 'object' && typeof v.next === 'function' && typeof v['return'] === 'function' && typeof v['throw'] === 'function';\n"
"    },\n"
"    isArrayBuffer: function(v) { return _safeInstance(v, 'ArrayBuffer'); },\n"
"    isSharedArrayBuffer: function(v) { return _safeInstance(v, 'SharedArrayBuffer'); },\n"
"    isAnyArrayBuffer: function(v) { return _safeInstance(v, 'ArrayBuffer') || _safeInstance(v, 'SharedArrayBuffer'); },\n"
"    isDataView: function(v) { return _safeInstance(v, 'DataView'); },\n"
"    isArrayBufferView: function(v) { try { return ArrayBuffer.isView(v); } catch(e) { return false; } },\n"
"    isTypedArray: function(v) { try { return ArrayBuffer.isView(v) && !_safeInstance(v, 'DataView'); } catch(e) { return false; } },\n"
"    isUint8Array: function(v) { return _safeInstance(v, 'Uint8Array'); },\n"
"    isUint8ClampedArray: function(v) { return _safeInstance(v, 'Uint8ClampedArray'); },\n"
"    isUint16Array: function(v) { return _safeInstance(v, 'Uint16Array'); },\n"
"    isUint32Array: function(v) { return _safeInstance(v, 'Uint32Array'); },\n"
"    isInt8Array: function(v) { return _safeInstance(v, 'Int8Array'); },\n"
"    isInt16Array: function(v) { return _safeInstance(v, 'Int16Array'); },\n"
"    isInt32Array: function(v) { return _safeInstance(v, 'Int32Array'); },\n"
"    isFloat32Array: function(v) { return _safeInstance(v, 'Float32Array'); },\n"
"    isFloat64Array: function(v) { return _safeInstance(v, 'Float64Array'); },\n"
"    isBigInt64Array: function(v) { return _safeInstance(v, 'BigInt64Array'); },\n"
"    isBigUint64Array: function(v) { return _safeInstance(v, 'BigUint64Array'); },\n"
"    isWeakRef: function() { return false; },\n"
"    isProxy: function() { return false; },\n"
"    isModuleNamespaceObject: function() { return false; },\n"
"    isExternal: function() { return false; },\n"
"    isCryptoKey: function() { return false; },\n"
"    isKeyObject: function() { return false; },\n"
"    isArgumentsObject: function(v) {\n"
"      return v != null && typeof v === 'object' && Object.prototype.toString.call(v) === '[object Arguments]';\n"
"    },\n"
"    isBoxedPrimitive: function(v) {\n"
"      if (v == null || typeof v !== 'object') return false;\n"
"      return v instanceof Number || v instanceof String || v instanceof Boolean;\n"
"    },\n"
"    isNumberObject: function(v) { return v instanceof Number; },\n"
"    isStringObject: function(v) { return v instanceof String; },\n"
"    isBooleanObject: function(v) { return v instanceof Boolean; },\n"
"    isSymbolObject: function(v) { return typeof v === 'object' && Object.prototype.toString.call(v) === '[object Symbol]'; }\n"
"  };\n"
"\n"
"  /* Legacy (deprecated in node but still present) */\n"
"  function isArray(v) { return Array.isArray(v); }\n"
"  function isBoolean(v) { return typeof v === 'boolean'; }\n"
"  function isBuffer(v) { return v != null && typeof v === 'object' && typeof v.copy === 'function' && typeof v.writeUInt8 === 'function'; }\n"
"  function isNull(v) { return v === null; }\n"
"  function isUndefined(v) { return v === undefined; }\n"
"  function isNullOrUndefined(v) { return v == null; }\n"
"  function isNumber(v) { return typeof v === 'number'; }\n"
"  function isString(v) { return typeof v === 'string'; }\n"
"  function isSymbol(v) { return typeof v === 'symbol'; }\n"
"  function isObject(v) { return v !== null && typeof v === 'object'; }\n"
"  function isFunction(v) { return typeof v === 'function'; }\n"
"  function isPrimitive(v) { return v === null || (typeof v !== 'object' && typeof v !== 'function'); }\n"
"  function isRegExp(v) { return v instanceof RegExp; }\n"
"  function isDate(v) { return v instanceof Date; }\n"
"  function isError(v) { return v instanceof Error; }\n"
"\n"
"  /* ----- stripVTControlCharacters: removes ANSI escape sequences ----- */\n"
"  /* Matches CSI/OSC/ESC sequences.  Same regex node uses. */\n"
"  var _ansiRe = /[\\u001b\\u009b][[\\]()#;?]*(?:(?:(?:[a-zA-Z0-9]*(?:;[a-zA-Z0-9]*)*)?\\u0007)|(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[0-9A-PR-TZcf-nq-uy=><~]))/g;\n"
"  function stripVTControlCharacters(str) {\n"
"    if (typeof str !== 'string')\n"
"      throw new TypeError('stripVTControlCharacters: argument must be a string');\n"
"    return str.replace(_ansiRe, '');\n"
"  }\n"
"\n"
"  /* ----- errno table for getSystemErrorName / getSystemErrorMap ----- */\n"
"  /* Linux errno -> symbolic name.  Negatives are node's libuv convention. */\n"
"  var _errnoTable = {\n"
"    '1': 'EPERM', '2': 'ENOENT', '3': 'ESRCH', '4': 'EINTR', '5': 'EIO',\n"
"    '6': 'ENXIO', '7': 'E2BIG', '8': 'ENOEXEC', '9': 'EBADF', '10': 'ECHILD',\n"
"    '11': 'EAGAIN', '12': 'ENOMEM', '13': 'EACCES', '14': 'EFAULT',\n"
"    '15': 'ENOTBLK', '16': 'EBUSY', '17': 'EEXIST', '18': 'EXDEV',\n"
"    '19': 'ENODEV', '20': 'ENOTDIR', '21': 'EISDIR', '22': 'EINVAL',\n"
"    '23': 'ENFILE', '24': 'EMFILE', '25': 'ENOTTY', '26': 'ETXTBSY',\n"
"    '27': 'EFBIG', '28': 'ENOSPC', '29': 'ESPIPE', '30': 'EROFS',\n"
"    '31': 'EMLINK', '32': 'EPIPE', '33': 'EDOM',  '34': 'ERANGE',\n"
"    '35': 'EDEADLK','36': 'ENAMETOOLONG', '37': 'ENOLCK', '38': 'ENOSYS',\n"
"    '39': 'ENOTEMPTY','40': 'ELOOP', '42': 'ENOMSG', '43': 'EIDRM',\n"
"    '95': 'EOPNOTSUPP','97': 'EAFNOSUPPORT','98': 'EADDRINUSE',\n"
"    '99': 'EADDRNOTAVAIL','100': 'ENETDOWN','101': 'ENETUNREACH',\n"
"    '102': 'ENETRESET','103': 'ECONNABORTED','104': 'ECONNRESET',\n"
"    '105': 'ENOBUFS','106': 'EISCONN','107': 'ENOTCONN','108': 'ESHUTDOWN',\n"
"    '110': 'ETIMEDOUT','111': 'ECONNREFUSED','112': 'EHOSTDOWN',\n"
"    '113': 'EHOSTUNREACH','114': 'EALREADY','115': 'EINPROGRESS'\n"
"  };\n"
"  function getSystemErrorName(errno) {\n"
"    /* Node accepts either signed (libuv) or unsigned errnos.  Try absolute. */\n"
"    var key = String(Math.abs(errno | 0));\n"
"    return _errnoTable[key] || ('Unknown system errno ' + errno);\n"
"  }\n"
"  function getSystemErrorMap() {\n"
"    var M;\n"
"    try { M = new Map(); } catch (e) { M = {}; M._isPlain = true; }\n"
"    var has = !M._isPlain;\n"
"    for (var k in _errnoTable) {\n"
"      var n = parseInt(k, 10);\n"
"      if (has) M.set(n, [_errnoTable[k], _errnoTable[k]]);\n"
"      else      M[n]   = [_errnoTable[k], _errnoTable[k]];\n"
"    }\n"
"    return M;\n"
"  }\n"
"\n"
"  /* ----- styleText(format, str): ANSI color helper (node 22+) ----- */\n"
"  /* `format` may be a string or array of strings; values map to SGR codes. */\n"
"  var _sgr = {\n"
"    reset: '0', bold: '1', dim: '2', italic: '3', underline: '4',\n"
"    blink: '5', inverse: '7', hidden: '8', strikethrough: '9',\n"
"    black: '30', red: '31', green: '32', yellow: '33', blue: '34',\n"
"    magenta: '35', cyan: '36', white: '37', gray: '90', grey: '90',\n"
"    blackBright: '90', redBright: '91', greenBright: '92', yellowBright: '93',\n"
"    blueBright: '94', magentaBright: '95', cyanBright: '96', whiteBright: '97',\n"
"    bgBlack: '40', bgRed: '41', bgGreen: '42', bgYellow: '43',\n"
"    bgBlue: '44', bgMagenta: '45', bgCyan: '46', bgWhite: '47'\n"
"  };\n"
"  function styleText(format, str) {\n"
"    if (typeof str !== 'string')\n"
"      throw new TypeError('styleText: second argument must be a string');\n"
"    var list = Array.isArray(format) ? format : [format];\n"
"    var codes = [];\n"
"    for (var i = 0; i < list.length; i++) {\n"
"      var c = _sgr[list[i]];\n"
"      if (!c) {\n"
"        var e = new TypeError('styleText: unknown format \"' + list[i] + '\"');\n"
"        e.code = 'ERR_INVALID_ARG_VALUE'; throw e;\n"
"      }\n"
"      codes.push(c);\n"
"    }\n"
"    return '\\u001b[' + codes.join(';') + 'm' + str + '\\u001b[0m';\n"
"  }\n"
"\n"
"  /* ----- parseArgs (node 18+) ----- */\n"
"  /* Minimal node-compatible implementation supporting:\n"
"     - options.options: {flag: {type:'boolean'|'string', short, multiple,\n"
"                                default}}\n"
"     - options.args (default process.argv.slice(2))\n"
"     - options.allowPositionals (default false)\n"
"     - options.strict (default true)\n"
"     - options.tokens (default false) -- we ignore for now */\n"
"  function parseArgs(config) {\n"
"    config = config || {};\n"
"    var optDefs = config.options || {};\n"
"    var allowPos = !!config.allowPositionals;\n"
"    var strict = (config.strict !== false);\n"
"    var args = config.args;\n"
"    if (!args) {\n"
"      try { args = require('rampart-nodeshim').process.argv.slice(2); }\n"
"      catch (e) { args = []; }\n"
"    }\n"
"    var values = {}, positionals = [];\n"
"    /* Seed defaults */\n"
"    for (var n in optDefs) {\n"
"      if (optDefs[n].default !== undefined) values[n] = optDefs[n].default;\n"
"    }\n"
"    function shortToLong(s) {\n"
"      for (var n in optDefs) if (optDefs[n].short === s) return n;\n"
"      return null;\n"
"    }\n"
"    function setVal(name, val) {\n"
"      var def = optDefs[name];\n"
"      if (def && def.multiple) {\n"
"        if (!Array.isArray(values[name])) values[name] = [];\n"
"        values[name].push(val);\n"
"      } else {\n"
"        values[name] = val;\n"
"      }\n"
"    }\n"
"    for (var i = 0; i < args.length; i++) {\n"
"      var a = args[i];\n"
"      if (a === '--') { positionals.push.apply(positionals, args.slice(i+1)); break; }\n"
"      if (a.length > 2 && a.slice(0, 2) === '--') {\n"
"        var eq = a.indexOf('=');\n"
"        var nm = (eq < 0) ? a.slice(2) : a.slice(2, eq);\n"
"        var def = optDefs[nm];\n"
"        if (!def) {\n"
"          if (strict) { var e = new TypeError('Unknown option --' + nm); e.code='ERR_PARSE_ARGS_UNKNOWN_OPTION'; throw e; }\n"
"          continue;\n"
"        }\n"
"        if (def.type === 'boolean') { setVal(nm, true); }\n"
"        else { setVal(nm, eq < 0 ? args[++i] : a.slice(eq+1)); }\n"
"      } else if (a.length > 1 && a.charAt(0) === '-' && a !== '-') {\n"
"        var s = a.slice(1);\n"
"        var lookup = shortToLong(s);\n"
"        if (!lookup) {\n"
"          if (strict) { var e = new TypeError('Unknown short option -' + s); e.code='ERR_PARSE_ARGS_UNKNOWN_OPTION'; throw e; }\n"
"          continue;\n"
"        }\n"
"        if (optDefs[lookup].type === 'boolean') setVal(lookup, true);\n"
"        else setVal(lookup, args[++i]);\n"
"      } else {\n"
"        if (allowPos) positionals.push(a);\n"
"        else if (strict) { var e = new TypeError('Unexpected positional: ' + a); e.code='ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL'; throw e; }\n"
"      }\n"
"    }\n"
"    return {values: values, positionals: positionals};\n"
"  }\n"
"\n"
"  /* ----- parseEnv (node 21+): parse .env-style content -> object ----- */\n"
"  function parseEnv(content) {\n"
"    if (typeof content !== 'string')\n"
"      throw new TypeError('parseEnv: content must be a string');\n"
"    var out = {};\n"
"    var lines = content.split(/\\r?\\n/);\n"
"    for (var i = 0; i < lines.length; i++) {\n"
"      var ln = lines[i].replace(/^\\s+/, '');\n"
"      if (!ln || ln.charAt(0) === '#') continue;\n"
"      var eq = ln.indexOf('=');\n"
"      if (eq < 0) continue;\n"
"      var key = ln.slice(0, eq).replace(/\\s+$/, '');\n"
"      var raw = ln.slice(eq + 1).replace(/^\\s+/, '');\n"
"      /* Strip surrounding single/double quotes; honor backslash escapes\n"
"         inside double quotes per dotenv convention. */\n"
"      var val;\n"
"      if ((raw.charAt(0) === '\"' && raw.charAt(raw.length-1) === '\"') ||\n"
"          (raw.charAt(0) === \"'\" && raw.charAt(raw.length-1) === \"'\")) {\n"
"        val = raw.slice(1, -1);\n"
"        if (raw.charAt(0) === '\"')\n"
"          val = val.replace(/\\\\n/g, '\\n').replace(/\\\\r/g, '\\r').replace(/\\\\\"/g, '\"');\n"
"      } else {\n"
"        /* Strip inline comments after a space-hash. */\n"
"        var hash = raw.indexOf(' #');\n"
"        val = (hash < 0 ? raw : raw.slice(0, hash)).replace(/\\s+$/, '');\n"
"      }\n"
"      out[key] = val;\n"
"    }\n"
"    return out;\n"
"  }\n"
"\n"
"  /* ----- toUSVString(str): replace lone surrogates with U+FFFD ----- */\n"
"  /* JS strings are UTF-16 sequences that may contain unpaired\n"
"     surrogate halves.  toUSVString scrubs them to U+FFFD so the\n"
"     output is a valid Unicode scalar value sequence (no orphan\n"
"     surrogates).  Used as a normalization helper before passing\n"
"     strings to APIs that require well-formed Unicode. */\n"
"  function toUSVString(str) {\n"
"    if (typeof str !== 'string') str = String(str);\n"
"    var out = '', i = 0, n = str.length;\n"
"    while (i < n) {\n"
"      var c = str.charCodeAt(i);\n"
"      if (c >= 0xD800 && c <= 0xDBFF) {\n"
"        /* High surrogate -- must be followed by a low surrogate. */\n"
"        if (i + 1 < n) {\n"
"          var c2 = str.charCodeAt(i + 1);\n"
"          if (c2 >= 0xDC00 && c2 <= 0xDFFF) {\n"
"            out += str.charAt(i) + str.charAt(i + 1);\n"
"            i += 2; continue;\n"
"          }\n"
"        }\n"
"        out += '\\uFFFD'; i++; continue;\n"
"      }\n"
"      if (c >= 0xDC00 && c <= 0xDFFF) {\n"
"        /* Orphan low surrogate */\n"
"        out += '\\uFFFD'; i++; continue;\n"
"      }\n"
"      out += str.charAt(i); i++;\n"
"    }\n"
"    return out;\n"
"  }\n"
"\n"
"  /* ----- MIMEType / MIMEParams (WHATWG MIME Sniffing) ----- */\n"
"  /* HTTP-token chars per RFC 7230. */\n"
"  function _isToken(s) {\n"
"    if (typeof s !== 'string' || s.length === 0) return false;\n"
"    return /^[!#$%&'*+\\-.\\^_`|~0-9A-Za-z]+$/.test(s);\n"
"  }\n"
"\n"
"  /* MIMEParams -- iterable parameter store with Map-like semantics.\n"
"     Iteration order: insertion order, lowercased keys. */\n"
"  function MIMEParams() {\n"
"    /* Internal store: array of [name, value] pairs to preserve order. */\n"
"    Object.defineProperty(this, '_list', {value: [], writable: false});\n"
"  }\n"
"  MIMEParams.prototype.get = function(name) {\n"
"    name = String(name).toLowerCase();\n"
"    for (var i = 0; i < this._list.length; i++)\n"
"      if (this._list[i][0] === name) return this._list[i][1];\n"
"    return null;\n"
"  };\n"
"  MIMEParams.prototype.has = function(name) {\n"
"    name = String(name).toLowerCase();\n"
"    for (var i = 0; i < this._list.length; i++)\n"
"      if (this._list[i][0] === name) return true;\n"
"    return false;\n"
"  };\n"
"  MIMEParams.prototype.set = function(name, value) {\n"
"    name = String(name).toLowerCase();\n"
"    if (!_isToken(name))\n"
"      throw new TypeError('MIMEParams.set: invalid parameter name');\n"
"    value = String(value);\n"
"    for (var i = 0; i < this._list.length; i++) {\n"
"      if (this._list[i][0] === name) { this._list[i][1] = value; return; }\n"
"    }\n"
"    this._list.push([name, value]);\n"
"  };\n"
"  MIMEParams.prototype.delete = function(name) {\n"
"    name = String(name).toLowerCase();\n"
"    for (var i = 0; i < this._list.length; i++) {\n"
"      if (this._list[i][0] === name) { this._list.splice(i, 1); return; }\n"
"    }\n"
"  };\n"
"  MIMEParams.prototype.entries = function() {\n"
"    var self = this, i = 0;\n"
"    var iter = {\n"
"      next: function() {\n"
"        if (i < self._list.length) {\n"
"          var e = self._list[i++];\n"
"          return {value: [e[0], e[1]], done: false};\n"
"        }\n"
"        return {value: undefined, done: true};\n"
"      }\n"
"    };\n"
"    if (typeof Symbol !== 'undefined' && Symbol.iterator)\n"
"      iter[Symbol.iterator] = function() { return this; };\n"
"    return iter;\n"
"  };\n"
"  MIMEParams.prototype.keys = function() {\n"
"    var src = this.entries();\n"
"    return _wrapIter(src, function(e) { return e[0]; });\n"
"  };\n"
"  MIMEParams.prototype.values = function() {\n"
"    var src = this.entries();\n"
"    return _wrapIter(src, function(e) { return e[1]; });\n"
"  };\n"
"  function _wrapIter(src, fn) {\n"
"    var iter = {\n"
"      next: function() {\n"
"        var s = src.next();\n"
"        if (s.done) return s;\n"
"        return {value: fn(s.value), done: false};\n"
"      }\n"
"    };\n"
"    if (typeof Symbol !== 'undefined' && Symbol.iterator)\n"
"      iter[Symbol.iterator] = function() { return this; };\n"
"    return iter;\n"
"  }\n"
"  if (typeof Symbol !== 'undefined' && Symbol.iterator) {\n"
"    MIMEParams.prototype[Symbol.iterator] = MIMEParams.prototype.entries;\n"
"  }\n"
"  MIMEParams.prototype.toString = function() {\n"
"    var out = '';\n"
"    for (var i = 0; i < this._list.length; i++) {\n"
"      var k = this._list[i][0], v = this._list[i][1];\n"
"      /* Quote values that contain non-token chars. */\n"
"      var qv = _isToken(v) ? v : '\"' + v.replace(/\"/g, '\\\\\"') + '\"';\n"
"      out += (i ? ';' : '') + k + '=' + qv;\n"
"    }\n"
"    return out;\n"
"  };\n"
"\n"
"  /* MIMEType -- parses input like 'text/html; charset=utf-8'. */\n"
"  function MIMEType(input) {\n"
"    if (!(this instanceof MIMEType)) return new MIMEType(input);\n"
"    var s = String(input).replace(/^[\\s\\t\\n\\r\\f]+|[\\s\\t\\n\\r\\f]+$/g, '');\n"
"    var slash = s.indexOf('/');\n"
"    if (slash < 0) throw new TypeError('Invalid MIME type: missing /');\n"
"    var type = s.slice(0, slash).toLowerCase();\n"
"    if (!_isToken(type)) throw new TypeError('Invalid MIME type: bad type');\n"
"    var rest = s.slice(slash + 1);\n"
"    var semi = rest.indexOf(';');\n"
"    var subtype = (semi < 0 ? rest : rest.slice(0, semi))\n"
"                  .replace(/[\\s\\t\\n\\r\\f]+$/, '').toLowerCase();\n"
"    if (!_isToken(subtype)) throw new TypeError('Invalid MIME type: bad subtype');\n"
"    var params = new MIMEParams();\n"
"    if (semi >= 0) {\n"
"      var paramStr = rest.slice(semi + 1);\n"
"      var i = 0;\n"
"      while (i < paramStr.length) {\n"
"        /* Skip whitespace then read name */\n"
"        while (i < paramStr.length && /[\\s\\t\\n\\r\\f;]/.test(paramStr.charAt(i))) i++;\n"
"        if (i >= paramStr.length) break;\n"
"        var nameStart = i;\n"
"        while (i < paramStr.length && paramStr.charAt(i) !== '=' && paramStr.charAt(i) !== ';') i++;\n"
"        var name = paramStr.slice(nameStart, i).replace(/[\\s\\t\\n\\r\\f]+$/, '').toLowerCase();\n"
"        if (paramStr.charAt(i) !== '=') { i++; continue; }\n"
"        i++;  /* consume = */\n"
"        var value;\n"
"        if (paramStr.charAt(i) === '\"') {\n"
"          i++; var v = '';\n"
"          while (i < paramStr.length && paramStr.charAt(i) !== '\"') {\n"
"            if (paramStr.charAt(i) === '\\\\' && i + 1 < paramStr.length) {\n"
"              v += paramStr.charAt(i + 1); i += 2;\n"
"            } else { v += paramStr.charAt(i); i++; }\n"
"          }\n"
"          if (paramStr.charAt(i) === '\"') i++;\n"
"          /* Skip up to next ; */\n"
"          while (i < paramStr.length && paramStr.charAt(i) !== ';') i++;\n"
"          value = v;\n"
"        } else {\n"
"          var valStart = i;\n"
"          while (i < paramStr.length && paramStr.charAt(i) !== ';') i++;\n"
"          value = paramStr.slice(valStart, i).replace(/[\\s\\t\\n\\r\\f]+$/, '');\n"
"        }\n"
"        if (name && _isToken(name) && !params.has(name)) params.set(name, value);\n"
"      }\n"
"    }\n"
"    Object.defineProperty(this, 'type',    {get: function(){ return type; },    set: function(v){\n"
"      v = String(v).toLowerCase();\n"
"      if (!_isToken(v)) throw new TypeError('Invalid MIME type');\n"
"      type = v;\n"
"    }, enumerable: true});\n"
"    Object.defineProperty(this, 'subtype', {get: function(){ return subtype; }, set: function(v){\n"
"      v = String(v).toLowerCase();\n"
"      if (!_isToken(v)) throw new TypeError('Invalid MIME subtype');\n"
"      subtype = v;\n"
"    }, enumerable: true});\n"
"    Object.defineProperty(this, 'essence', {get: function(){ return type + '/' + subtype; }, enumerable: true});\n"
"    Object.defineProperty(this, 'params',  {value: params, enumerable: true});\n"
"  }\n"
"  MIMEType.prototype.toString = function() {\n"
"    var s = this.type + '/' + this.subtype;\n"
"    var p = this.params.toString();\n"
"    return p ? s + ';' + p : s;\n"
"  };\n"
"  MIMEType.prototype.toJSON = MIMEType.prototype.toString;\n"
"\n"
"  /* ----- AbortSignal-related helpers -----\n"
"     util.aborted(signal, resource): Promise<void> that resolves when the\n"
"       signal aborts.  Node's spec holds `resource` weakly so the promise\n"
"       can be GC'd if resource is collected before abort; duktape has no\n"
"       weak refs, so we hold strongly — promise resolves only on abort.\n"
"     util.transferableAbortController() / util.transferableAbortSignal(signal):\n"
"       Node makes these transferable across postMessage boundaries.  Our\n"
"       postMessage is deep-copy (no transferList plumbing); we return a\n"
"       plain AbortController/Signal which still survives the copy because\n"
"       deep-copy duplicates them.  Surface-compatible with node usage. */\n"
"  function aborted(signal, resource) {\n"
"    var P = (typeof Promise !== 'undefined') ? Promise\n"
"          : (typeof global !== 'undefined' && global.Promise);\n"
"    if (!P) throw new Error('util.aborted requires Promise');\n"
"    if (!signal || typeof signal.addEventListener !== 'function')\n"
"      return P.reject(new TypeError('util.aborted: signal must be an AbortSignal'));\n"
"    if (resource === undefined || resource === null)\n"
"      return P.reject(new TypeError('util.aborted: resource is required'));\n"
"    return new P(function(resolve) {\n"
"      if (signal.aborted) { resolve(); return; }\n"
"      signal.addEventListener('abort', function(){ resolve(); }, { once: true });\n"
"    });\n"
"  }\n"
"  function transferableAbortController() {\n"
"    if (typeof AbortController !== 'function')\n"
"      throw new Error('util.transferableAbortController: AbortController not available');\n"
"    return new AbortController();\n"
"  }\n"
"  function transferableAbortSignal(signal) {\n"
"    if (!signal || typeof signal.addEventListener !== 'function')\n"
"      throw new TypeError('util.transferableAbortSignal: signal must be an AbortSignal');\n"
"    return signal;\n"
"  }\n"
"\n"
"  return {\n"
"    format: format,\n"
"    formatWithOptions: formatWithOptions,\n"
"    inspect: inspect,\n"
"    promisify: promisify,\n"
"    callbackify: callbackify,\n"
"    inherits: inherits,\n"
"    deprecate: deprecate,\n"
"    debuglog: debuglog,\n"
"    debug: debuglog,\n"
"    isDeepStrictEqual: isDeepStrictEqual,\n"
"    types: types,\n"
"    isArray: isArray, isBoolean: isBoolean, isBuffer: isBuffer,\n"
"    isNull: isNull, isUndefined: isUndefined, isNullOrUndefined: isNullOrUndefined,\n"
"    isNumber: isNumber, isString: isString, isSymbol: isSymbol,\n"
"    isObject: isObject, isFunction: isFunction, isPrimitive: isPrimitive,\n"
"    isRegExp: isRegExp, isDate: isDate, isError: isError,\n"
"    /* Spec-compliant globals (extended by rampart-textencoding.c) */\n"
"    TextEncoder: TextEncoder, TextDecoder: TextDecoder,\n"
"    /* New additions */\n"
"    stripVTControlCharacters: stripVTControlCharacters,\n"
"    getSystemErrorName: getSystemErrorName,\n"
"    getSystemErrorMap: getSystemErrorMap,\n"
"    styleText: styleText,\n"
"    parseArgs: parseArgs,\n"
"    parseEnv: parseEnv,\n"
"    toUSVString: toUSVString,\n"
"    MIMEType: MIMEType,\n"
"    MIMEParams: MIMEParams,\n"
"    aborted: aborted,\n"
"    transferableAbortController: transferableAbortController,\n"
"    transferableAbortSignal: transferableAbortSignal\n"
"  };\n"
"}";

static void nodeshim_init_util(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:util_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, util_js);
    duk_call(ctx, 0);
}
/* ============================================================
 * querystring — pure JS, small. parse/stringify with custom
 * sep+eq, plus escape/unescape and encode/decode aliases.
 * ============================================================ */

static const char *qs_js =
"function() {\n"
"  'use strict';\n"
"  /* Fast path uses rampart.utils.queryToObject/objectToQuery (~16x and ~4x\n"
"     speedups over pure JS). Semantic deltas vs node:\n"
"       - rampart parse: bare key returns `true`; we coerce to ''.\n"
"       - rampart parse: '+' is decoded as space; we pre-escape '+' to '%2B'\n"
"         so it round-trips as literal '+' (matches node querystring, not\n"
"         URLSearchParams).\n"
"       - rampart stringify: space → '+', we post-replace to '%20'.\n"
"       - rampart stringify: literal '+' → '%2b' (lowercase); we uppercase.\n"
"       - rampart stringify: null/undefined values literal-stringified; we\n"
"         pre-convert to ''.\n"
"       - rampart: no custom sep/eq; fall back to JS for those.\n"
"     The JS slow path is kept for custom-sep callers and as a no-rampart\n"
"     fallback. */\n"
"  var native = (typeof rampart !== 'undefined' && rampart && rampart.utils &&\n"
"                typeof rampart.utils.queryToObject === 'function' &&\n"
"                typeof rampart.utils.objectToQuery === 'function')\n"
"               ? rampart.utils : null;\n"
"\n"
"  function qsEscape(s) {\n"
"    try { return encodeURIComponent(String(s)); }\n"
"    catch (e) {\n"
"      if (e instanceof URIError) {\n"
"        var ne = new URIError('URI malformed');\n"
"        ne.code = 'ERR_INVALID_URI';\n"
"        throw ne;\n"
"      }\n"
"      throw e;\n"
"    }\n"
"  }\n"
"  function qsUnescape(s) {\n"
"    try { return decodeURIComponent(String(s)); }\n"
"    catch (e) { return String(s); }\n"
"  }\n"
"\n"
"  function stringifyPrimitive(v) {\n"
"    if (typeof v === 'string') return v;\n"
"    if (typeof v === 'number' && isFinite(v)) return String(v);\n"
"    if (typeof v === 'bigint') return String(v);\n"
"    if (typeof v === 'boolean') return v ? 'true' : 'false';\n"
"    return '';\n"
"  }\n"
"\n"
"  /* Uppercase any %XX percent-encoding emitted as lowercase by rampart */\n"
"  function _normalizeHex(s) {\n"
"    return s.replace(/%[0-9a-f]{2}/g, function(m) { return m.toUpperCase(); });\n"
"  }\n"
"\n"
"  /* Fast-path stringify via rampart.utils.objectToQuery.\n"
"     Only used for default sep='&', eq='=', no opts. */\n"
"  function _fastStringify(obj) {\n"
"    if (obj === null || typeof obj !== 'object') return '';\n"
"    /* Pre-process: null/undefined → '' (rampart writes their text otherwise).\n"
"       Non-primitive non-array values: stringifyPrimitive returns ''. */\n"
"    var copy = {};\n"
"    var keys = Object.keys(obj);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var v = obj[keys[i]];\n"
"      if (Array.isArray(v)) {\n"
"        var arr = new Array(v.length);\n"
"        for (var j = 0; j < v.length; j++) arr[j] = stringifyPrimitive(v[j]);\n"
"        copy[keys[i]] = arr;\n"
"      } else {\n"
"        copy[keys[i]] = stringifyPrimitive(v);\n"
"      }\n"
"    }\n"
"    var raw = native.objectToQuery(copy);\n"
"    /* Post-process: + → %20 (space), uppercase %XX */\n"
"    raw = raw.replace(/\\+/g, '%20');\n"
"    return _normalizeHex(raw);\n"
"  }\n"
"\n"
"  /* Fast-path parse via rampart.utils.queryToObject.\n"
"     Only used for default sep='&', eq='=', no opts. */\n"
"  function _fastParse(qs) {\n"
"    var out = Object.create(null);\n"
"    if (typeof qs !== 'string' || qs.length === 0) return out;\n"
"    /* Pre-escape literal '+' so rampart doesn't decode it as space.\n"
"       Node's querystring (unlike URLSearchParams) leaves '+' as '+'. */\n"
"    var raw = native.queryToObject(qs.replace(/\\+/g, '%2B'));\n"
"    var keys = Object.keys(raw);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var v = raw[keys[i]];\n"
"      /* rampart returns `true` for bare keys; node returns '' */\n"
"      if (v === true) v = '';\n"
"      out[keys[i]] = v;\n"
"    }\n"
"    return out;\n"
"  }\n"
"\n"
"  /* Slow-path JS implementation — for custom sep/eq or when rampart absent. */\n"
"  function _slowStringify(obj, sep, eq, opts) {\n"
"    sep = sep || '&';\n"
"    eq = eq || '=';\n"
"    opts = opts || {};\n"
"    var encode = (opts.encodeURIComponent || encodeURIComponent);\n"
"    if (obj === null || typeof obj !== 'object') return '';\n"
"    var parts = [];\n"
"    var keys = Object.keys(obj);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var k = keys[i];\n"
"      var v = obj[k];\n"
"      var encK = encode(stringifyPrimitive(k));\n"
"      if (Array.isArray(v)) {\n"
"        for (var j = 0; j < v.length; j++) {\n"
"          parts.push(encK + eq + encode(stringifyPrimitive(v[j])));\n"
"        }\n"
"      } else {\n"
"        parts.push(encK + eq + encode(stringifyPrimitive(v)));\n"
"      }\n"
"    }\n"
"    return parts.join(sep);\n"
"  }\n"
"\n"
"  function _slowParse(qs, sep, eq, opts) {\n"
"    var obj = Object.create(null);\n"
"    if (typeof qs !== 'string' || qs.length === 0) return obj;\n"
"    sep = sep || '&';\n"
"    eq = eq || '=';\n"
"    opts = opts || {};\n"
"    /* maxKeys: finite number → use it; Infinity/NaN → no limit;\n"
"       not-a-number (string, etc.) → default 1000 */\n"
"    var maxKeys = 1000;\n"
"    if (typeof opts.maxKeys === 'number') {\n"
"      if (!isFinite(opts.maxKeys)) maxKeys = -1;\n"
"      else maxKeys = opts.maxKeys;\n"
"    }\n"
"    var decode = opts.decodeURIComponent || qsUnescape;\n"
"\n"
"    var pairs = qs.split(sep);\n"
"    if (maxKeys > 0 && pairs.length > maxKeys) pairs.length = maxKeys;\n"
"    for (var i = 0; i < pairs.length; i++) {\n"
"      var pair = pairs[i];\n"
"      var idx = pair.indexOf(eq);\n"
"      var k, v;\n"
"      if (idx === -1) {\n"
"        k = _decodeStr(pair, decode);\n"
"        v = '';\n"
"      } else {\n"
"        k = _decodeStr(pair.substring(0, idx), decode);\n"
"        v = _decodeStr(pair.substring(idx + eq.length), decode);\n"
"      }\n"
"      if (obj[k] === undefined) {\n"
"        obj[k] = v;\n"
"      } else if (Array.isArray(obj[k])) {\n"
"        obj[k].push(v);\n"
"      } else {\n"
"        obj[k] = [obj[k], v];\n"
"      }\n"
"    }\n"
"    return obj;\n"
"  }\n"
"  function _decodeStr(s, decoder) {\n"
"    try { return decoder(s); } catch (e) { return s; }\n"
"  }\n"
"\n"
"  function parse(qs, sep, eq, opts) {\n"
"    if (native && (sep === undefined || sep === '&') &&\n"
"        (eq === undefined || eq === '=') && !opts) {\n"
"      return _fastParse(qs);\n"
"    }\n"
"    return _slowParse(qs, sep, eq, opts);\n"
"  }\n"
"  function stringify(obj, sep, eq, opts) {\n"
"    /* _fastStringify (via rampart C) measured slower than the JS path\n"
"       once post-processing for + → %20 and lowercase-hex was added.\n"
"       Keep _slowStringify. _fastStringify is still defined so it can\n"
"       be re-enabled if post-processing is later moved into C. */\n"
"    return _slowStringify(obj, sep, eq, opts);\n"
"  }\n"
"\n"
"  return {\n"
"    parse: parse,\n"
"    decode: parse,\n"
"    stringify: stringify,\n"
"    encode: stringify,\n"
"    escape: qsEscape,\n"
"    unescape: qsUnescape\n"
"  };\n"
"}";

static void nodeshim_init_querystring(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:qs_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, qs_js);
    duk_call(ctx, 0);
}
/* ============================================================
 * url — node's url module, plus WHATWG URL/URLSearchParams.
 *
 * Foundation is rampart-url (js_modules/rampart-url.js) embedded
 * here rather than required at runtime — keeps the .so
 * self-contained, avoids version skew, lets us tweak field names
 * without affecting users of rampart-url.
 *
 * Layered on top:
 *   - Legacy url.parse / url.format / url.resolve / fileURLToPath /
 *     pathToFileURL / urlToHttpOptions
 *   - WHATWG URL constructor (href/protocol/host/hostname/port/
 *     pathname/search/searchParams/hash/origin/username/password)
 *   - WHATWG URLSearchParams (get/getAll/set/append/delete/has/
 *     sort/keys/values/entries/forEach/toString)
 *   - domainToASCII / domainToUnicode (no-op for ASCII inputs)
 * ============================================================ */

static const char *url_js =
"function() {\n"
"  'use strict';\n"
"\n"
"  /* ----- node legacy url.parse / format / resolve -----\n"
"     All backed by rampart.utils.urlComponents / absUrl, which\n"
"     wrap upa-url (vendored extern/upa, WHATWG-conformant C++ parser\n"
"     + UTS #46 IDN).  parse() re-shapes the WHATWG fields back into\n"
"     node's legacy plain-object shape for backward compat. */\n"
"\n"
"  /* Legacy node-style url.parse — returns the old plain object\n"
"     shape (protocol-with-colon, slashes:boolean, auth, host, etc.).\n"
"     Backed by rampart.utils.urlComponents (the upa WHATWG parser),\n"
"     then re-shaped to the legacy fields.  parseQueryString=true\n"
"     parses the search into an object. */\n"
"  function parse(urlStr, parseQueryString, slashesDenoteHost) {\n"
"    if (typeof urlStr !== 'string') {\n"
"      var e = new TypeError('Parameter \"url\" must be a string, not ' + typeof urlStr);\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"    var c = rampart.utils.urlComponents(urlStr);\n"
"    var u = {\n"
"      protocol: null, slashes: null, auth: null,\n"
"      host: null, port: null, hostname: null, hash: null,\n"
"      search: null, query: null, pathname: null, path: null,\n"
"      href: urlStr\n"
"    };\n"
"    if (!c) return u;\n"
"    u.protocol = c.protocol || null;\n"
"    u.slashes = !!(c.protocol && urlStr.indexOf(c.protocol + '//') === 0);\n"
"    if (c.username || c.password)\n"
"      u.auth = c.username + (c.password ? ':' + c.password : '');\n"
"    u.hostname = c.hostname || null;\n"
"    u.port     = c.port     || null;\n"
"    u.host     = c.host     || null;\n"
"    u.hash     = c.hash     || null;\n"
"    u.pathname = c.pathname || null;\n"
"    u.search   = c.search   || null;\n"
"    if (parseQueryString) {\n"
"      u.query = {};\n"
"      var qs = c.search.charAt(0) === '?' ? c.search.substring(1) : c.search;\n"
"      if (qs) {\n"
"        var pairs = qs.split('&');\n"
"        for (var i = 0; i < pairs.length; i++) {\n"
"          var eq = pairs[i].indexOf('=');\n"
"          var k = eq < 0 ? pairs[i] : pairs[i].substring(0, eq);\n"
"          var v = eq < 0 ? ''       : pairs[i].substring(eq + 1);\n"
"          try { k = decodeURIComponent(k.replace(/\\+/g, ' ')); } catch (e) {}\n"
"          try { v = decodeURIComponent(v.replace(/\\+/g, ' ')); } catch (e) {}\n"
"          if (u.query[k] === undefined)        u.query[k] = v;\n"
"          else if (Array.isArray(u.query[k]))  u.query[k].push(v);\n"
"          else                                 u.query[k] = [u.query[k], v];\n"
"        }\n"
"      }\n"
"    } else {\n"
"      u.query = c.search ? (c.search.charAt(0) === '?' ? c.search.substring(1) : c.search) : null;\n"
"    }\n"
"    u.path = (u.pathname || '') + (u.search || '');\n"
"    return u;\n"
"  }\n"
"\n"
"  function format(u) {\n"
"    if (typeof u === 'string') {\n"
"      var p = rampart.utils.urlComponents(u);\n"
"      if (!p) return u;\n"
"      u = p;\n"
"    }\n"
"    if (!u || typeof u !== 'object') {\n"
"      var e = new TypeError('Parameter \"urlObj\" must be an object, not ' + typeof u);\n"
"      e.code = 'ERR_INVALID_ARG_TYPE';\n"
"      throw e;\n"
"    }\n"
"    var protocol = u.protocol || '';\n"
"    if (protocol && protocol.charAt(protocol.length - 1) !== ':') protocol += ':';\n"
"    var slashes = u.slashes;\n"
"    if (slashes !== false && slashes !== true) slashes = protocol && /^(https?|ftp|gopher|file):/i.test(protocol);\n"
"    var host = u.host;\n"
"    if (host == null) {\n"
"      var hostname = u.hostname || '';\n"
"      var portText = u.port != null ? ':' + u.port : '';\n"
"      host = hostname + portText;\n"
"    }\n"
"    var auth = u.auth || '';\n"
"    if (auth) auth = encodeURI(auth).replace(/%3A/i, ':') + '@';\n"
"    var pathname = u.pathname || '';\n"
"    var search = u.search;\n"
"    if (search === undefined || search === null) {\n"
"      if (u.query) {\n"
"        if (typeof u.query === 'string') search = '?' + u.query;\n"
"        else search = '?' + _stringifyQuery(u.query);\n"
"      } else search = '';\n"
"    }\n"
"    if (search && search.charAt(0) !== '?') search = '?' + search;\n"
"    var hash = u.hash || '';\n"
"    if (hash && hash.charAt(0) !== '#') hash = '#' + hash;\n"
"    var result = '';\n"
"    if (protocol) result += protocol;\n"
"    if (slashes || (host && host.length > 0)) result += '//';\n"
"    result += auth;\n"
"    if (host) result += host;\n"
"    result += pathname + search + hash;\n"
"    return result;\n"
"  }\n"
"\n"
"  function _stringifyQuery(q) {\n"
"    var parts = [];\n"
"    var keys = Object.keys(q);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var v = q[keys[i]];\n"
"      if (Array.isArray(v)) {\n"
"        for (var j = 0; j < v.length; j++)\n"
"          parts.push(encodeURIComponent(keys[i]) + '=' + encodeURIComponent(String(v[j])));\n"
"      } else {\n"
"        parts.push(encodeURIComponent(keys[i]) + '=' + encodeURIComponent(String(v == null ? '' : v)));\n"
"      }\n"
"    }\n"
"    return parts.join('&');\n"
"  }\n"
"\n"
"  function resolve(from, to) {\n"
"    /* node url.resolve(from, to) → rampart absUrl(base, rel). */\n"
"    var r = rampart.utils.absUrl(from, to);\n"
"    return r === undefined ? to : r;\n"
"  }\n"
"\n"
"  function fileURLToPath(url) {\n"
"    if (typeof url === 'object' && url && url.href) url = url.href;\n"
"    if (typeof url !== 'string') {\n"
"      var e = new TypeError('The \"url\" argument must be of type string or an instance of URL.');\n"
"      e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    if (url.substring(0, 7) !== 'file://') {\n"
"      var e = new TypeError('The URL must be of scheme file');\n"
"      e.code = 'ERR_INVALID_URL_SCHEME'; throw e;\n"
"    }\n"
"    var s = url.substring(7);\n"
"    var slash = s.indexOf('/');\n"
"    if (slash >= 0) {\n"
"      /* file://host/path → ignore host on posix (return /path) */\n"
"      try { return decodeURIComponent(s.substring(slash)); }\n"
"      catch (e) { return s.substring(slash); }\n"
"    }\n"
"    try { return decodeURIComponent(s); } catch (e) { return s; }\n"
"  }\n"
"\n"
"  function pathToFileURL(path) {\n"
"    if (typeof path !== 'string') {\n"
"      var e = new TypeError('Path must be a string'); e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"    }\n"
"    /* node: only absolute paths produce a working file URL */\n"
"    var enc = encodeURI(path).replace(/[?#]/g, function(c) {\n"
"      return c === '?' ? '%3F' : '%23';\n"
"    });\n"
"    var href = 'file://' + (enc.charAt(0) === '/' ? '' : '/') + enc;\n"
"    return new globalThis.URL(href);\n"
"  }\n"
"\n"
"  function urlToHttpOptions(url) {\n"
"    return {\n"
"      protocol: url.protocol, hostname: url.hostname,\n"
"      hash: url.hash, search: url.search, pathname: url.pathname,\n"
"      path: (url.pathname || '') + (url.search || ''),\n"
"      href: url.href,\n"
"      port: url.port ? Number(url.port) : undefined,\n"
"      auth: url.username ? (url.username + (url.password ? ':' + url.password : '')) : undefined\n"
"    };\n"
"  }\n"
"\n"
"  /* Route through rampart.utils (upa's UTS #46 IDN). */\n"
"  function domainToASCII(d) {\n"
"    if (d == null) return '';\n"
"    try { return rampart.utils.toASCII(String(d)); }\n"
"    catch (e) { return ''; }\n"
"  }\n"
"  function domainToUnicode(d) {\n"
"    if (d == null) return '';\n"
"    try { return rampart.utils.toUnicode(String(d)); }\n"
"    catch (e) { return ''; }\n"
"  }\n"
"\n"
"  /* URL / URLSearchParams classes now live in rampart-whatwg.so\n"
"     and are exposed below via globalThis getters on _out.  The\n"
"     Legacy Url constructor (~unused) is still provided as a stub\n"
"     so require('url').Url remains truthy for old code. */\n"
"  function LegacyUrl() {}\n"
"\n"
"  /* URL and URLSearchParams now live in rampart-whatwg.so.\n"
"     Expose via getters so the lazy global getter on globalThis.URL\n"
"     only fires when require('url').URL is actually accessed — by\n"
"     then rampart-whatwg has loaded and installed the real class. */\n"
"  var _out = {\n"
"    parse: parse,\n"
"    format: format,\n"
"    resolve: resolve,\n"
"    fileURLToPath: fileURLToPath,\n"
"    pathToFileURL: pathToFileURL,\n"
"    urlToHttpOptions: urlToHttpOptions,\n"
"    domainToASCII: domainToASCII,\n"
"    domainToUnicode: domainToUnicode,\n"
"    Url: LegacyUrl\n"
"  };\n"
"  Object.defineProperty(_out, 'URL', {\n"
"    get: function() { return globalThis.URL; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"  Object.defineProperty(_out, 'URLSearchParams', {\n"
"    get: function() { return globalThis.URLSearchParams; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"  return _out;\n"
"}";

static void nodeshim_init_url(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:url_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, url_js);
    duk_call(ctx, 0);

    /* URL/URLSearchParams (and their statics like .canParse/.parse) now
     * live in rampart-whatwg.so.  The url_js return object exposes them
     * via getters on globalThis.URL/URLSearchParams — see the bottom of
     * url_js.  Do NOT access -1.URL here: it would trigger the global
     * getter and recursively require rampart-whatwg before nodeshim's
     * own init completes. */
}
/* ============================================================
 * assert — node's assert module.
 * Inline JS (Pattern 2). assert is a callable function with
 * properties (ok/equal/strict/.../AssertionError) — modeling that
 * directly in JS is simpler than building it from duktape stack ops.
 *
 * Covers: ok, equal/notEqual, strictEqual/notStrictEqual,
 * deepEqual/notDeepEqual, deepStrictEqual/notDeepStrictEqual,
 * throws/doesNotThrow, rejects/doesNotReject (need Promise),
 * ifError, fail, match/doesNotMatch, AssertionError class,
 * assert.strict subnamespace.
 * Deferred: assert.CallTracker (deprecated), partial-deep-equal,
 * util.inspect-style diff in default messages.
 * ============================================================ */

static const char *assert_js =
"function() {\n"
"  'use strict';\n"
"  /* Lazy reference to util.inspect for default-message formatting */\n"
"  var _inspect = null;\n"
"  function inspect(v) {\n"
"    if (_inspect === null) {\n"
"      try { _inspect = require('rampart-nodeshim').util.inspect; }\n"
"      catch (e) { _inspect = function(x) { try { return JSON.stringify(x); } catch(_) { return String(x); } }; }\n"
"    }\n"
"    return _inspect(v);\n"
"  }\n"
"\n"
"  function _msg(o) {\n"
"    return inspect(o.actual) + ' ' + (o.operator || '==') + ' ' + inspect(o.expected);\n"
"  }\n"
"\n"
"  function AssertionError(opts) {\n"
"    opts = opts || {};\n"
"    var err = new Error(opts.message || _msg(opts));\n"
"    err.name = 'AssertionError';\n"
"    err.code = 'ERR_ASSERTION';\n"
"    err.actual = opts.actual;\n"
"    err.expected = opts.expected;\n"
"    err.operator = opts.operator;\n"
"    err.generatedMessage = !opts.message;\n"
"    try { Object.setPrototypeOf(err, AssertionError.prototype); } catch (e) {}\n"
"    return err;\n"
"  }\n"
"  AssertionError.prototype = Object.create(Error.prototype);\n"
"  AssertionError.prototype.name = 'AssertionError';\n"
"  AssertionError.prototype.constructor = AssertionError;\n"
"\n"
"  function _deepEq(a, b, strict, seen) {\n"
"    if (strict ? a === b : a == b) return true;\n"
"    if (strict && a !== a && b !== b) return true; /* both NaN */\n"
"    if (a === null || b === null || a === undefined || b === undefined) return false;\n"
"    if (typeof a !== 'object' || typeof b !== 'object') return strict ? false : (a == b);\n"
"    if (strict && Object.getPrototypeOf(a) !== Object.getPrototypeOf(b)) return false;\n"
"    if (a instanceof Date) return b instanceof Date && a.getTime() === b.getTime();\n"
"    if (a instanceof RegExp) return b instanceof RegExp && a.toString() === b.toString();\n"
"    if (Array.isArray(a)) {\n"
"      if (!Array.isArray(b) || a.length !== b.length) return false;\n"
"      for (var i = 0; i < a.length; i++) if (!_deepEq(a[i], b[i], strict, seen)) return false;\n"
"      return true;\n"
"    }\n"
"    for (var i = 0; i < seen.length; i++) if (seen[i].a === a && seen[i].b === b) return true;\n"
"    seen = seen.concat([{a: a, b: b}]);\n"
"    var ak = Object.keys(a), bk = Object.keys(b);\n"
"    if (ak.length !== bk.length) return false;\n"
"    ak.sort(); bk.sort();\n"
"    for (var i = 0; i < ak.length; i++) {\n"
"      if (ak[i] !== bk[i]) return false;\n"
"      if (!_deepEq(a[ak[i]], b[bk[i]], strict, seen)) return false;\n"
"    }\n"
"    return true;\n"
"  }\n"
"\n"
"  function ok(value, message) {\n"
"    if (!value) {\n"
"      throw AssertionError({\n"
"        actual: value, expected: true, operator: '==',\n"
"        message: typeof message === 'string' ? message : (message instanceof Error ? message.message : undefined)\n"
"      });\n"
"    }\n"
"  }\n"
"\n"
"  function fail(actualOrMsg, expected, message, operator) {\n"
"    if (arguments.length === 0) {\n"
"      var e = AssertionError({ message: 'Failed', operator: 'fail' });\n"
"      e.generatedMessage = true;\n"
"      throw e;\n"
"    }\n"
"    if (arguments.length === 1) {\n"
"      /* fail(message) — message may be a string or Error */\n"
"      if (actualOrMsg instanceof Error) throw actualOrMsg;\n"
"      throw AssertionError({ message: actualOrMsg, operator: 'fail' });\n"
"    }\n"
"    throw AssertionError({\n"
"      actual: actualOrMsg, expected: expected, operator: operator || 'fail', message: message\n"
"    });\n"
"  }\n"
"\n"
"  function equal(actual, expected, message) {\n"
"    if (actual != expected) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: '==', message: message\n"
"    });\n"
"  }\n"
"  function notEqual(actual, expected, message) {\n"
"    if (actual == expected) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: '!=', message: message\n"
"    });\n"
"  }\n"
"  function strictEqual(actual, expected, message) {\n"
"    if (actual === expected) return;\n"
"    if (actual !== actual && expected !== expected) return; /* NaN */\n"
"    throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'strictEqual', message: message\n"
"    });\n"
"  }\n"
"  function notStrictEqual(actual, expected, message) {\n"
"    if (actual !== expected) {\n"
"      if (actual !== actual && expected !== expected) {\n"
"        throw AssertionError({\n"
"          actual: actual, expected: expected, operator: 'notStrictEqual', message: message\n"
"        });\n"
"      }\n"
"      return;\n"
"    }\n"
"    throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'notStrictEqual', message: message\n"
"    });\n"
"  }\n"
"  function deepEqual(actual, expected, message) {\n"
"    if (!_deepEq(actual, expected, false, [])) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'deepEqual', message: message\n"
"    });\n"
"  }\n"
"  function notDeepEqual(actual, expected, message) {\n"
"    if (_deepEq(actual, expected, false, [])) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'notDeepEqual', message: message\n"
"    });\n"
"  }\n"
"  function deepStrictEqual(actual, expected, message) {\n"
"    if (!_deepEq(actual, expected, true, [])) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'deepStrictEqual', message: message\n"
"    });\n"
"  }\n"
"  function notDeepStrictEqual(actual, expected, message) {\n"
"    if (_deepEq(actual, expected, true, [])) throw AssertionError({\n"
"      actual: actual, expected: expected, operator: 'notDeepStrictEqual', message: message\n"
"    });\n"
"  }\n"
"\n"
"  function _matches(thrown, expected) {\n"
"    if (typeof expected === 'function') {\n"
"      /* class/constructor: instanceof OR matcher fn returning truthy */\n"
"      try { if (thrown instanceof expected) return true; } catch (e) {}\n"
"      try { var r = expected(thrown); return !!r; } catch (e) { return false; }\n"
"    }\n"
"    if (expected instanceof RegExp) {\n"
"      return expected.test(typeof thrown === 'string' ? thrown : (thrown && thrown.message ? thrown.message : String(thrown)));\n"
"    }\n"
"    if (typeof expected === 'object' && expected !== null) {\n"
"      for (var key in expected) {\n"
"        if (!Object.prototype.hasOwnProperty.call(expected, key)) continue;\n"
"        var exp = expected[key];\n"
"        var act = thrown == null ? undefined : thrown[key];\n"
"        if (exp instanceof RegExp) {\n"
"          if (!exp.test(String(act))) return false;\n"
"        } else if (typeof exp === 'object' && exp !== null) {\n"
"          if (!_deepEq(act, exp, true, [])) return false;\n"
"        } else {\n"
"          if (act !== exp) return false;\n"
"        }\n"
"      }\n"
"      return true;\n"
"    }\n"
"    if (typeof expected === 'string') {\n"
"      /* String is taken as a message hint, not a match. Node behavior. */\n"
"      return true;\n"
"    }\n"
"    return true;\n"
"  }\n"
"\n"
"  function throws(fn, expected, message) {\n"
"    if (typeof expected === 'string') { message = expected; expected = undefined; }\n"
"    var thrown = null, caught = false;\n"
"    try { fn(); } catch (e) { thrown = e; caught = true; }\n"
"    if (!caught) throw AssertionError({\n"
"      actual: undefined, expected: expected, operator: 'throws',\n"
"      message: message || 'Missing expected exception.'\n"
"    });\n"
"    if (expected !== undefined && !_matches(thrown, expected)) throw AssertionError({\n"
"      actual: thrown, expected: expected, operator: 'throws',\n"
"      message: message || 'Got unwanted exception.'\n"
"    });\n"
"  }\n"
"\n"
"  function doesNotThrow(fn, expected, message) {\n"
"    if (typeof expected === 'string') { message = expected; expected = undefined; }\n"
"    var thrown = null, caught = false;\n"
"    try { fn(); } catch (e) { thrown = e; caught = true; }\n"
"    if (!caught) return;\n"
"    if (expected === undefined || _matches(thrown, expected)) {\n"
"      throw AssertionError({\n"
"        actual: thrown, expected: undefined, operator: 'doesNotThrow',\n"
"        message: (message ? message + ': ' : '') + 'Got unwanted exception.' +\n"
"                 (thrown && thrown.message ? ' ' + thrown.message : '')\n"
"      });\n"
"    }\n"
"    throw thrown;\n"
"  }\n"
"\n"
"  function rejects(promiseOrFn, expected, message) {\n"
"    if (typeof expected === 'string') { message = expected; expected = undefined; }\n"
"    var p;\n"
"    try {\n"
"      p = typeof promiseOrFn === 'function' ? promiseOrFn() : promiseOrFn;\n"
"    } catch (e) {\n"
"      return Promise.reject(AssertionError({\n"
"        actual: e, expected: expected, operator: 'rejects',\n"
"        message: 'Got unwanted exception.'\n"
"      }));\n"
"    }\n"
"    if (!p || typeof p.then !== 'function') {\n"
"      return Promise.reject(AssertionError({\n"
"        message: 'Expected a Promise (or a function returning one)'\n"
"      }));\n"
"    }\n"
"    return Promise.resolve(p).then(function() {\n"
"      throw AssertionError({\n"
"        actual: undefined, expected: expected, operator: 'rejects',\n"
"        message: message || 'Missing expected rejection.'\n"
"      });\n"
"    }, function(e) {\n"
"      if (expected !== undefined && !_matches(e, expected)) {\n"
"        throw AssertionError({\n"
"          actual: e, expected: expected, operator: 'rejects',\n"
"          message: message || 'Got unwanted rejection.'\n"
"        });\n"
"      }\n"
"    });\n"
"  }\n"
"\n"
"  function doesNotReject(promiseOrFn, expected, message) {\n"
"    if (typeof expected === 'string') { message = expected; expected = undefined; }\n"
"    var p;\n"
"    try {\n"
"      p = typeof promiseOrFn === 'function' ? promiseOrFn() : promiseOrFn;\n"
"    } catch (e) {\n"
"      return Promise.reject(AssertionError({\n"
"        actual: e, operator: 'doesNotReject',\n"
"        message: message || 'Got unwanted exception.'\n"
"      }));\n"
"    }\n"
"    return Promise.resolve(p).then(function() {}, function(e) {\n"
"      throw AssertionError({\n"
"        actual: e, operator: 'doesNotReject',\n"
"        message: message || 'Got unwanted rejection.'\n"
"      });\n"
"    });\n"
"  }\n"
"\n"
"  function ifError(value) {\n"
"    if (value === null || value === undefined) return;\n"
"    var msg;\n"
"    if (value instanceof Error) msg = value.message || String(value);\n"
"    else msg = String(value);\n"
"    throw AssertionError({\n"
"      actual: value, expected: null, operator: 'ifError',\n"
"      message: 'ifError got unwanted exception: ' + msg\n"
"    });\n"
"  }\n"
"\n"
"  function match(string, regexp, message) {\n"
"    if (typeof string !== 'string') throw AssertionError({\n"
"      message: message || 'The \"string\" argument must be of type string. Received type ' + typeof string\n"
"    });\n"
"    if (!(regexp instanceof RegExp)) throw AssertionError({\n"
"      message: message || 'The \"regexp\" argument must be an instance of RegExp.'\n"
"    });\n"
"    if (!regexp.test(string)) throw AssertionError({\n"
"      actual: string, expected: regexp, operator: 'match',\n"
"      message: message || 'The input did not match the regular expression ' + regexp + '.'\n"
"    });\n"
"  }\n"
"  function doesNotMatch(string, regexp, message) {\n"
"    if (typeof string !== 'string') throw AssertionError({\n"
"      message: message || 'The \"string\" argument must be of type string. Received type ' + typeof string\n"
"    });\n"
"    if (!(regexp instanceof RegExp)) throw AssertionError({\n"
"      message: message || 'The \"regexp\" argument must be an instance of RegExp.'\n"
"    });\n"
"    if (regexp.test(string)) throw AssertionError({\n"
"      actual: string, expected: regexp, operator: 'doesNotMatch',\n"
"      message: message || 'The input was expected to not match the regular expression ' + regexp + '.'\n"
"    });\n"
"  }\n"
"\n"
"  /* assert is callable as assert(value) === assert.ok(value) */\n"
"  var assert = function(value, message) { return ok(value, message); };\n"
"  assert.ok = ok;\n"
"  assert.fail = fail;\n"
"  assert.equal = equal;\n"
"  assert.notEqual = notEqual;\n"
"  assert.strictEqual = strictEqual;\n"
"  assert.notStrictEqual = notStrictEqual;\n"
"  assert.deepEqual = deepEqual;\n"
"  assert.notDeepEqual = notDeepEqual;\n"
"  assert.deepStrictEqual = deepStrictEqual;\n"
"  assert.notDeepStrictEqual = notDeepStrictEqual;\n"
"  assert.throws = throws;\n"
"  assert.doesNotThrow = doesNotThrow;\n"
"  assert.rejects = rejects;\n"
"  assert.doesNotReject = doesNotReject;\n"
"  assert.ifError = ifError;\n"
"  assert.match = match;\n"
"  assert.doesNotMatch = doesNotMatch;\n"
"  assert.AssertionError = AssertionError;\n"
"\n"
"  /* assert.strict — loose variants alias the strict ones */\n"
"  var strict = function(value, message) { return ok(value, message); };\n"
"  strict.ok = ok;\n"
"  strict.fail = fail;\n"
"  strict.equal = strictEqual;\n"
"  strict.notEqual = notStrictEqual;\n"
"  strict.strictEqual = strictEqual;\n"
"  strict.notStrictEqual = notStrictEqual;\n"
"  strict.deepEqual = deepStrictEqual;\n"
"  strict.notDeepEqual = notDeepStrictEqual;\n"
"  strict.deepStrictEqual = deepStrictEqual;\n"
"  strict.notDeepStrictEqual = notDeepStrictEqual;\n"
"  strict.throws = throws;\n"
"  strict.doesNotThrow = doesNotThrow;\n"
"  strict.rejects = rejects;\n"
"  strict.doesNotReject = doesNotReject;\n"
"  strict.ifError = ifError;\n"
"  strict.match = match;\n"
"  strict.doesNotMatch = doesNotMatch;\n"
"  strict.AssertionError = AssertionError;\n"
"  strict.strict = strict;\n"
"  assert.strict = strict;\n"
"\n"
"  /* assert.partialDeepStrictEqual (node 22+).  Asserts that every\n"
"     key/value in `expected` appears (deep-strict-equal) in `actual`.\n"
"     Extra keys in actual are ignored. */\n"
"  function partialDeepStrictEqual(actual, expected, message) {\n"
"    function recur(a, e, path) {\n"
"      if (e === null || typeof e !== 'object') {\n"
"        if (!Object.is(a, e))\n"
"          fail('partial deep equality failed at ' + path + ': expected ' +\n"
"               JSON.stringify(e) + ', got ' + JSON.stringify(a));\n"
"        return;\n"
"      }\n"
"      if (a === null || typeof a !== 'object')\n"
"        fail('partial deep equality failed at ' + path + ': expected object, got ' +\n"
"             (a === null ? 'null' : typeof a));\n"
"      if (Array.isArray(e)) {\n"
"        if (!Array.isArray(a))\n"
"          fail('partial deep equality failed at ' + path + ': expected array');\n"
"        for (var i = 0; i < e.length; i++) recur(a[i], e[i], path + '[' + i + ']');\n"
"        return;\n"
"      }\n"
"      for (var k in e) {\n"
"        if (!Object.prototype.hasOwnProperty.call(e, k)) continue;\n"
"        if (!Object.prototype.hasOwnProperty.call(a, k))\n"
"          fail('partial deep equality failed at ' + path + ': missing key ' + JSON.stringify(k));\n"
"        recur(a[k], e[k], path + '.' + k);\n"
"      }\n"
"    }\n"
"    try { recur(actual, expected, ''); }\n"
"    catch (e) { if (e.code === 'ERR_ASSERTION') throw e; throw e; }\n"
"  }\n"
"  assert.partialDeepStrictEqual = partialDeepStrictEqual;\n"
"  strict.partialDeepStrictEqual = partialDeepStrictEqual;\n"
"\n"
"  /* ----- CallTracker (deprecated in node 20 but still callable) -----\n"
"     Tracks how many times wrapped functions are called; verify() throws\n"
"     listing any that didn't hit their expected count. */\n"
"  function CallTracker() {\n"
"    /* entries: array of {fn, wrapped, expected, calls: [{thisArg, arguments}]} */\n"
"    var entries = [];\n"
"\n"
"    this.calls = function(fn, exact) {\n"
"      if (typeof fn !== 'function') { exact = fn; fn = function() {}; }\n"
"      if (exact === undefined) exact = 1;\n"
"      if (typeof exact !== 'number' || exact < 0)\n"
"        throw new TypeError('CallTracker.calls: exact must be a non-negative number');\n"
"      var entry = {fn: fn, expected: exact, calls: []};\n"
"      entry.wrapped = function() {\n"
"        var args = [];\n"
"        for (var i = 0; i < arguments.length; i++) args.push(arguments[i]);\n"
"        entry.calls.push({thisArg: this, arguments: args});\n"
"        return fn.apply(this, arguments);\n"
"      };\n"
"      entries.push(entry);\n"
"      return entry.wrapped;\n"
"    };\n"
"\n"
"    this.getCalls = function(fn) {\n"
"      for (var i = 0; i < entries.length; i++)\n"
"        if (entries[i].wrapped === fn || entries[i].fn === fn)\n"
"          return entries[i].calls.slice();\n"
"      return [];\n"
"    };\n"
"\n"
"    this.report = function() {\n"
"      var out = [];\n"
"      for (var i = 0; i < entries.length; i++) {\n"
"        var e = entries[i];\n"
"        if (e.calls.length !== e.expected)\n"
"          out.push({message: 'Expected ' + e.expected + ' call(s), got ' + e.calls.length,\n"
"                    actual: e.calls.length, expected: e.expected,\n"
"                    operator: 'CallTracker'});\n"
"      }\n"
"      return out;\n"
"    };\n"
"\n"
"    this.verify = function() {\n"
"      var report = this.report();\n"
"      if (report.length === 0) return;\n"
"      var msg = report.map(function(r) { return r.message; }).join('\\n');\n"
"      var err = new AssertionError({message: msg, operator: 'CallTracker.verify'});\n"
"      throw err;\n"
"    };\n"
"\n"
"    this.reset = function(fn) {\n"
"      if (fn === undefined) {\n"
"        for (var i = 0; i < entries.length; i++) entries[i].calls = [];\n"
"        return;\n"
"      }\n"
"      for (var j = 0; j < entries.length; j++)\n"
"        if (entries[j].wrapped === fn || entries[j].fn === fn)\n"
"          entries[j].calls = [];\n"
"    };\n"
"  }\n"
"  assert.CallTracker = CallTracker;\n"
"  strict.CallTracker = CallTracker;\n"
"\n"
"  return assert;\n"
"}";

static void nodeshim_init_assert(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:assert_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, assert_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * timers — node's `timers` module.  Re-exports the rampart globals
 * (setTimeout/clearTimeout/setInterval/clearInterval/setImmediate/
 * clearImmediate).  Adds ref()/unref()/hasRef()/refresh() stubs to
 * the returned handles so node code that calls them doesn't throw.
 * No `timers/promises` submodule — rampart code typically doesn't use
 * Promises.
 * ============================================================ */
static const char *timers_js =
"function() {\n"
"  'use strict';\n"
"  function _patch(h) {\n"
"    if (h && typeof h === 'object' && typeof h.ref !== 'function') {\n"
"      h.ref = function()     { return this; };\n"
"      h.unref = function()   { return this; };\n"
"      h.hasRef = function()  { return true; };\n"
"      h.refresh = function() { return this; };\n"
"    }\n"
"    return h;\n"
"  }\n"
"  function _wrap(fn) {\n"
"    return function() { return _patch(fn.apply(null, arguments)); };\n"
"  }\n"
"  return {\n"
"    setTimeout:     _wrap(setTimeout),\n"
"    clearTimeout:   clearTimeout,\n"
"    setInterval:    _wrap(setInterval),\n"
"    clearInterval:  clearTimeout,    /* same impl in rampart */\n"
"    setImmediate:   _wrap(setImmediate),\n"
"    clearImmediate: clearTimeout,\n"
"    /* legacy alias for refresh() */\n"
"    active: function(h) { return (h && h.refresh) ? h.refresh() : h; },\n"
"    /* unenroll/enroll are legacy deprecated -- no-ops */\n"
"    unenroll: function() {},\n"
"    enroll:   function() {}\n"
"  };\n"
"}";
static void nodeshim_init_timers(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:timers_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, timers_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * string_decoder — node's StringDecoder class.  Built on our fixed
 * TextDecoder for utf-8/utf-16le/etc., with chunk-boundary buffering
 * for the multi-byte utf-8 case.  Supported encodings: utf-8 (default),
 * utf-16le, latin1, ascii, base64, hex.
 * ============================================================ */
static const char *string_decoder_js =
"function() {\n"
"  'use strict';\n"
"  function StringDecoder(encoding) {\n"
"    if (!(this instanceof StringDecoder)) return new StringDecoder(encoding);\n"
"    encoding = String(encoding || 'utf-8').toLowerCase();\n"
"    /* normalize a few common aliases */\n"
"    if (encoding === 'utf8')    encoding = 'utf-8';\n"
"    if (encoding === 'utf16le' || encoding === 'ucs2' || encoding === 'ucs-2')\n"
"      encoding = 'utf-16le';\n"
"    if (encoding === 'binary')  encoding = 'latin1';\n"
"    this.encoding = encoding;\n"
"    /* Per-encoding buffered-bytes count.  utf-8 may have 1-3 bytes\n"
"       pending across .write() calls; utf-16le may have 1 byte pending;\n"
"       latin1/ascii/base64/hex have well-defined chunk boundaries. */\n"
"    this._buf = Buffer.alloc(8);   /* enough for utf-8 (4) + utf-16 (2) + base64 (3) margin */\n"
"    this._nbuf = 0;\n"
"  }\n"
"\n"
"  /* For utf-8: figure out how many trailing bytes of `buf` form an\n"
"     incomplete codepoint that should be buffered for the next call.\n"
"     Returns count (0-3). */\n"
"  function _utf8Incomplete(buf) {\n"
"    var n = buf.length;\n"
"    if (n === 0) return 0;\n"
"    /* Look back up to 4 bytes for a leading byte that needs more. */\n"
"    for (var look = 1; look <= 3 && look <= n; look++) {\n"
"      var b = buf[n - look];\n"
"      if ((b & 0xC0) === 0x80) continue;  /* continuation byte; keep going */\n"
"      if ((b & 0xE0) === 0xC0) return look < 2 ? look : 0;  /* 2-byte; needs 1 cont */\n"
"      if ((b & 0xF0) === 0xE0) return look < 3 ? look : 0;  /* 3-byte; needs 2 conts */\n"
"      if ((b & 0xF8) === 0xF0) return look < 4 ? look : 0;  /* 4-byte; needs 3 conts */\n"
"      return 0;\n"
"    }\n"
"    return 0;\n"
"  }\n"
"\n"
"  StringDecoder.prototype.write = function(chunk) {\n"
"    if (typeof chunk === 'string') chunk = Buffer.from(chunk, 'binary');\n"
"    if (!Buffer.isBuffer(chunk) && !(chunk instanceof Uint8Array)) chunk = Buffer.from(chunk);\n"
"    var enc = this.encoding;\n"
"    /* For non-multi-byte-prone encodings, just decode directly. */\n"
"    if (enc === 'latin1' || enc === 'us-ascii' || enc === 'ascii' ||\n"
"        enc === 'base64' || enc === 'hex' || enc === 'base64url') {\n"
"      var encName = (enc === 'us-ascii') ? 'ascii' : enc;\n"
"      return Buffer.from(chunk).toString(encName);\n"
"    }\n"
"\n"
"    /* utf-8: prepend any previously buffered bytes, then split off the\n"
"       trailing incomplete codepoint (if any) to buffer for next call. */\n"
"    if (enc === 'utf-8') {\n"
"      var combined;\n"
"      if (this._nbuf > 0) {\n"
"        combined = Buffer.alloc(this._nbuf + chunk.length);\n"
"        for (var i = 0; i < this._nbuf; i++) combined[i] = this._buf[i];\n"
"        for (var j = 0; j < chunk.length; j++) combined[this._nbuf + j] = chunk[j];\n"
"        this._nbuf = 0;\n"
"      } else {\n"
"        combined = chunk;\n"
"      }\n"
"      var pending = _utf8Incomplete(combined);\n"
"      if (pending > 0) {\n"
"        var emitLen = combined.length - pending;\n"
"        var emit = combined.slice(0, emitLen);\n"
"        for (var k = 0; k < pending; k++) this._buf[k] = combined[emitLen + k];\n"
"        this._nbuf = pending;\n"
"        return emit.toString('utf8');\n"
"      }\n"
"      return Buffer.from(combined).toString('utf8');\n"
"    }\n"
"\n"
"    /* utf-16le: needs even-byte alignment. */\n"
"    if (enc === 'utf-16le') {\n"
"      var combined2;\n"
"      if (this._nbuf > 0) {\n"
"        combined2 = Buffer.alloc(this._nbuf + chunk.length);\n"
"        for (var ii = 0; ii < this._nbuf; ii++) combined2[ii] = this._buf[ii];\n"
"        for (var jj = 0; jj < chunk.length; jj++) combined2[this._nbuf + jj] = chunk[jj];\n"
"        this._nbuf = 0;\n"
"      } else {\n"
"        combined2 = chunk;\n"
"      }\n"
"      var odd = combined2.length & 1;\n"
"      if (odd) {\n"
"        this._buf[0] = combined2[combined2.length - 1];\n"
"        this._nbuf = 1;\n"
"        return combined2.slice(0, combined2.length - 1).toString('utf16le');\n"
"      }\n"
"      return Buffer.from(combined2).toString('utf16le');\n"
"    }\n"
"\n"
"    /* Fallback: just utf-8 */\n"
"    return Buffer.from(chunk).toString('utf8');\n"
"  };\n"
"\n"
"  StringDecoder.prototype.end = function(chunk) {\n"
"    var out = '';\n"
"    if (chunk !== undefined && chunk.length > 0) out = this.write(chunk);\n"
"    if (this._nbuf > 0) {\n"
"      /* Anything left over is unrepresentable -- emit as U+FFFD per spec. */\n"
"      out += '\\uFFFD';\n"
"      this._nbuf = 0;\n"
"    }\n"
"    return out;\n"
"  };\n"
"\n"
"  return { StringDecoder: StringDecoder };\n"
"}";
static void nodeshim_init_string_decoder(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:string_decoder_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, string_decoder_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * stream — Node's stream module, adapter on top of WHATWG Streams.
 *
 * Wraps globalThis.ReadableStream / WritableStream / TransformStream
 * (installed by rampart-whatwg.so) with Node's classic EventEmitter
 * API surface: .on('data')/.on('end')/.on('drain')/.on('finish')/
 * .on('error')/.on('close'), .pipe(dest), .read(), .write(), .end(),
 * .destroy(), .push().
 *
 * Coverage:
 *  - Readable / Writable / Duplex / Transform / PassThrough classes
 *  - stream.pipeline(...streams, cb)
 *  - stream.finished(stream, cb)  (callback only; no Promise variant)
 *  - Readable.from(iterable / arraylike / async-iterable-stub)
 *  - Readable.toWeb / Readable.fromWeb
 *  - Writable.toWeb / Writable.fromWeb
 *
 * Caveats:
 *  - objectMode: best-effort.  WHATWG streams ARE object mode by
 *    default; we don't enforce byte coercion.
 *  - encoding (setEncoding/setDefaultEncoding): basic — strings are
 *    passed through; the StringDecoder adapter for partial UTF-8 is
 *    NOT plumbed.  For byte streams users get raw Uint8Array chunks.
 *  - highWaterMark: best-effort via WHATWG's queuing strategy.
 *  - cork/uncork: stubs (no-op).  Real cork batching not implemented.
 *  - Duplex.allowHalfOpen: stub field; close behavior simplified.
 * ============================================================ */
static const char *stream_js =
"function(EventEmitter) {\n"
"  'use strict';\n"
"  var W_RS = globalThis.ReadableStream;\n"
"  var W_WS = globalThis.WritableStream;\n"
"  var W_TS = globalThis.TransformStream;\n"
"\n"
"  function _isEnd(c) { return c === null || c === undefined; }\n"
"\n"
"  /* ---------- Readable -----------------------------------------\n"
"     Wraps a WHATWG ReadableStream.  Two modes:\n"
"       - paused: caller calls .read() to pull chunks\n"
"       - flowing: .on('data', cb) attaches → chunks pushed via 'data' events\n"
"  */\n"
"  function Readable(opts) {\n"
"    if (!(this instanceof Readable)) return new Readable(opts);\n"
"    EventEmitter.call(this);\n"
"    opts = opts || {};\n"
"    var self = this;\n"
"    self._readableState = {\n"
"      flowing: null, ended: false, destroyed: false, errored: null,\n"
"      buffer: [], reading: false,\n"
"      objectMode: !!opts.objectMode,\n"
"      highWaterMark: opts.highWaterMark || 16384\n"
"    };\n"
"    self.readable = true;\n"
"    if (typeof opts.read === 'function') self._read = opts.read;\n"
"    /* Build underlying WHATWG stream — controller exposes enqueue/close/error\n"
"       which we hand off via the .push() Node API. */\n"
"    self._web = new W_RS({\n"
"      start: function (ctrl) { self._wctrl = ctrl; },\n"
"      pull: function () {\n"
"        /* Node API: subclasses define `_read` on the instance (either via\n"
"           opts.read or by post-construction assignment). */\n"
"        if (typeof self._read === 'function' && !self._readableState.reading) {\n"
"          self._readableState.reading = true;\n"
"          try { self._read.call(self, self._readableState.highWaterMark); }\n"
"          catch (e) { self.destroy(e); }\n"
"          self._readableState.reading = false;\n"
"        }\n"
"      },\n"
"      cancel: function (reason) {\n"
"        if (typeof opts.destroy === 'function')\n"
"          try { opts.destroy.call(self, reason, function(){}); } catch (_) {}\n"
"      }\n"
"    });\n"
"    /* Listener auto-start: .on('data') flips to flowing mode. */\n"
"    var origOn = self.on;\n"
"    self.on = function (event, fn) {\n"
"      var r = origOn.call(self, event, fn);\n"
"      if (event === 'data' && self._readableState.flowing !== false) {\n"
"        self.resume();\n"
"      }\n"
"      if (event === 'readable' && self._readableState.flowing === null) {\n"
"        self._readableState.flowing = false;  /* don't auto-flow */\n"
"      }\n"
"      return r;\n"
"    };\n"
"    self.addListener = self.on;\n"
"  }\n"
"  Readable.prototype = Object.create(EventEmitter.prototype);\n"
"  Readable.prototype.constructor = Readable;\n"
"  /* .push(chunk) — caller-side enqueue.  push(null) signals end. */\n"
"  Readable.prototype.push = function (chunk, encoding) {\n"
"    var s = this._readableState;\n"
"    if (s.ended) return false;\n"
"    if (_isEnd(chunk)) {\n"
"      s.ended = true;\n"
"      try { this._wctrl.close(); } catch (_) {}\n"
"      return false;\n"
"    }\n"
"    try { this._wctrl.enqueue(chunk); }\n"
"    catch (e) { this.destroy(e); return false; }\n"
"    return true;\n"
"  };\n"
"  /* .read([n]) — paused-mode pull.  Returns next chunk or null. */\n"
"  Readable.prototype.read = function () {\n"
"    var s = this._readableState;\n"
"    if (!this._reader) this._reader = this._web.getReader();\n"
"    /* In paused mode we read async but expose a sync interface; for\n"
"       simplicity, .read() returns null and emits 'readable' when chunks\n"
"       are available.  Most callers use .on('data') instead. */\n"
"    var self = this;\n"
"    if (s.buffer.length > 0) return s.buffer.shift();\n"
"    if (s.ended) return null;\n"
"    this._reader.read().then(function (r) {\n"
"      if (r.done) {\n"
"        s.ended = true;\n"
"        self.emit('end');\n"
"        self.readableEnded = true;\n"
"        self.emit('close');\n"
"      } else {\n"
"        s.buffer.push(r.value);\n"
"        self.emit('readable');\n"
"      }\n"
"    }, function (e) { self.destroy(e); });\n"
"    return null;\n"
"  };\n"
"  /* Flowing mode: drain reader, emit 'data' for each chunk. */\n"
"  Readable.prototype.resume = function () {\n"
"    /* Lazy-IM safe: no state allocated means there's nothing to drain. */\n"
"    if (!this._readableState) return this;\n"
"    var s = this._readableState;\n"
"    /* Don't gate on s.ended: push(null) sets that synchronously, but\n"
"       the WHATWG reader still drains buffered chunks first before\n"
"       returning {done:true}.  Bailing on s.ended here would skip\n"
"       chunks pushed sync alongside the close signal — a real problem\n"
"       for sync transforms where push(chunkA); push(chunkB); push(null)\n"
"       all happen in one tick. */\n"
"    if (s.flowing === true) return this;\n"
"    s.flowing = true;\n"
"    if (!this._reader) this._reader = this._web.getReader();\n"
"    var self = this;\n"
"    function loop() {\n"
"      if (!s.flowing || s.destroyed) return;\n"
"      self._reader.read().then(function (r) {\n"
"        if (r.done) {\n"
"          if (!s.ended) s.ended = true;\n"
"          self.emit('end');\n"
"          /* node: readableEnded is true AFTER 'end' has fired. */\n"
"          self.readableEnded = true;\n"
"          self.emit('close');\n"
"          return;\n"
"        }\n"
"        var v = r.value;\n"
"        if (self._encoding && v && (Buffer.isBuffer(v) || v instanceof Uint8Array)) {\n"
"          try { v = Buffer.isBuffer(v) ? v.toString(self._encoding) : Buffer.from(v).toString(self._encoding); }\n"
"          catch (_e) {}\n"
"        }\n"
"        self.emit('data', v);\n"
"        loop();\n"
"      }, function (e) { self.destroy(e); });\n"
"    }\n"
"    loop();\n"
"    return this;\n"
"  };\n"
"  Readable.prototype.setEncoding = function (enc) {\n"
"    this._encoding = enc;\n"
"    return this;\n"
"  };\n"
"  Readable.prototype.pause = function () {\n"
"    if (this._readableState) this._readableState.flowing = false;\n"
"    return this;\n"
"  };\n"
"  Readable.prototype.isPaused = function () {\n"
"    return !!(this._readableState && this._readableState.flowing === false);\n"
"  };\n"
"  Readable.prototype.destroy = function (err) {\n"
"    var s = this._readableState;\n"
"    if (!s) return this;\n"
"    if (s.destroyed) return this;\n"
"    s.destroyed = true;\n"
"    if (err) { s.errored = err; this.emit('error', err); }\n"
"    try { this._wctrl.error(err || new Error('destroyed')); } catch (_) {}\n"
"    this.emit('close');\n"
"    return this;\n"
"  };\n"
"  /* .pipe(dest) — wire 'data' / 'end' / 'error' → dest.write/.end. */\n"
"  Readable.prototype.pipe = function (dest, opts) {\n"
"    opts = opts || {};\n"
"    var self = this;\n"
"    var ended = false;\n"
"    function onData(chunk) {\n"
"      var ok = dest.write(chunk);\n"
"      if (ok === false) self.pause();\n"
"    }\n"
"    function onDrain() { self.resume(); }\n"
"    function onEnd() {\n"
"      if (ended) return;\n"
"      ended = true;\n"
"      if (opts['end'] !== false) dest.end();\n"
"    }\n"
"    function onError(e) {\n"
"      cleanup();\n"
"      dest.emit('error', e);\n"
"    }\n"
"    function cleanup() {\n"
"      self.removeListener('data',  onData);\n"
"      self.removeListener('end',   onEnd);\n"
"      self.removeListener('error', onError);\n"
"      dest.removeListener('drain', onDrain);\n"
"    }\n"
"    self.on('data', onData);\n"
"    self.on('end', onEnd);\n"
"    self.on('error', onError);\n"
"    dest.on('drain', onDrain);\n"
"    dest.emit('pipe', self);\n"
"    return dest;\n"
"  };\n"
"  Readable.prototype.unpipe = function (dest) {\n"
"    /* Best-effort — full unpipe bookkeeping isn't tracked. */\n"
"    this.pause();\n"
"    return this;\n"
"  };\n"
"  /* Readable.from(iterable) — async-iterable not supported (no\n"
"     async iterators in vanilla rampart); sync iterables (arrays,\n"
"     strings, anything with Symbol.iterator) work. */\n"
"  Readable.from = function (iterable, opts) {\n"
"    var arr;\n"
"    if (Array.isArray(iterable)) arr = iterable.slice();\n"
"    else if (typeof iterable === 'string') arr = [iterable];\n"
"    else if (iterable && typeof iterable.length === 'number') {\n"
"      arr = Array.prototype.slice.call(iterable);\n"
"    } else if (iterable && typeof Symbol !== 'undefined' && iterable[Symbol.iterator]) {\n"
"      arr = [];\n"
"      var it = iterable[Symbol.iterator](), step;\n"
"      while (!(step = it.next()).done) arr.push(step.value);\n"
"    } else {\n"
"      throw new TypeError('Readable.from: argument is not iterable');\n"
"    }\n"
"    var i = 0;\n"
"    return new Readable({\n"
"      read: function () {\n"
"        if (i >= arr.length) this.push(null);\n"
"        else this.push(arr[i++]);\n"
"      },\n"
"      objectMode: opts && opts.objectMode\n"
"    });\n"
"  };\n"
"  /* WHATWG interop */\n"
"  Readable.toWeb = function (nodeReadable) {\n"
"    if (nodeReadable && nodeReadable._web) return nodeReadable._web;\n"
"    /* Wrap any Node-style stream into a WHATWG ReadableStream */\n"
"    return new W_RS({\n"
"      start: function (ctrl) {\n"
"        nodeReadable.on('data', function (chunk) { ctrl.enqueue(chunk); });\n"
"        nodeReadable.on('end',  function () { try { ctrl.close(); } catch (_) {} });\n"
"        nodeReadable.on('error',function (e) { try { ctrl.error(e); } catch (_) {} });\n"
"      },\n"
"      cancel: function () { try { nodeReadable.destroy(); } catch (_) {} }\n"
"    });\n"
"  };\n"
"  Readable.fromWeb = function (webStream, opts) {\n"
"    var r = new Readable({\n"
"      objectMode: opts && opts.objectMode\n"
"    });\n"
"    var reader = webStream.getReader();\n"
"    function pump() {\n"
"      reader.read().then(function (x) {\n"
"        if (x.done) { r.push(null); return; }\n"
"        r.push(x.value);\n"
"        pump();\n"
"      }, function (e) { r.destroy(e); });\n"
"    }\n"
"    pump();\n"
"    return r;\n"
"  };\n"
"\n"
"  /* ---------- Writable -----------------------------------------\n"
"     Native Node-style state machine: write() invokes _writeImpl\n"
"     synchronously when no in-flight write is pending and cork is\n"
"     disengaged, so user-supplied write callbacks that fire sync\n"
"     are visible to the caller before write() returns (matches Node).\n"
"     Backpressure: returns false from write() when buffered bytes\n"
"     exceed highWaterMark; emits 'drain' once buffer empties.\n"
"     ._web is built lazily by Writable.toWeb. */\n"
"  function _initWritableState(self, opts) {\n"
"    self._writableState = {\n"
"      ended: false, finished: false, destroyed: false, errored: null,\n"
"      objectMode: !!opts.objectMode,\n"
"      highWaterMark: (opts.highWaterMark === 0 ? 0 : (opts.highWaterMark || 16384)),\n"
"      writing: false,\n"
"      corked: 0,\n"
"      buffer: [],\n"
"      bufferedLength: 0,\n"
"      needDrain: false,\n"
"      finalCalled: false\n"
"    };\n"
"    self.writable = true;\n"
"    self._writeImpl  = (typeof opts.write  === 'function') ? opts.write  : null;\n"
"    self._writevImpl = (typeof opts.writev === 'function') ? opts.writev : null;\n"
"    self._finalImpl  = (typeof opts.final  === 'function') ? opts.final  : null;\n"
"  }\n"
"  function _dispatchWrite(self, chunk, enc, cb) {\n"
"    var st = self._writableState;\n"
"    st.writing = true;\n"
"    var settled = false;\n"
"    /* The user-supplied write cb and downstream events fire on the\n"
"       next microtask — matches Node, where process.nextTick defers\n"
"       user callbacks even when _write completed synchronously. */\n"
"    function wrappedCb(err) {\n"
"      if (settled) return;\n"
"      settled = true;\n"
"      st.writing = false;\n"
"      Promise.resolve().then(function() {\n"
"        if (cb) {\n"
"          try { cb(err); } catch (e) { Promise.resolve().then(function(){ throw e; }); }\n"
"        }\n"
"        if (err) {\n"
"          if (!st.errored) st.errored = err;\n"
"          self.emit('error', err);\n"
"          return;\n"
"        }\n"
"        _processWriteBuffer(self);\n"
"      });\n"
"    }\n"
"    /* Honor both styles: opts.write at construction time (our shim's\n"
"       legacy), and `_write` on the prototype (Node's subclassing\n"
"       convention used by busboy, readable-stream, etc.). */\n"
"    var fn = self._writeImpl || (typeof self._write === 'function' ? self._write : null);\n"
"    if (!fn) { wrappedCb(); return; }\n"
"    try { fn.call(self, chunk, enc || 'utf8', wrappedCb); }\n"
"    catch (e) { wrappedCb(e); }\n"
"  }\n"
"  function _processWriteBuffer(self) {\n"
"    var st = self._writableState;\n"
"    if (st.writing || st.corked > 0) return;\n"
"    if (st.buffer.length > 0) {\n"
"      var next = st.buffer.shift();\n"
"      var sz = (next[0] && next[0].length) || 0;\n"
"      st.bufferedLength -= sz; if (st.bufferedLength < 0) st.bufferedLength = 0;\n"
"      _dispatchWrite(self, next[0], next[1], next[2]);\n"
"      return;\n"
"    }\n"
"    if (st.needDrain) {\n"
"      st.needDrain = false;\n"
"      Promise.resolve().then(function() { self.emit('drain'); });\n"
"    }\n"
"    if (st.ended && !st.finalCalled && !st.finished) {\n"
"      st.finalCalled = true;\n"
"      var finishedDone = function(err) {\n"
"        if (err) {\n"
"          st.errored = err;\n"
"          self.emit('error', err);\n"
"          self.emit('close');\n"
"          return;\n"
"        }\n"
"        st.finished = true;\n"
"        /* Duplex half-open: push null to readable side if not allowHalfOpen */\n"
"        if (self.allowHalfOpen === false && typeof self.push === 'function') {\n"
"          try { self.push(null); } catch (_) {}\n"
"        }\n"
"        self.emit('finish');\n"
"        self.emit('close');\n"
"      };\n"
"      var finalFn = self._finalImpl ||\n"
"        (typeof self._final === 'function' ? self._final : null);\n"
"      if (finalFn) {\n"
"        try { finalFn.call(self, finishedDone); }\n"
"        catch (e) { finishedDone(e); }\n"
"      } else {\n"
"        finishedDone();\n"
"      }\n"
"    }\n"
"  }\n"
"  function Writable(opts) {\n"
"    if (!(this instanceof Writable)) return new Writable(opts);\n"
"    EventEmitter.call(this);\n"
"    _initWritableState(this, opts || {});\n"
"  }\n"
"  Writable.prototype = Object.create(EventEmitter.prototype);\n"
"  Writable.prototype.constructor = Writable;\n"
"  Writable.prototype.write = function (chunk, encoding, cb) {\n"
"    if (typeof encoding === 'function') { cb = encoding; encoding = null; }\n"
"    var st = this._writableState;\n"
"    if (st.ended) {\n"
"      var err = new Error('write after end');\n"
"      if (cb) cb(err); else this.emit('error', err);\n"
"      return false;\n"
"    }\n"
"    var sz = (chunk && chunk.length) || 0;\n"
"    if (st.writing || st.corked > 0) {\n"
"      st.buffer.push([chunk, encoding, cb]);\n"
"      st.bufferedLength += sz;\n"
"      var ret = st.bufferedLength < st.highWaterMark;\n"
"      if (!ret) st.needDrain = true;\n"
"      return ret;\n"
"    }\n"
"    _dispatchWrite(this, chunk, encoding, cb);\n"
"    /* If the impl's cb fired synchronously, writing is false and buffer\n"
"       length is unchanged (0); otherwise we just have one in-flight. */\n"
"    var ret2 = st.bufferedLength < st.highWaterMark;\n"
"    if (!ret2) st.needDrain = true;\n"
"    return ret2;\n"
"  };\n"
"  Writable.prototype.end = function (chunk, encoding, cb) {\n"
"    if (typeof chunk === 'function') { cb = chunk; chunk = null; encoding = null; }\n"
"    else if (typeof encoding === 'function') { cb = encoding; encoding = null; }\n"
"    var self = this;\n"
"    var st = this._writableState;\n"
"    if (cb) {\n"
"      var fired = false;\n"
"      var endCb = function(err) { if (fired) return; fired = true; cb(err); };\n"
"      this.once('finish', endCb);\n"
"      this.once('error',  endCb);\n"
"    }\n"
"    if (chunk != null && chunk !== undefined) {\n"
"      this.write(chunk, encoding);\n"
"    }\n"
"    if (st.ended) return this;\n"
"    st.ended = true;\n"
"    /* Trigger the buffer-drain path so _finalImpl fires when ready. */\n"
"    _processWriteBuffer(this);\n"
"    return this;\n"
"  };\n"
"  Writable.prototype.destroy = function (err) {\n"
"    var s = this._writableState;\n"
"    if (s.destroyed) return this;\n"
"    s.destroyed = true;\n"
"    if (err) { s.errored = err; this.emit('error', err); }\n"
"    this.emit('close');\n"
"    return this;\n"
"  };\n"
"  Writable.prototype.cork    = function () { this._writableState.corked++; return this; };\n"
"  Writable.prototype.uncork  = function () {\n"
"    var st = this._writableState;\n"
"    if (st.corked > 0) {\n"
"      st.corked--;\n"
"      if (st.corked === 0) _processWriteBuffer(this);\n"
"    }\n"
"    return this;\n"
"  };\n"
"  Writable.prototype.setDefaultEncoding = function () { return this; };\n"
"  /* WHATWG interop: ._web is built lazily on first toWeb() access. */\n"
"  Writable.toWeb = function (nodeWritable) {\n"
"    if (nodeWritable && nodeWritable._web) return nodeWritable._web;\n"
"    var ws = new W_WS({\n"
"      write: function (chunk) {\n"
"        return new Promise(function (resolve, reject) {\n"
"          nodeWritable.write(chunk, function (err) {\n"
"            if (err) reject(err); else resolve();\n"
"          });\n"
"        });\n"
"      },\n"
"      close: function () {\n"
"        return new Promise(function (resolve) {\n"
"          nodeWritable.end(function () { resolve(); });\n"
"        });\n"
"      },\n"
"      abort: function (err) {\n"
"        try { nodeWritable.destroy(err); } catch (_) {}\n"
"      }\n"
"    });\n"
"    if (nodeWritable) {\n"
"      try { Object.defineProperty(nodeWritable, '_web', { value: ws, writable: true, configurable: true }); }\n"
"      catch (_) { nodeWritable._web = ws; }\n"
"    }\n"
"    return ws;\n"
"  };\n"
"  Writable.fromWeb = function (webStream, opts) {\n"
"    var writer = webStream.getWriter();\n"
"    return new Writable({\n"
"      write: function (chunk, enc, cb) {\n"
"        writer.write(chunk).then(function () { cb(); }, function (e) { cb(e); });\n"
"      },\n"
"      final: function (cb) {\n"
"        writer.close().then(function () { cb(); }, function (e) { cb(e); });\n"
"      },\n"
"      objectMode: opts && opts.objectMode\n"
"    });\n"
"  };\n"
"\n"
"  /* ---------- Duplex / Transform / PassThrough -----------------\n"
"     Duplex is both Readable and Writable.  Transform's _transform\n"
"     callback writes to its own Readable side via this.push().\n"
"     PassThrough is identity Transform. */\n"
"  function Duplex(opts) {\n"
"    if (!(this instanceof Duplex)) return new Duplex(opts);\n"
"    Readable.call(this, opts);\n"
"    opts = opts || {};\n"
"    /* Half-open: when false, end()'s finish path pushes null on the\n"
"       readable side too.  _processWriteBuffer's finalize step checks\n"
"       this.allowHalfOpen === false explicitly. */\n"
"    this.allowHalfOpen = opts.allowHalfOpen !== false;\n"
"    _initWritableState(this, opts);\n"
"  }\n"
"  Duplex.prototype = Object.create(Readable.prototype);\n"
"  Duplex.prototype.constructor = Duplex;\n"
"  Duplex.prototype.write    = Writable.prototype.write;\n"
"  Duplex.prototype.end      = Writable.prototype.end;\n"
"  Duplex.prototype.cork     = Writable.prototype.cork;\n"
"  Duplex.prototype.uncork   = Writable.prototype.uncork;\n"
"  Duplex.prototype.setDefaultEncoding = Writable.prototype.setDefaultEncoding;\n"
"\n"
"  function Transform(opts) {\n"
"    if (!(this instanceof Transform)) return new Transform(opts);\n"
"    opts = opts || {};\n"
"    var self = this;\n"
"    /* Build with a default 'write' that calls _transform */\n"
"    var userTransform = opts.transform;\n"
"    var userFlush     = opts.flush;\n"
"    Duplex.call(this, {\n"
"      objectMode:    opts.objectMode,\n"
"      highWaterMark: opts.highWaterMark,\n"
"      write: function (chunk, encoding, cb) {\n"
"        var t = self._transform || userTransform;\n"
"        if (!t) return cb();\n"
"        try {\n"
"          t.call(self, chunk, encoding, function (err, out) {\n"
"            if (err) return cb(err);\n"
"            if (out !== undefined && out !== null) self.push(out);\n"
"            cb();\n"
"          });\n"
"        } catch (e) { cb(e); }\n"
"      },\n"
"      final: function (cb) {\n"
"        var f = self._flush || userFlush;\n"
"        if (!f) { self.push(null); return cb(); }\n"
"        try {\n"
"          f.call(self, function (err, out) {\n"
"            if (err) return cb(err);\n"
"            if (out !== undefined && out !== null) self.push(out);\n"
"            self.push(null);\n"
"            cb();\n"
"          });\n"
"        } catch (e) { cb(e); }\n"
"      }\n"
"    });\n"
"  }\n"
"  Transform.prototype = Object.create(Duplex.prototype);\n"
"  Transform.prototype.constructor = Transform;\n"
"\n"
"  function PassThrough(opts) {\n"
"    if (!(this instanceof PassThrough)) return new PassThrough(opts);\n"
"    opts = opts || {};\n"
"    opts.transform = function (chunk, enc, cb) { cb(null, chunk); };\n"
"    Transform.call(this, opts);\n"
"  }\n"
"  PassThrough.prototype = Object.create(Transform.prototype);\n"
"  PassThrough.prototype.constructor = PassThrough;\n"
"\n"
"  /* ---------- pipeline / finished -------------------------------- */\n"
"  function pipeline() {\n"
"    var streams = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof streams[streams.length - 1] === 'function') ? streams.pop() : null;\n"
"    if (streams.length < 2) {\n"
"      var e = new TypeError('pipeline requires at least 2 streams');\n"
"      if (cb) cb(e); else throw e;\n"
"      return;\n"
"    }\n"
"    var dest = streams[streams.length - 1];\n"
"    var errored = false;\n"
"    function onErr(e) {\n"
"      if (errored) return;\n"
"      errored = true;\n"
"      for (var i = 0; i < streams.length; i++) {\n"
"        try { streams[i].destroy && streams[i].destroy(e); } catch (_) {}\n"
"      }\n"
"      if (cb) cb(e);\n"
"    }\n"
"    for (var i = 0; i < streams.length; i++)\n"
"      streams[i].on('error', onErr);\n"
"    /* Chain pipes */\n"
"    var cur = streams[0];\n"
"    for (var k = 1; k < streams.length; k++) cur = cur.pipe(streams[k]);\n"
"    dest.on('finish', function () { if (!errored && cb) cb(); });\n"
"    dest.on('end',    function () { if (!errored && cb) cb(); });\n"
"    return dest;\n"
"  }\n"
"  function finished(stream, optsOrCb, cb) {\n"
"    if (typeof optsOrCb === 'function') { cb = optsOrCb; }\n"
"    var fired = false;\n"
"    function fire(err) {\n"
"      if (fired) return;\n"
"      fired = true;\n"
"      if (cb) cb(err);\n"
"    }\n"
"    stream.on('end',    function () { fire(); });\n"
"    stream.on('finish', function () { fire(); });\n"
"    stream.on('close',  function () { fire(); });\n"
"    stream.on('error',  function (e) { fire(e); });\n"
"  }\n"
"\n"
"  /* Per Node: `require('stream')` returns the legacy `Stream` base\n"
"     class (extends EventEmitter), with Readable/Writable/etc. and\n"
"     the helpers attached as static properties.  Code like\n"
"     `util.inherits(MyStream, require('stream'))` (used by send,\n"
"     content-disposition, many npm deps) needs the module to be a\n"
"     callable with a real .prototype. */\n"
"  function Stream() { EventEmitter.call(this); }\n"
"  Stream.prototype = Object.create(EventEmitter.prototype);\n"
"  Stream.prototype.constructor = Stream;\n"
"  /* Legacy Stream.prototype.pipe — most callers will subclass via\n"
"     Readable/Writable which have their own pipe; this is the bare\n"
"     legacy event-only version. */\n"
"  Stream.prototype.pipe = function(dest, opts) {\n"
"    var self = this;\n"
"    opts = opts || {};\n"
"    function onData(c) { if (dest.write && dest.write(c) === false) self.pause && self.pause(); }\n"
"    function onDrain() { self.resume && self.resume(); }\n"
"    function onEnd() { if (opts.end !== false && dest.end) dest.end(); }\n"
"    self.on('data', onData);\n"
"    self.on('end',  onEnd);\n"
"    dest.on && dest.on('drain', onDrain);\n"
"    dest.emit && dest.emit('pipe', self);\n"
"    return dest;\n"
"  };\n"
"  Stream.Stream      = Stream;\n"
"  Stream.Readable    = Readable;\n"
"  Stream.Writable    = Writable;\n"
"  Stream.Duplex      = Duplex;\n"
"  Stream.Transform   = Transform;\n"
"  Stream.PassThrough = PassThrough;\n"
"  Stream.pipeline    = pipeline;\n"
"  Stream.finished    = finished;\n"
"  Stream.promises = {\n"
"    pipeline: function () {\n"
"      var args = Array.prototype.slice.call(arguments);\n"
"      return new Promise(function (resolve, reject) {\n"
"        args.push(function (err) { if (err) reject(err); else resolve(); });\n"
"        pipeline.apply(null, args);\n"
"      });\n"
"    },\n"
"    finished: function (stream, opts) {\n"
"      return new Promise(function (resolve, reject) {\n"
"        finished(stream, function (err) { if (err) reject(err); else resolve(); });\n"
"      });\n"
"    }\n"
"  };\n"
"  return Stream;\n"
"}";

static void nodeshim_init_stream(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:stream_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, stream_js);
    /* Pass EventEmitter from the partially-built top-level exports object —
       same pattern as worker_threads.  Slot order in duk_open_module
       puts 'events' before 'stream'. */
    duk_get_prop_string(ctx, -2, "events");
    duk_call(ctx, 1);
}

/* ============================================================
 * module — subset of node's `module` module.  Exposes
 * builtinModules, isBuiltin, createRequire, wrap.
 * Skips compile-cache, ESM register, sourcemaps, TS stripping.
 * ============================================================ */
static const char *module_js =
"function() {\n"
"  'use strict';\n"
"  /* The list of things require() can find via js_modules/*.js. */\n"
"  var builtinModules = [\n"
"    'assert','buffer','console','crypto','dns','events','fs','http',\n"
"    'https','module','net','os','path','perf_hooks','process','punycode',\n"
"    'querystring','stream','string_decoder','timers','tty','url',\n"
"    'util','worker_threads','zlib'\n"
"  ];\n"
"  function isBuiltin(name) {\n"
"    if (typeof name !== 'string') return false;\n"
"    var n = name.indexOf(':') === 0 ? name : name.replace(/^node:/, '');\n"
"    return builtinModules.indexOf(n) >= 0;\n"
"  }\n"
"  /* createRequire(filename) -- returns a require() bound to filename's\n"
"     directory.  rampart's require is path-aware via its own resolver,\n"
"     so we return the global require here -- it does the right thing\n"
"     either way for our use case. */\n"
"  function createRequire(filename) {\n"
"    return require;\n"
"  }\n"
"  /* module.wrap(source) -- the CJS wrapper.  Deprecated but trivial. */\n"
"  var wrapper = [\n"
"    '(function (exports, require, module, __filename, __dirname) { ',\n"
"    '\\n});'\n"
"  ];\n"
"  function wrap(source) {\n"
"    return wrapper[0] + source + wrapper[1];\n"
"  }\n"
"  function Module(id, parent) {\n"
"    this.id = id || '';\n"
"    this.exports = {};\n"
"    this.parent  = parent || null;\n"
"    this.filename = null;\n"
"    this.loaded = false;\n"
"    this.children = [];\n"
"    this.paths = [];\n"
"  }\n"
"  Module.builtinModules = builtinModules;\n"
"  Module.createRequire  = createRequire;\n"
"  Module.isBuiltin      = isBuiltin;\n"
"  Module.wrap           = wrap;\n"
"  Module.wrapper        = wrapper;\n"
"  /* The module module itself returns the Module class with statics; the\n"
"     module.X helpers are also exposed directly as conveniences. */\n"
"  Module.builtinModules = builtinModules;\n"
"  Module.createRequire  = createRequire;\n"
"  Module.isBuiltin      = isBuiltin;\n"
"  Module.wrap           = wrap;\n"
"  return Module;\n"
"}";
static void nodeshim_init_module(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:module_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, module_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * console — node's `console` module.  Exposes the Console class for
 * libraries that want their own instance (`const { Console } =
 * require('console')`).  Methods reuse the global console (which has
 * been augmented by rampart-console.c with time/table/group/etc.).
 * ============================================================ */
static const char *console_js =
"function() {\n"
"  'use strict';\n"
"  /* Console(options) -- node accepts {stdout, stderr, ignoreErrors,\n"
"     colorMode, inspectOptions, groupIndentation}.  Our impl honors\n"
"     stdout/stderr -- write to them via .write() -- and ignores the rest.\n"
"     If stdout is missing, falls back to the global console.log. */\n"
"  function Console(opts, maybeStderr) {\n"
"    if (!(this instanceof Console)) return new Console(opts, maybeStderr);\n"
"    /* Legacy positional form: new Console(stdoutStream, stderrStream).\n"
"       Detect by checking if the first arg looks like a stream (has\n"
"       .write) rather than a plain options object. */\n"
"    if (opts && typeof opts.write === 'function') {\n"
"      opts = {stdout: opts, stderr: maybeStderr || opts};\n"
"    } else {\n"
"      opts = opts || {};\n"
"    }\n"
"    var stdout = opts.stdout || (typeof process !== 'undefined' && process.stdout);\n"
"    var stderr = opts.stderr || (typeof process !== 'undefined' && process.stderr) || stdout;\n"
"    var self = this;\n"
"    function _write(stream, args) {\n"
"      var msg = Array.prototype.map.call(args, function(v) {\n"
"        if (typeof v === 'string') return v;\n"
"        try { return JSON.stringify(v); } catch (e) { return String(v); }\n"
"      }).join(' ');\n"
"      if (stream && typeof stream.write === 'function') stream.write(msg + '\\n');\n"
"      else console.log(msg);\n"
"    }\n"
"    self.log   = function() { _write(stdout, arguments); };\n"
"    self.info  = self.log;\n"
"    self.debug = self.log;\n"
"    self.error = function() { _write(stderr, arguments); };\n"
"    self.warn  = self.error;\n"
"    self.trace = self.error;\n"
"    self.dir   = self.log;\n"
"    self.assert = function(cond /*, ...msg */) {\n"
"      if (!cond) {\n"
"        var args = ['Assertion failed:'];\n"
"        for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);\n"
"        _write(stderr, args);\n"
"      }\n"
"    };\n"
"    /* The rest (time/timeEnd/count/group/table/clear) -- delegate to the\n"
"       global console.  Each Console instance shares the same timer/counter\n"
"       state; this is a known node deviation we don't bother isolating. */\n"
"    ['time','timeEnd','timeLog','count','countReset','group','groupEnd',\n"
"     'groupCollapsed','clear','table'].forEach(function(name) {\n"
"      self[name] = function() { return console[name].apply(console, arguments); };\n"
"    });\n"
"  }\n"
"  /* The module's default export is the GLOBAL console, with a .Console\n"
"     class attached as a property. */\n"
"  var mod = console;\n"
"  /* Don't overwrite if global console already has a .Console (defensive). */\n"
"  if (typeof mod.Console !== 'function') {\n"
"    try { Object.defineProperty(mod, 'Console', {value: Console, writable: true, configurable: true}); }\n"
"    catch (e) { mod.Console = Console; }\n"
"  }\n"
"  return mod;\n"
"}";
static void nodeshim_init_console(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:console_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, console_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * perf_hooks — node's `perf_hooks` module.  Full mark/measure surface
 * on top of duktape's `performance.now()` global.
 * ============================================================ */
/* perf_hooks — Node module shape.  The W3C Performance Timeline
 * (mark, measure, getEntriesByX, clearMarks, clearMeasures, timeOrigin
 * and the PerformanceEntry / PerformanceMark / PerformanceMeasure
 * classes) now lives in rampart-whatwg.so and augments
 * globalThis.performance there.  This module just re-exposes those
 * via getters, plus Node-only extras (eventLoopUtilization,
 * nodeTiming stubs, GC constants). */
static const char *perf_hooks_js =
"function() {\n"
"  'use strict';\n"
"  var _out = {\n"
"    constants: { NODE_PERFORMANCE_GC_MAJOR: 2, NODE_PERFORMANCE_GC_MINOR: 1,\n"
"                 NODE_PERFORMANCE_GC_INCREMENTAL: 4, NODE_PERFORMANCE_GC_WEAKCB: 8 }\n"
"  };\n"
"  /* All Performance Timeline names resolved lazily via globalThis —\n"
"     by the time perf_hooks.performance is accessed by user code,\n"
"     rampart-whatwg has loaded and installed the augmentations. */\n"
"  Object.defineProperty(_out, 'performance', {\n"
"    get: function() {\n"
"      var p = globalThis.performance;\n"
"      /* Layer on Node-only extras (these are NOT W3C). */\n"
"      if (p && typeof p.eventLoopUtilization !== 'function') {\n"
"        p.eventLoopUtilization = function() {\n"
"          return { idle: 0, active: 0, utilization: 0 };\n"
"        };\n"
"      }\n"
"      if (p && p.nodeTiming === undefined) p.nodeTiming = {};\n"
"      return p;\n"
"    },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"  ['PerformanceEntry','PerformanceMark','PerformanceMeasure'].forEach(function (n) {\n"
"    Object.defineProperty(_out, n, {\n"
"      get: function() { return globalThis.performance && globalThis.performance[n]; },\n"
"      configurable: true, enumerable: true\n"
"    });\n"
"  });\n"
"  return _out;\n"
"}";
static void nodeshim_init_perf_hooks(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:perf_hooks_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, perf_hooks_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * dns — wraps rampart.net.Resolver into node's dns API.
 * Supports lookup, resolve, resolve4/6/Mx/Txt/Ns/Cname/Srv/Ptr, reverse,
 * Resolver class, dns.promises (only if Promise available).
 * ============================================================ */
static const char *dns_js =
"function() {\n"
"  'use strict';\n"
"  var net;\n"
"  try { net = require('rampart-net'); } catch (e) { net = null; }\n"
"\n"
"  function _need() {\n"
"    if (!net || !net.Resolver) throw new Error('dns: rampart-net is required for DNS resolution');\n"
"  }\n"
"\n"
"  function _err(host, code, syscall, msg) {\n"
"    var e = new Error((code || 'ENOTFOUND') + ': ' + (msg || 'dns lookup failed') + ' ' + host);\n"
"    e.code = code || 'ENOTFOUND';\n"
"    e.syscall = syscall || 'getaddrinfo';\n"
"    e.hostname = host;\n"
"    return e;\n"
"  }\n"
"\n"
"  /* Cheap literal-IP detector — covers the common 'lookup(\"127.0.0.1\")'\n"
"     case without touching DNS, matching node's behavior. */\n"
"  function _isIPv4(s) { return /^(\\d{1,3}\\.){3}\\d{1,3}$/.test(s); }\n"
"  function _isIPv6(s) { return s.indexOf(':') >= 0 && /^[0-9a-fA-F:.]+$/.test(s); }\n"
"\n"
"  function lookup(hostname, options, cb) {\n"
"    if (typeof options === 'function') { cb = options; options = {}; }\n"
"    options = options || {};\n"
"    var family = options.family || 0;\n"
"    var all = !!options.all;\n"
"    /* Literal-IP fast path: callback async on next tick to preserve\n"
"       node's contract that lookup() never invokes the callback\n"
"       synchronously. */\n"
"    var lit = null;\n"
"    if (_isIPv4(hostname)) lit = {address: hostname, family: 4};\n"
"    else if (_isIPv6(hostname)) lit = {address: hostname, family: 6};\n"
"    if (lit) {\n"
"      if (family && lit.family !== family) {\n"
"        setImmediate(function() { cb(_err(hostname, 'ENOTFOUND')); });\n"
"        return;\n"
"      }\n"
"      setImmediate(function() {\n"
"        if (all) cb(null, [lit]);\n"
"        else cb(null, lit.address, lit.family);\n"
"      });\n"
"      return;\n"
"    }\n"
"    _need();\n"
"    var r = new net.Resolver();\n"
"    r.resolve(hostname, function(err, result) {\n"
"      if (err) return cb(_err(hostname, 'ENOTFOUND', 'getaddrinfo', err.message || String(err)));\n"
"      /* result is typically an object with .address etc.; normalize */\n"
"      var list = [];\n"
"      if (Array.isArray(result)) list = result;\n"
"      else if (result && result.address) list = [result];\n"
"      else if (result && result.addresses) list = result.addresses.map(function(a) {\n"
"        return {address: a, family: a.indexOf(':') >= 0 ? 6 : 4};\n"
"      });\n"
"      if (family) list = list.filter(function(x) { return x.family === family; });\n"
"      if (!list.length) return cb(_err(hostname, 'ENOTFOUND'));\n"
"      if (all) cb(null, list);\n"
"      else cb(null, list[0].address, list[0].family);\n"
"    });\n"
"  }\n"
"\n"
"  function resolve(hostname, rrtype, cb) {\n"
"    if (typeof rrtype === 'function') { cb = rrtype; rrtype = 'A'; }\n"
"    _need();\n"
"    var r = new net.Resolver();\n"
"    try {\n"
"      r.resolve(hostname, rrtype, function(err, result) {\n"
"        if (err) return cb(_err(hostname, 'ENOTFOUND', 'queryA', err.message));\n"
"        var arr = Array.isArray(result) ? result\n"
"                : (result && result.addresses) ? result.addresses\n"
"                : (result && result.address) ? [result.address]\n"
"                : [];\n"
"        cb(null, arr);\n"
"      });\n"
"    } catch (e) { cb(_err(hostname, 'ENOTFOUND', 'queryA', e.message)); }\n"
"  }\n"
"\n"
"  function _typed(type) {\n"
"    return function(hostname, cb) { return resolve(hostname, type, cb); };\n"
"  }\n"
"  var resolve4    = _typed('A');\n"
"  var resolve6    = _typed('AAAA');\n"
"  var resolveMx   = _typed('MX');\n"
"  var resolveTxt  = _typed('TXT');\n"
"  var resolveNs   = _typed('NS');\n"
"  var resolveCname= _typed('CNAME');\n"
"  var resolveSrv  = _typed('SRV');\n"
"  var resolvePtr  = _typed('PTR');\n"
"\n"
"  function reverse(ip, cb) {\n"
"    resolve(ip, 'PTR', cb);\n"
"  }\n"
"\n"
"  function Resolver(opts) {\n"
"    if (!(this instanceof Resolver)) return new Resolver(opts);\n"
"    _need();\n"
"    this._r = new net.Resolver();\n"
"  }\n"
"  Resolver.prototype.lookup   = function(h, o, cb) { return lookup(h, o, cb); };\n"
"  Resolver.prototype.resolve  = function(h, t, cb) { return resolve(h, t, cb); };\n"
"  Resolver.prototype.resolve4 = resolve4;\n"
"  Resolver.prototype.resolve6 = resolve6;\n"
"  Resolver.prototype.resolveMx    = resolveMx;\n"
"  Resolver.prototype.resolveTxt   = resolveTxt;\n"
"  Resolver.prototype.resolveNs    = resolveNs;\n"
"  Resolver.prototype.resolveCname = resolveCname;\n"
"  Resolver.prototype.resolveSrv   = resolveSrv;\n"
"  Resolver.prototype.resolvePtr   = resolvePtr;\n"
"  Resolver.prototype.reverse  = reverse;\n"
"  Resolver.prototype.cancel   = function() {};   /* no-op stub */\n"
"  Resolver.prototype.setServers = function() {}; /* no-op */\n"
"\n"
"  /* dns.promises mirror (only if Promise is available) */\n"
"  var promises = null;\n"
"  function _promiseify(fn) {\n"
"    return function() {\n"
"      var P = (typeof Promise !== 'undefined') ? Promise : (typeof global !== 'undefined' && global.Promise);\n"
"      if (!P) throw new Error('dns.promises requires Promise');\n"
"      var args = Array.prototype.slice.call(arguments);\n"
"      return new P(function(resolve, reject) {\n"
"        args.push(function(err, res) { if (err) reject(err); else resolve(res); });\n"
"        fn.apply(null, args);\n"
"      });\n"
"    };\n"
"  }\n"
"  promises = {\n"
"    lookup: _promiseify(lookup), resolve: _promiseify(resolve),\n"
"    resolve4: _promiseify(resolve4), resolve6: _promiseify(resolve6),\n"
"    resolveMx: _promiseify(resolveMx), resolveTxt: _promiseify(resolveTxt),\n"
"    resolveNs: _promiseify(resolveNs), resolveCname: _promiseify(resolveCname),\n"
"    resolveSrv: _promiseify(resolveSrv), resolvePtr: _promiseify(resolvePtr),\n"
"    reverse: _promiseify(reverse),\n"
"    Resolver: Resolver\n"
"  };\n"
"\n"
"  return {\n"
"    lookup: lookup, resolve: resolve,\n"
"    resolve4: resolve4, resolve6: resolve6,\n"
"    resolveMx: resolveMx, resolveTxt: resolveTxt,\n"
"    resolveNs: resolveNs, resolveCname: resolveCname,\n"
"    resolveSrv: resolveSrv, resolvePtr: resolvePtr,\n"
"    reverse: reverse,\n"
"    Resolver: Resolver, promises: promises,\n"
"    /* getaddrinfo hint constants (numeric, matching node's libuv values) */\n"
"    ADDRCONFIG: 0x20, V4MAPPED: 0x08, ALL: 0x10,\n"
"    /* constants */\n"
"    NODATA: 'ENODATA', FORMERR: 'EFORMERR', SERVFAIL: 'ESERVFAIL',\n"
"    NOTFOUND: 'ENOTFOUND', NOTIMP: 'ENOTIMP', REFUSED: 'EREFUSED',\n"
"    BADQUERY: 'EBADQUERY', BADNAME: 'EBADNAME', BADFAMILY: 'EBADFAMILY',\n"
"    BADRESP: 'EBADRESP', CONNREFUSED: 'ECONNREFUSED', TIMEOUT: 'ETIMEOUT',\n"
"    EOF: 'EOF', FILE: 'EFILE', NOMEM: 'ENOMEM', DESTRUCTION: 'EDESTRUCTION',\n"
"    BADSTR: 'EBADSTR', BADFLAGS: 'EBADFLAGS', NONAME: 'ENONAME',\n"
"    BADHINTS: 'EBADHINTS', NOTINITIALIZED: 'ENOTINITIALIZED',\n"
"    LOADIPHLPAPI: 'ELOADIPHLPAPI', ADDRGETNETWORKPARAMS: 'EADDRGETNETWORKPARAMS',\n"
"    CANCELLED: 'ECANCELLED'\n"
"  };\n"
"}";
static void nodeshim_init_dns(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:dns_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, dns_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * zlib — wraps rampart.utils.gzip/gunzip/deflate/inflate/... (which
 * are libdeflate-backed and live in fs-extras.c) with node's API
 * surface.  All sync forms work directly; callback forms defer via
 * setTimeout 0 (no thread pool).  Stream classes throw ENOSYS (would
 * need real `stream` module).  Brotli/Zstd throw ENOSYS (different
 * algorithms; libdeflate doesn't cover).
 * ============================================================ */
static const char *zlib_js =
"function() {\n"
"  'use strict';\n"
"  var u = rampart.utils;\n"
"\n"
"  function _normData(data) {\n"
"    if (Buffer.isBuffer(data) || (data instanceof Uint8Array)) return data;\n"
"    if (typeof data === 'string') return data;\n"
"    return Buffer.from(data);\n"
"  }\n"
"  function _level(opts) {\n"
"    if (opts && typeof opts === 'object') {\n"
"      if (typeof opts.level === 'number') return opts.level;\n"
"      if (typeof opts.compressionLevel === 'number') return opts.compressionLevel;\n"
"    }\n"
"    return 6;\n"
"  }\n"
"\n"
"  function gzipSync(data, opts)    { return u.gzip(_normData(data),    _level(opts)); }\n"
"  function gunzipSync(data, opts)  { return u.gunzip(_normData(data)); }\n"
"  function deflateSync(data, opts) { return u.deflate(_normData(data), _level(opts)); }\n"
"  function inflateSync(data, opts) { return u.inflate(_normData(data)); }\n"
"  function deflateRawSync(data, opts) { return u.deflateRaw(_normData(data), _level(opts)); }\n"
"  function inflateRawSync(data, opts) { return u.inflateRaw(_normData(data)); }\n"
"\n"
"  /* unzip / unzipSync -- auto-detect: gzip starts with 0x1F 0x8B, else\n"
"     assumed zlib. */\n"
"  function unzipSync(data, opts) {\n"
"    var buf = _normData(data);\n"
"    if (typeof buf === 'string') buf = Buffer.from(buf, 'binary');\n"
"    if (buf.length >= 2 && buf[0] === 0x1F && buf[1] === 0x8B)\n"
"      return u.gunzip(buf);\n"
"    return u.inflate(buf);\n"
"  }\n"
"\n"
"  /* Async callback variants -- (data, [opts,] cb) */\n"
"  function _makeAsync(syncFn) {\n"
"    return function(data, optsOrCb, maybeCb) {\n"
"      var cb = (typeof maybeCb === 'function') ? maybeCb\n"
"             : (typeof optsOrCb === 'function' ? optsOrCb : null);\n"
"      var opts = (typeof optsOrCb === 'object' && optsOrCb !== null) ? optsOrCb : null;\n"
"      if (typeof cb !== 'function') {\n"
"        var e = new TypeError('callback must be a function');\n"
"        e.code = 'ERR_INVALID_ARG_TYPE'; throw e;\n"
"      }\n"
"      setTimeout(function() {\n"
"        try { cb(null, syncFn(data, opts)); }\n"
"        catch (e) { cb(e); }\n"
"      }, 0);\n"
"    };\n"
"  }\n"
"\n"
"  /* Streaming Gzip/Deflate/etc. classes backed by WHATWG\n"
"     CompressionStream / DecompressionStream (real streaming libz via\n"
"     rampart-whatwg.so) wrapped in Node stream.Transform. */\n"
"  function _zlibStream(format, decomp) {\n"
"    if (typeof CompressionStream === 'undefined' || typeof DecompressionStream === 'undefined') {\n"
"      var e = new Error('zlib stream classes require WHATWG CompressionStream (rampart-whatwg)');\n"
"      e.code = 'ERR_NOT_SUPPORTED'; throw e;\n"
"    }\n"
"    var stream = require('stream');\n"
"    var Ctor = decomp ? DecompressionStream : CompressionStream;\n"
"    var cs;\n"
"    try { cs = new Ctor(format); }\n"
"    catch (err) {\n"
"      var e2 = new Error('zlib: unsupported format ' + format + ': ' + err.message);\n"
"      e2.code = 'ERR_NOT_SUPPORTED'; throw e2;\n"
"    }\n"
"    var writer = cs.writable.getWriter();\n"
"    var reader = cs.readable.getReader();\n"
"    var pumpDone, pumpErr = null;\n"
"    var pumpPromise = new Promise(function(r) { pumpDone = r; });\n"
"    var t = new stream.Transform({\n"
"      transform: function(chunk, enc, cb) {\n"
"        try {\n"
"          var data = Buffer.isBuffer(chunk) ? chunk\n"
"                   : (typeof chunk === 'string' ? Buffer.from(chunk, enc)\n"
"                                                : Buffer.from(chunk));\n"
"          var u8 = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);\n"
"        } catch (e) { cb(e); return; }\n"
"        writer.ready.then(function() {\n"
"          return writer.write(u8);\n"
"        }).then(function() { cb(); }, function(err) { cb(err); });\n"
"      },\n"
"      flush: function(cb) {\n"
"        writer.close().then(function() { return pumpPromise; })\n"
"          .then(function() { if (pumpErr) cb(pumpErr); else cb(); },\n"
"                function(err) { cb(err); });\n"
"      }\n"
"    });\n"
"    (function pump() {\n"
"      reader.read().then(function(res) {\n"
"        if (res.done) { pumpDone(); return; }\n"
"        try { t.push(Buffer.from(res.value)); } catch (e) { pumpErr = e; pumpDone(); return; }\n"
"        pump();\n"
"      }, function(err) { pumpErr = err; pumpDone(); });\n"
"    })();\n"
"    return t;\n"
"  }\n"
"  /* unzip is auto-detect: gzip if first bytes are 0x1F 0x8B, else inflate.\n"
"     Buffer up to the first 2 bytes before dispatching to a real stream. */\n"
"  function _unzipStream() {\n"
"    if (typeof DecompressionStream === 'undefined') {\n"
"      var e = new Error('zlib.Unzip requires WHATWG DecompressionStream');\n"
"      e.code = 'ERR_NOT_SUPPORTED'; throw e;\n"
"    }\n"
"    var stream = require('stream');\n"
"    var inner = null;\n"
"    var pending = [];\n"
"    var t = new stream.Transform({\n"
"      transform: function(chunk, enc, cb) {\n"
"        var data = Buffer.isBuffer(chunk) ? chunk\n"
"                 : (typeof chunk === 'string' ? Buffer.from(chunk, enc) : Buffer.from(chunk));\n"
"        if (!inner) {\n"
"          pending.push(data);\n"
"          var total = 0;\n"
"          for (var i = 0; i < pending.length; i++) total += pending[i].length;\n"
"          if (total < 2) { cb(); return; }\n"
"          var merged = Buffer.concat(pending);\n"
"          pending = null;\n"
"          var fmt = (merged[0] === 0x1F && merged[1] === 0x8B) ? 'gzip' : 'deflate';\n"
"          inner = _zlibStream(fmt, true);\n"
"          inner.on('data', function(buf) { t.push(buf); });\n"
"          inner.on('end',  function() { t.push(null); });\n"
"          inner.on('error', function(err) { t.destroy(err); });\n"
"          inner.write(merged, cb);\n"
"        } else {\n"
"          inner.write(data, cb);\n"
"        }\n"
"      },\n"
"      flush: function(cb) {\n"
"        if (!inner) {\n"
"          if (pending && pending.length) {\n"
"            /* tiny input -- decide on whatever we have */\n"
"            var merged2 = Buffer.concat(pending); pending = null;\n"
"            var fmt2 = (merged2.length >= 2 && merged2[0] === 0x1F && merged2[1] === 0x8B) ? 'gzip' : 'deflate';\n"
"            inner = _zlibStream(fmt2, true);\n"
"            inner.on('data', function(buf) { t.push(buf); });\n"
"            inner.on('end',  function() { t.push(null); cb(); });\n"
"            inner.on('error', function(err) { cb(err); });\n"
"            inner.end(merged2);\n"
"          } else { cb(); }\n"
"          return;\n"
"        }\n"
"        inner.once('end', function() { cb(); });\n"
"        inner.once('error', function(err) { cb(err); });\n"
"        inner.end();\n"
"      }\n"
"    });\n"
"    return t;\n"
"  }\n"
"  function _brotliStub(name) {\n"
"    return function() {\n"
"      var e = new Error('zlib.' + name + ' not supported (Brotli requires libbrotli, not in this build)');\n"
"      e.code = 'ERR_NOT_SUPPORTED'; throw e;\n"
"    };\n"
"  }\n"
"\n"
"  return {\n"
"    /* sync */\n"
"    gzipSync:       gzipSync,\n"
"    gunzipSync:     gunzipSync,\n"
"    deflateSync:    deflateSync,\n"
"    inflateSync:    inflateSync,\n"
"    deflateRawSync: deflateRawSync,\n"
"    inflateRawSync: inflateRawSync,\n"
"    unzipSync:      unzipSync,\n"
"    /* async (callback) */\n"
"    gzip:           _makeAsync(gzipSync),\n"
"    gunzip:         _makeAsync(gunzipSync),\n"
"    deflate:        _makeAsync(deflateSync),\n"
"    inflate:        _makeAsync(inflateSync),\n"
"    deflateRaw:     _makeAsync(deflateRawSync),\n"
"    inflateRaw:     _makeAsync(inflateRawSync),\n"
"    unzip:          _makeAsync(unzipSync),\n"
"    /* checksums */\n"
"    crc32:          u.crc32,\n"
"    /* constants (node.zlib.constants) */\n"
"    constants: {\n"
"      Z_NO_FLUSH: 0, Z_PARTIAL_FLUSH: 1, Z_SYNC_FLUSH: 2,\n"
"      Z_FULL_FLUSH: 3, Z_FINISH: 4, Z_BLOCK: 5, Z_TREES: 6,\n"
"      Z_OK: 0, Z_STREAM_END: 1, Z_NEED_DICT: 2,\n"
"      Z_ERRNO: -1, Z_STREAM_ERROR: -2, Z_DATA_ERROR: -3,\n"
"      Z_MEM_ERROR: -4, Z_BUF_ERROR: -5, Z_VERSION_ERROR: -6,\n"
"      Z_NO_COMPRESSION: 0, Z_BEST_SPEED: 1, Z_BEST_COMPRESSION: 9,\n"
"      Z_DEFAULT_COMPRESSION: -1,\n"
"      Z_FILTERED: 1, Z_HUFFMAN_ONLY: 2, Z_RLE: 3, Z_FIXED: 4,\n"
"      Z_DEFAULT_STRATEGY: 0,\n"
"      Z_BINARY: 0, Z_TEXT: 1, Z_UNKNOWN: 2,\n"
"      Z_DEFLATED: 8, Z_NULL: 0,\n"
"      Z_DEFAULT_CHUNK: 16384, Z_DEFAULT_LEVEL: -1,\n"
"      Z_DEFAULT_MEMLEVEL: 8, Z_DEFAULT_WINDOWBITS: 15,\n"
"      Z_MAX_CHUNK: Infinity, Z_MAX_LEVEL: 9,\n"
"      Z_MAX_MEMLEVEL: 9, Z_MAX_WINDOWBITS: 15,\n"
"      Z_MIN_CHUNK: 64, Z_MIN_LEVEL: -1, Z_MIN_MEMLEVEL: 1,\n"
"      Z_MIN_WINDOWBITS: 8,\n"
"      DEFLATE: 1, INFLATE: 2, GZIP: 3, GUNZIP: 4,\n"
"      DEFLATERAW: 5, INFLATERAW: 6, UNZIP: 7,\n"
"      BROTLI_DECODE: 8, BROTLI_ENCODE: 9, ZSTD_COMPRESS: 10, ZSTD_DECOMPRESS: 11\n"
"    },\n"
"    /* stream classes -- backed by WHATWG CompressionStream */\n"
"    Deflate:    function() { return _zlibStream('deflate',     false); },\n"
"    Inflate:    function() { return _zlibStream('deflate',     true); },\n"
"    Gzip:       function() { return _zlibStream('gzip',        false); },\n"
"    Gunzip:     function() { return _zlibStream('gzip',        true); },\n"
"    DeflateRaw: function() { return _zlibStream('deflate-raw', false); },\n"
"    InflateRaw: function() { return _zlibStream('deflate-raw', true); },\n"
"    Unzip:      function() { return _unzipStream(); },\n"
"    createDeflate:    function() { return _zlibStream('deflate',     false); },\n"
"    createInflate:    function() { return _zlibStream('deflate',     true); },\n"
"    createGzip:       function() { return _zlibStream('gzip',        false); },\n"
"    createGunzip:     function() { return _zlibStream('gzip',        true); },\n"
"    createDeflateRaw: function() { return _zlibStream('deflate-raw', false); },\n"
"    createInflateRaw: function() { return _zlibStream('deflate-raw', true); },\n"
"    createUnzip:      function() { return _unzipStream(); },\n"
"    /* Brotli — separate algorithm */\n"
"    BrotliCompress:       _brotliStub('BrotliCompress'),\n"
"    BrotliDecompress:     _brotliStub('BrotliDecompress'),\n"
"    brotliCompress:       _brotliStub('brotliCompress'),\n"
"    brotliCompressSync:   _brotliStub('brotliCompressSync'),\n"
"    brotliDecompress:     _brotliStub('brotliDecompress'),\n"
"    brotliDecompressSync: _brotliStub('brotliDecompressSync'),\n"
"    createBrotliCompress:   _brotliStub('createBrotliCompress'),\n"
"    createBrotliDecompress: _brotliStub('createBrotliDecompress')\n"
"  };\n"
"}";
static void nodeshim_init_zlib(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:zlib_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, zlib_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * worker_threads -- node-compat Worker + parentPort + workerData.
 *
 * Backed by rampart.thread + rampart.lock + thread.onGet.  Each Worker
 * uses two clipboard slots (parent->worker and worker->parent) and a
 * shared lock to make read-modify-write of the slot array atomic.
 * Messages are delivered via thread.onGet (level-triggered via libevent
 * pipe registration) so signals are never lost between event-loop ticks.
 *
 * Termination uses thr.terminate() (forcible loopbreak with C-side
 * sweep of pending events) so a Worker holding an onGet subscription
 * can actually be shut down -- thr.close() would hang forever on
 * subscribed threads.
 *
 * Caveats: deep-copy on every postMessage (no SharedArrayBuffer
 * transfer), transferList is no-op, MessageChannel + BroadcastChannel
 * not yet implemented (stubs throw).
 * ============================================================ */
static const char *worker_threads_js =
"function(EventEmitter) {\n"
"  'use strict';\n"
"  var thread       = rampart.thread;\n"
"\n"
"  var _nextId = 1;\n"
"  function _newId() { return _nextId++; }\n"
"\n"
"  /* Module exports.  Mutated by _initWorker inside the worker thread\n"
"     so isMainThread / threadId / workerData / parentPort have the\n"
"     correct values before user code runs.                          */\n"
"  var X = {\n"
"    isMainThread: true,\n"
"    threadId:     0,\n"
"    workerData:   undefined,\n"
"    parentPort:   null,\n"
"    /* sentinel used by node for inheriting parent env -- harmless string for us */\n"
"    SHARE_ENV: '__share_env__',\n"
"    setEnvironmentData: function() {},\n"
"    getEnvironmentData: function() { return undefined; },\n"
"    moveMessagePortToContext: function() {\n"
"      throw new Error('moveMessagePortToContext: not supported');\n"
"    },\n"
"    receiveMessageOnPort: function() {\n"
"      throw new Error('receiveMessageOnPort: not supported');\n"
"    },\n"
"    markAsUntransferable:    function() {},\n"
"    isMarkedAsUntransferable:function() { return false; },\n"
"    Worker:           null,  /* set below */\n"
"    MessagePort:      null,\n"
"    MessageChannel:   null,\n"
"    BroadcastChannel: null,\n"
"    _initWorker:      null\n"
"  };\n"
"\n"
"  /* Minimal thenable so worker.terminate() can be awaited even though\n"
"     rampart has no Promise by default. */\n"
"  function _thenable(value) {\n"
"    return {\n"
"      then: function(ok, fail) {\n"
"        setImmediate(function() {\n"
"          try { ok && ok(value); }\n"
"          catch (e) { fail && fail(e); }\n"
"        });\n"
"        return _thenable(value);\n"
"      },\n"
"      'catch': function() { return this; }\n"
"    };\n"
"  }\n"
"\n"
"  /* MessagePort, MessageChannel, BroadcastChannel now live in\n"
"     rampart-whatwg.so.  We re-expose them via getters on X (the\n"
"     module exports) — lazy resolution avoids triggering whatwg load\n"
"     during nodeshim init. */\n"
"  Object.defineProperty(X, 'MessagePort', {\n"
"    get: function() { return globalThis.MessagePort; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"\n"
"  /* ---------- Worker class (parent side) ---------- */\n"
"  function Worker(filenameOrCode, options) {\n"
"    if (!(this instanceof Worker))\n"
"      return new Worker(filenameOrCode, options);\n"
"    EventEmitter.call(this);\n"
"    options = options || {};\n"
"\n"
"    var id      = _newId();\n"
"    var chanP2W = '_wt_p2w_' + id;\n"
"    var chanW2P = '_wt_w2p_' + id;\n"
"    var lock    = new rampart.lock();\n"
"    thread.put(chanP2W, []);\n"
"    thread.put(chanW2P, []);\n"
"\n"
"    this._id       = id;\n"
"    this._chanP2W  = chanP2W;\n"
"    this._chanW2P  = chanW2P;\n"
"    this._lock     = lock;\n"
"    this._exited   = false;\n"
"    this._exitCode = null;\n"
"\n"
"    var script   = null;\n"
"    var filename = null;\n"
"    if (options.eval) script = String(filenameOrCode);\n"
"    else              filename = String(filenameOrCode);\n"
"\n"
"    var self = this;\n"
"\n"
"    /* Subscribe to worker->parent BEFORE spawning so we don't miss\n"
"       messages.  Drain anything already in the slot first.          */\n"
"    lock.lock();\n"
"    var initial = thread.get(chanW2P) || [];\n"
"    if (initial.length) thread.put(chanW2P, []);\n"
"    lock.unlock();\n"
"\n"
"    this._sub = thread.onGet(chanW2P, function() {\n"
"      if (self._exited) return;\n"
"      lock.lock();\n"
"      var msgs = thread.get(chanW2P) || [];\n"
"      if (msgs.length) thread.put(chanW2P, []);\n"
"      lock.unlock();\n"
"      for (var i = 0; i < msgs.length; i++) _dispatch(self, msgs[i]);\n"
"    });\n"
"\n"
"    var thr = new rampart.thread();\n"
"    this._thr     = thr;\n"
"    this.threadId = thr.getId();\n"
"\n"
"    var threadArg = {\n"
"      id:         id,\n"
"      chanP2W:    chanP2W,\n"
"      chanW2P:    chanW2P,\n"
"      lock:       lock,\n"
"      workerData: options.workerData,\n"
"      script:     script,\n"
"      filename:   filename,\n"
"      isEval:     !!options.eval\n"
"    };\n"
"\n"
"    thr.exec(_workerEntry, threadArg, function(retval, err) {\n"
"      /* Fires when _workerEntry returns or throws synchronously.\n"
"         Asynchronous errors / clean exits come via in-band exit msg. */\n"
"      if (err) {\n"
"        var emsg = (err && err.error) || err;\n"
"        setImmediate(function() {\n"
"          self.emit('error', new Error(String(emsg)));\n"
"          if (!self._exited) {\n"
"            self._exited = true;\n"
"            self._exitCode = 1;\n"
"            self.emit('exit', 1);\n"
"            if (self._sub && self._sub.remove) self._sub.remove();\n"
"            self._sub = null;\n"
"          }\n"
"        });\n"
"      }\n"
"    });\n"
"\n"
"    setImmediate(function() {\n"
"      if (!self._exited) self.emit('online');\n"
"    });\n"
"\n"
"    if (initial.length) {\n"
"      setImmediate(function() {\n"
"        for (var i = 0; i < initial.length; i++) _dispatch(self, initial[i]);\n"
"      });\n"
"    }\n"
"  }\n"
"  Worker.prototype = Object.create(EventEmitter.prototype);\n"
"  Worker.prototype.constructor = Worker;\n"
"\n"
"  function _dispatch(worker, msg) {\n"
"    if (msg && typeof msg === 'object') {\n"
"      if (msg.__wt_exit === true) {\n"
"        if (!worker._exited) {\n"
"          worker._exited   = true;\n"
"          worker._exitCode = (msg.code | 0);\n"
"          if (worker._sub && worker._sub.remove) worker._sub.remove();\n"
"          worker._sub = null;\n"
"          worker.emit('exit', worker._exitCode);\n"
"        }\n"
"        return;\n"
"      }\n"
"      if (msg.__wt_error === true) {\n"
"        worker.emit('error', new Error(String(msg.message || 'worker error')));\n"
"        return;\n"
"      }\n"
"    }\n"
"    worker.emit('message', msg);\n"
"  }\n"
"\n"
"  /* Mirror of the validation logic now in rampart-whatwg.so; kept\n"
"     inline here because Worker.postMessage validates the parent-\n"
"     side transferList without going through a MessagePort. */\n"
"  function _validateTransferList(list) {\n"
"    if (list == null) return;\n"
"    if (!Array.isArray(list))\n"
"      throw new TypeError('transferList must be an Array');\n"
"    var MP = globalThis.MessagePort;\n"
"    for (var i = 0; i < list.length; i++) {\n"
"      var t = list[i];\n"
"      var ok = (t instanceof ArrayBuffer) || (MP && t instanceof MP);\n"
"      if (!ok) {\n"
"        var e = new Error('Found invalid object in transferList');\n"
"        e.code = 'ERR_INVALID_TRANSFER_OBJECT';\n"
"        throw e;\n"
"      }\n"
"    }\n"
"  }\n"
"  Worker.prototype.postMessage = function(msg, transferList) {\n"
"    if (this._exited) return;\n"
"    _validateTransferList(transferList);\n"
"    this._lock.lock();\n"
"    try {\n"
"      var cur = thread.get(this._chanP2W) || [];\n"
"      cur.push(msg);\n"
"      thread.put(this._chanP2W, cur);\n"
"    } finally {\n"
"      this._lock.unlock();\n"
"    }\n"
"  };\n"
"\n"
"  Worker.prototype.terminate = function() {\n"
"    if (this._exited)\n"
"      return _thenable(this._exitCode == null ? 1 : this._exitCode);\n"
"    var self = this;\n"
"    try { if (this._thr && this._thr.terminate) this._thr.terminate(); }\n"
"    catch (e) {}\n"
"    if (!this._exited) {\n"
"      this._exited   = true;\n"
"      this._exitCode = 1;\n"
"      if (this._sub && this._sub.remove) this._sub.remove();\n"
"      this._sub = null;\n"
"      setImmediate(function() { self.emit('exit', 1); });\n"
"    }\n"
"    return _thenable(1);\n"
"  };\n"
"\n"
"  Worker.prototype.ref   = function() { return this; };\n"
"  Worker.prototype.unref = function() { return this; };\n"
"  Worker.prototype.getHeapSnapshot = function() {\n"
"    throw new Error('getHeapSnapshot: not supported in rampart');\n"
"  };\n"
"\n"
"  Object.defineProperty(Worker.prototype, 'stdin', {\n"
"    get: function() { return null; }, configurable: true });\n"
"  Object.defineProperty(Worker.prototype, 'stdout', {\n"
"    get: function() { return null; }, configurable: true });\n"
"  Object.defineProperty(Worker.prototype, 'stderr', {\n"
"    get: function() { return null; }, configurable: true });\n"
"  Object.defineProperty(Worker.prototype, 'resourceLimits', {\n"
"    get: function() { return {}; }, configurable: true });\n"
"  Object.defineProperty(Worker.prototype, 'performance', {\n"
"    get: function() { return null; }, configurable: true });\n"
"\n"
"  X.Worker = Worker;\n"
"\n"
"  /* ---------- Worker entry (runs in worker pthread) ----------\n"
"     Serialized via duk_dump_function and re-eval'd in the worker.\n"
"     Closures don't survive thr.exec; rely on threadArg + globals. */\n"
"  function _workerEntry(arg) {\n"
"    var ws = require('worker_threads');\n"
"    ws._initWorker(arg);\n"
"\n"
"    var t = rampart.thread;\n"
"    var pp = ws.parentPort;\n"
"\n"
"    function _send(obj) {\n"
"      arg.lock.lock();\n"
"      try {\n"
"        var cur = t.get(arg.chanW2P) || [];\n"
"        cur.push(obj);\n"
"        t.put(arg.chanW2P, cur);\n"
"      } finally {\n"
"        arg.lock.unlock();\n"
"      }\n"
"    }\n"
"\n"
"    /* Hook process.exit so user code can exit cleanly with a code. */\n"
"    try {\n"
"      var process = require('rampart-nodeshim').process;\n"
"      process.exit = function(code) {\n"
"        _send({__wt_exit: true, code: (code | 0)});\n"
"        if (pp) pp.close();\n"
"      };\n"
"    } catch (e) {}\n"
"\n"
"    try {\n"
"      if (arg.isEval) {\n"
"        /* indirect eval: run in global scope */\n"
"        (1, eval)(arg.script);\n"
"      } else {\n"
"        require(arg.filename);\n"
"      }\n"
"    } catch (e) {\n"
"      _send({__wt_error: true, message: String(e && e.message || e)});\n"
"      _send({__wt_exit:  true, code: 1});\n"
"      if (pp) pp.close();\n"
"    }\n"
"  }\n"
"\n"
"  /* ---------- _initWorker (invoked from worker entry) ---------- */\n"
"  X._initWorker = function(arg) {\n"
"    X.isMainThread = false;\n"
"    X.threadId     = rampart.thread.getCurrentId();\n"
"    X.workerData   = arg.workerData;\n"
"    var port = new globalThis.MessagePort(arg.chanP2W, arg.chanW2P, arg.lock);\n"
"    port._listen();\n"
"    X.parentPort = port;\n"
"  };\n"
"\n"
"  /* MessageChannel + BroadcastChannel implementations live in\n"
"     rampart-whatwg.so; re-exposed here via getters. */\n"
"  Object.defineProperty(X, 'MessageChannel', {\n"
"    get: function() { return globalThis.MessageChannel; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"  Object.defineProperty(X, 'BroadcastChannel', {\n"
"    get: function() { return globalThis.BroadcastChannel; },\n"
"    configurable: true, enumerable: true\n"
"  });\n"
"\n"
"  return X;\n"
"}";
static void nodeshim_init_worker_threads(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:worker_threads_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, worker_threads_js);
    /* Pass EventEmitter from the partially-built top-level exports object.
       Slot ordering in duk_open_module guarantees `events` was set first. */
    duk_get_prop_string(ctx, -2, "events");
    duk_call(ctx, 1);
}

/* ============================================================
 * tty — minimal but useful v1.  Covers isatty + the query/control
 * surface that color/progress/prompt libraries actually use:
 *   - isatty(fd)
 *   - ReadStream(fd):  setRawMode(b), isRaw, isTTY, columns, rows,
 *                      getWindowSize()
 *   - WriteStream(fd): isTTY, columns, rows, getWindowSize(),
 *                      cursorTo, moveCursor, clearLine,
 *                      clearScreenDown, getColorDepth, hasColors
 * Full Readable/Writable stream behavior is deferred until the
 * `stream` module lands; constructors take an fd but do NOT extend
 * Readable/Writable for v1.  ReadStream.setRawMode toggles termios
 * canonical mode via the native helper below — original termios is
 * saved on first call per-fd so the state is restorable.
 * ============================================================ */
#include <termios.h>
#include <sys/ioctl.h>

/* Saved termios per-fd so setRawMode(false) can restore.  We expect
   at most a handful of TTY fds in a process (stdin, maybe a forkpty
   slave), so a small static table is fine. */
typedef struct {
    int            fd;
    int            saved;
    struct termios orig;
} ns_termios_save_t;
#define NS_TTY_SAVE_MAX 8
static ns_termios_save_t _ns_tty_saves[NS_TTY_SAVE_MAX];

static ns_termios_save_t *_ns_tty_slot(int fd, int create)
{
    int i, free_i = -1;
    for (i = 0; i < NS_TTY_SAVE_MAX; i++) {
        if (_ns_tty_saves[i].saved && _ns_tty_saves[i].fd == fd)
            return &_ns_tty_saves[i];
        if (!_ns_tty_saves[i].saved && free_i < 0)
            free_i = i;
    }
    if (create && free_i >= 0) {
        _ns_tty_saves[free_i].fd = fd;
        return &_ns_tty_saves[free_i];
    }
    return NULL;
}

static duk_ret_t tty_isatty_c(duk_context *ctx)
{
    int fd = duk_to_int(ctx, 0);
    duk_push_boolean(ctx, isatty(fd) ? 1 : 0);
    return 1;
}

static duk_ret_t tty_get_window_size_c(duk_context *ctx)
{
    int fd = duk_to_int(ctx, 0);
    struct winsize w;
    if (ioctl(fd, TIOCGWINSZ, &w) != 0) {
        duk_push_null(ctx);
        return 1;
    }
    /* Return [cols, rows] to match node's getWindowSize(). */
    duk_push_array(ctx);
    duk_push_int(ctx, w.ws_col); duk_put_prop_index(ctx, -2, 0);
    duk_push_int(ctx, w.ws_row); duk_put_prop_index(ctx, -2, 1);
    return 1;
}

/* setRawMode(fd, mode) → true on success, false on failure.
   On first true→raw transition, saves the current termios so the
   subsequent setRawMode(false) can restore. */
static duk_ret_t tty_set_raw_mode_c(duk_context *ctx)
{
    int fd   = duk_to_int(ctx, 0);
    int mode = duk_to_boolean(ctx, 1);
    if (!isatty(fd)) {
        duk_push_boolean(ctx, 0);
        return 1;
    }
    if (mode) {
        ns_termios_save_t *slot = _ns_tty_slot(fd, 1);
        if (slot && !slot->saved) {
            if (tcgetattr(fd, &slot->orig) != 0) {
                duk_push_boolean(ctx, 0);
                return 1;
            }
            slot->saved = 1;
        }
        struct termios raw;
        if (tcgetattr(fd, &raw) != 0) { duk_push_boolean(ctx, 0); return 1; }
        cfmakeraw(&raw);
        if (tcsetattr(fd, TCSANOW, &raw) != 0) { duk_push_boolean(ctx, 0); return 1; }
    } else {
        ns_termios_save_t *slot = _ns_tty_slot(fd, 0);
        if (!slot || !slot->saved) {
            /* Nothing saved — nothing to restore.  Return true so
               callers idempotently toggling raw mode don't error. */
            duk_push_boolean(ctx, 1);
            return 1;
        }
        if (tcsetattr(fd, TCSANOW, &slot->orig) != 0) {
            duk_push_boolean(ctx, 0);
            return 1;
        }
        slot->saved = 0;
        slot->fd = -1;
    }
    duk_push_boolean(ctx, 1);
    return 1;
}

static const char *tty_js =
"function(natives) {\n"
"  'use strict';\n"
"  var ESC = '\\x1b[';\n"
"\n"
"  function isatty(fd) { return natives.isatty(fd|0); }\n"
"\n"
"  /* getColorDepth: 1 (mono) / 4 (16) / 8 (256) / 24 (truecolor).\n"
"     Mirrors node's heuristic over TERM/COLORTERM/NO_COLOR/FORCE_COLOR. */\n"
"  function envColorDepth(env) {\n"
"    env = env || (typeof process !== 'undefined' ? process.env : {}) || {};\n"
"    if (env.NO_COLOR && env.NO_COLOR !== '') return 1;\n"
"    if (env.FORCE_COLOR !== undefined && env.FORCE_COLOR !== '') {\n"
"      var fc = String(env.FORCE_COLOR);\n"
"      if (fc === '1' || fc === 'true')  return 4;\n"
"      if (fc === '2')                   return 8;\n"
"      if (fc === '3' || fc === 'true')  return 24;\n"
"    }\n"
"    var term  = env.TERM || '';\n"
"    var cterm = env.COLORTERM || '';\n"
"    if (term === 'dumb') return 1;\n"
"    if (cterm === 'truecolor' || cterm === '24bit') return 24;\n"
"    if (/-256(color)?/.test(term)) return 8;\n"
"    if (/^xterm|color|ansi|cygwin|linux|screen|tmux|vt100|vt220|rxvt/i.test(term)) return 4;\n"
"    return 1;\n"
"  }\n"
"\n"
"  /* hasColors([count], [env]) — node signature variants. */\n"
"  function hasColors(count, env) {\n"
"    if (typeof count === 'object' && count !== null) { env = count; count = 16; }\n"
"    if (typeof count !== 'number') count = 16;\n"
"    var depth = envColorDepth(env);\n"
"    return count <= (1 << depth);\n"
"  }\n"
"\n"
"  /* Window-size getter shared by Read/Write stream. */\n"
"  function _winsize(fd) {\n"
"    var ws = natives.getWindowSize(fd|0);\n"
"    return ws || [80, 24];\n"
"  }\n"
"\n"
"  /* ReadStream — wraps an fd that IS a TTY.  v1: no Readable extension. */\n"
"  function ReadStream(fd, opts) {\n"
"    if (!(this instanceof ReadStream)) return new ReadStream(fd, opts);\n"
"    this.fd     = fd|0;\n"
"    this.isTTY  = true;\n"
"    this.isRaw  = false;\n"
"    var self = this;\n"
"    Object.defineProperty(this, 'columns', {get: function(){ return _winsize(self.fd)[0]; }, configurable: true});\n"
"    Object.defineProperty(this, 'rows',    {get: function(){ return _winsize(self.fd)[1]; }, configurable: true});\n"
"  }\n"
"  ReadStream.prototype.setRawMode = function(mode) {\n"
"    var m = !!mode;\n"
"    if (natives.setRawMode(this.fd|0, m)) this.isRaw = m;\n"
"    return this;\n"
"  };\n"
"  ReadStream.prototype.getWindowSize = function() { return _winsize(this.fd); };\n"
"\n"
"  /* WriteStream — wraps an fd that IS a TTY.  ANSI helpers write\n"
"     escape sequences via process.stdout/stderr matching this.fd. */\n"
"  function WriteStream(fd) {\n"
"    if (!(this instanceof WriteStream)) return new WriteStream(fd);\n"
"    this.fd    = fd|0;\n"
"    this.isTTY = true;\n"
"    var self = this;\n"
"    Object.defineProperty(this, 'columns', {get: function(){ return _winsize(self.fd)[0]; }, configurable: true});\n"
"    Object.defineProperty(this, 'rows',    {get: function(){ return _winsize(self.fd)[1]; }, configurable: true});\n"
"  }\n"
"  function _writeFd(fd, s) {\n"
"    var p = (typeof process !== 'undefined') ? process : null;\n"
"    if (p && fd === 2 && p.stderr && typeof p.stderr.write === 'function') p.stderr.write(s);\n"
"    else if (p && p.stdout && typeof p.stdout.write === 'function')         p.stdout.write(s);\n"
"  }\n"
"  WriteStream.prototype.getWindowSize = function() { return _winsize(this.fd); };\n"
"  WriteStream.prototype.cursorTo = function(x, y, cb) {\n"
"    if (typeof y === 'function') { cb = y; y = undefined; }\n"
"    if (typeof y === 'number') _writeFd(this.fd, ESC + (y+1) + ';' + (x+1) + 'H');\n"
"    else                       _writeFd(this.fd, ESC + (x+1) + 'G');\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  };\n"
"  WriteStream.prototype.moveCursor = function(dx, dy, cb) {\n"
"    var s = '';\n"
"    if (dy < 0) s += ESC + (-dy) + 'A';\n"
"    else if (dy > 0) s += ESC + dy + 'B';\n"
"    if (dx > 0) s += ESC + dx + 'C';\n"
"    else if (dx < 0) s += ESC + (-dx) + 'D';\n"
"    if (s) _writeFd(this.fd, s);\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  };\n"
"  /* clearLine(dir): -1 left of cursor, 1 right, 0 entire line */\n"
"  WriteStream.prototype.clearLine = function(dir, cb) {\n"
"    var code = (dir < 0) ? '1K' : (dir > 0 ? '0K' : '2K');\n"
"    _writeFd(this.fd, ESC + code);\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  };\n"
"  WriteStream.prototype.clearScreenDown = function(cb) {\n"
"    _writeFd(this.fd, ESC + '0J');\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  };\n"
"  WriteStream.prototype.getColorDepth = function(env) { return envColorDepth(env); };\n"
"  WriteStream.prototype.hasColors     = function(c, e) { return hasColors(c, e); };\n"
"\n"
"  return {\n"
"    isatty:      isatty,\n"
"    ReadStream:  ReadStream,\n"
"    WriteStream: WriteStream\n"
"  };\n"
"}";

static void nodeshim_init_tty(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:tty_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, tty_js);

    /* natives object */
    duk_push_object(ctx);
    duk_push_c_function(ctx, tty_isatty_c,         1); duk_put_prop_string(ctx, -2, "isatty");
    duk_push_c_function(ctx, tty_get_window_size_c, 1); duk_put_prop_string(ctx, -2, "getWindowSize");
    duk_push_c_function(ctx, tty_set_raw_mode_c,   2); duk_put_prop_string(ctx, -2, "setRawMode");

    duk_call(ctx, 1);
}

/* ============================================================
 * readline — node-compat line-by-line reading + interactive editor.
 *
 * Tier 1 (programmatic mode): subscribe to input's 'data' event, buffer
 * incoming bytes, split on \r?\n, emit 'line' for each complete line.
 * Pure JS over the stream API.
 *
 * Tier 2 (terminal mode): when opts.terminal===true OR auto-detected
 * (input.isTTY && output.isTTY), enter raw mode on input and run an
 * in-process line editor with arrow-key navigation, history, tab
 * completion, and prompt redraw.  Still pure JS — uses the existing
 * tty.setRawMode + WriteStream escape helpers.
 *
 * Out of scope: async iterator (no `for await (const line of rl)` —
 * blocked on duktape's lack of async iterators), readline.promises
 * submodule (Promise-returning variants).  Sync iteration via
 * `.on('line', cb)` covers every npm consumer we've seen.
 * ============================================================ */
static const char *readline_js =
"function(EventEmitter) {\n"
"  'use strict';\n"
"\n"
"  /* VT100 / xterm key sequence parser.  Takes a byte buffer (Buffer or\n"
"     string), returns {key, consumed} where key is {name, ctrl, meta,\n"
"     shift, sequence} and consumed is the number of bytes that formed\n"
"     this key event.  Returns {key:null, consumed:0} when more bytes\n"
"     are needed (partial escape sequence at the end of input). */\n"
"  function _parseKey(buf, i) {\n"
"    /* buf is a Buffer; i is starting index. */\n"
"    if (i >= buf.length) return { key: null, consumed: 0 };\n"
"    var b = buf[i];\n"
"\n"
"    /* Single-byte controls and printables. */\n"
"    if (b !== 0x1b) {\n"
"      var seq = buf.slice(i, i + 1);\n"
"      var name = null, ch = String.fromCharCode(b);\n"
"      var ctrl = false, shift = false, meta = false;\n"
"      if      (b === 0x0d) { name = 'return'; ch = ''; }\n"
"      else if (b === 0x0a) { name = 'enter'; ch = ''; }\n"
"      else if (b === 0x09) { name = 'tab'; ch = ''; }\n"
"      else if (b === 0x7f || b === 0x08) { name = 'backspace'; ch = ''; }\n"
"      else if (b === 0x03) { name = 'c'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x04) { name = 'd'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x0c) { name = 'l'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x01) { name = 'a'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x05) { name = 'e'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x0b) { name = 'k'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x15) { name = 'u'; ctrl = true; ch = ''; }\n"
"      else if (b === 0x17) { name = 'w'; ctrl = true; ch = ''; }\n"
"      else if (b >= 0x20 && b < 0x7f) { name = ch; }\n"
"      else if (b >= 0x80) { /* let UTF-8 multibyte pass as raw text */\n"
"        /* Decode a minimal UTF-8 sequence — 2/3/4 byte. */\n"
"        var need = (b & 0xe0) === 0xc0 ? 2 : (b & 0xf0) === 0xe0 ? 3 :\n"
"                   (b & 0xf8) === 0xf0 ? 4 : 1;\n"
"        if (i + need > buf.length) return { key: null, consumed: 0 };\n"
"        seq = buf.slice(i, i + need);\n"
"        ch  = seq.toString('utf8');\n"
"        name = ch;\n"
"        return { key: {sequence: ch, name: name, ctrl: false, meta: false, shift: false}, consumed: need };\n"
"      }\n"
"      return { key: {sequence: ch, name: name, ctrl: ctrl, meta: meta, shift: shift}, consumed: 1 };\n"
"    }\n"
"\n"
"    /* ESC starts a possibly-multi-byte sequence.  Need at least 2 bytes\n"
"       to disambiguate.  Bare ESC at end of buffer → wait for more. */\n"
"    if (i + 1 >= buf.length) return { key: null, consumed: 0 };\n"
"    var b1 = buf[i + 1];\n"
"\n"
"    /* ESC <alpha>  — meta+char (Alt-X) */\n"
"    if (b1 !== 0x5b && b1 !== 0x4f) {\n"
"      /* Bare ESC by itself, or ESC followed by a non-bracket — treat\n"
"         as Alt-prefix if printable, else just escape. */\n"
"      if (b1 >= 0x20 && b1 < 0x7f) {\n"
"        return { key: { sequence: '\\x1b' + String.fromCharCode(b1),\n"
"                        name: String.fromCharCode(b1), ctrl: false,\n"
"                        meta: true, shift: false }, consumed: 2 };\n"
"      }\n"
"      return { key: { sequence: '\\x1b', name: 'escape', ctrl: false,\n"
"                      meta: false, shift: false }, consumed: 1 };\n"
"    }\n"
"\n"
"    /* CSI sequence: ESC [ ... <final-byte>  OR  ESC O <letter> */\n"
"    /* Find the terminator. */\n"
"    var j = i + 2;\n"
"    if (b1 === 0x4f) {\n"
"      /* ESC O <letter>: 3 bytes total, terminator is letter at j. */\n"
"      if (j >= buf.length) return { key: null, consumed: 0 };\n"
"      var t = buf[j];\n"
"      var name1 = null;\n"
"      if (t === 0x41) name1 = 'up';\n"
"      else if (t === 0x42) name1 = 'down';\n"
"      else if (t === 0x43) name1 = 'right';\n"
"      else if (t === 0x44) name1 = 'left';\n"
"      else if (t === 0x46) name1 = 'end';\n"
"      else if (t === 0x48) name1 = 'home';\n"
"      else if (t >= 0x50 && t <= 0x53) name1 = 'f' + (t - 0x4f);\n"
"      return { key: { sequence: buf.slice(i, j+1).toString('utf8'),\n"
"                      name: name1, ctrl: false, meta: false, shift: false }, consumed: 3 };\n"
"    }\n"
"    /* CSI: ESC [ <params> <intermediate?> <final>  where final is 0x40..0x7e\n"
"       We accumulate digits and ; separators. */\n"
"    while (j < buf.length) {\n"
"      var c = buf[j];\n"
"      if (c >= 0x40 && c <= 0x7e) break;  /* final byte */\n"
"      j++;\n"
"    }\n"
"    if (j >= buf.length) return { key: null, consumed: 0 };\n"
"    var final = buf[j];\n"
"    var params = buf.slice(i + 2, j).toString('utf8');\n"
"    var consumed = (j - i) + 1;\n"
"    var name2 = null;\n"
"    var shiftK = false;\n"
"    /* params may include shift modifier as ';2' suffix. */\n"
"    if (/;2$/.test(params)) shiftK = true;\n"
"    if      (final === 0x41) name2 = 'up';\n"
"    else if (final === 0x42) name2 = 'down';\n"
"    else if (final === 0x43) name2 = 'right';\n"
"    else if (final === 0x44) name2 = 'left';\n"
"    else if (final === 0x46) name2 = 'end';\n"
"    else if (final === 0x48) name2 = 'home';\n"
"    else if (final === 0x5a) { name2 = 'tab'; shiftK = true; }  /* CSI Z = shift-tab */\n"
"    else if (final === 0x7e) {\n"
"      /* Function key style: CSI <num> ~ */\n"
"      var n = parseInt(params.split(';')[0], 10) || 0;\n"
"      if      (n === 1 || n === 7) name2 = 'home';\n"
"      else if (n === 4 || n === 8) name2 = 'end';\n"
"      else if (n === 3) name2 = 'delete';\n"
"      else if (n === 2) name2 = 'insert';\n"
"      else if (n === 5) name2 = 'pageup';\n"
"      else if (n === 6) name2 = 'pagedown';\n"
"      else if (n === 15) name2 = 'f5';\n"
"      else if (n === 17) name2 = 'f6';\n"
"      else if (n === 18) name2 = 'f7';\n"
"      else if (n === 19) name2 = 'f8';\n"
"      else if (n === 20) name2 = 'f9';\n"
"      else if (n === 21) name2 = 'f10';\n"
"      else if (n === 23) name2 = 'f11';\n"
"      else if (n === 24) name2 = 'f12';\n"
"    }\n"
"    return { key: { sequence: buf.slice(i, j+1).toString('utf8'),\n"
"                    name: name2, ctrl: false, meta: false, shift: shiftK },\n"
"             consumed: consumed };\n"
"  }\n"
"\n"
"  /* Install a 'keypress' event source on a Readable stream.  Each key\n"
"     emits ('keypress', char, keyInfo) per node's surface. */\n"
"  function emitKeypressEvents(stream, iface) {\n"
"    if (stream._keypressDecoder) return;\n"
"    stream._keypressDecoder = true;\n"
"    var pending = Buffer.alloc(0);\n"
"    stream.on('data', function(chunk) {\n"
"      var buf = Buffer.isBuffer(chunk) ? chunk\n"
"              : (chunk instanceof Uint8Array)\n"
"                  ? Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength)\n"
"                  : Buffer.from(String(chunk), 'utf8');\n"
"      pending = Buffer.concat([pending, buf]);\n"
"      var i = 0;\n"
"      while (i < pending.length) {\n"
"        var r = _parseKey(pending, i);\n"
"        if (r.consumed === 0) break;  /* need more bytes */\n"
"        i += r.consumed;\n"
"        if (r.key) stream.emit('keypress', r.key.sequence, r.key);\n"
"      }\n"
"      pending = pending.slice(i);\n"
"    });\n"
"  }\n"
"\n"
"  /* Interface — the readline workhorse.  Subclass of EventEmitter. */\n"
"  function Interface(opts) {\n"
"    if (!(this instanceof Interface)) return new Interface(opts);\n"
"    EventEmitter.call(this);\n"
"    opts = opts || {};\n"
"    var self = this;\n"
"    self.input        = opts.input;\n"
"    self.output       = opts.output;\n"
"    self.completer    = (typeof opts.completer === 'function') ? opts.completer : null;\n"
"    self._prompt      = (opts.prompt != null) ? String(opts.prompt) : '> ';\n"
"    self._closed      = false;\n"
"    self.terminal     = (opts.terminal !== undefined)\n"
"      ? !!opts.terminal\n"
"      : !!(self.input && self.input.isTTY && self.output && self.output.isTTY);\n"
"    self._historySize = (typeof opts.historySize === 'number') ? opts.historySize : 30;\n"
"    self.history      = Array.isArray(opts.history) ? opts.history.slice() : [];\n"
"    self._histIdx     = -1;            /* -1 = current line, 0+ = history offset */\n"
"    self._savedLine   = '';            /* original line when scrolling history */\n"
"    self._line        = '';\n"
"    self._cursor      = 0;\n"
"    if (!self.input) throw new TypeError('readline: opts.input is required');\n"
"\n"
"    if (self.terminal) {\n"
"      self._installTerminal();\n"
"    } else {\n"
"      self._installLineMode();\n"
"    }\n"
"\n"
"    self.input.on('end', function() {\n"
"      /* Flush any pending partial line (without trailing newline). */\n"
"      if (!self.terminal && self._lineBuf && self._lineBuf.length > 0) {\n"
"        self.emit('line', self._lineBuf);\n"
"        self._lineBuf = '';\n"
"      }\n"
"      self.close();\n"
"    });\n"
"  }\n"
"  Interface.prototype = Object.create(EventEmitter.prototype);\n"
"  Interface.prototype.constructor = Interface;\n"
"\n"
"  /* Tier 1: programmatic line-by-line reading. */\n"
"  Interface.prototype._installLineMode = function() {\n"
"    var self = this;\n"
"    self._lineBuf = '';\n"
"    self.input.on('data', function(chunk) {\n"
"      if (self._closed) return;\n"
"      var s;\n"
"      if (typeof chunk === 'string') s = chunk;\n"
"      else if (Buffer.isBuffer(chunk)) s = chunk.toString('utf8');\n"
"      else if (chunk instanceof Uint8Array)\n"
"           s = Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength).toString('utf8');\n"
"      else s = String(chunk);\n"
"      self._lineBuf += s;\n"
"      var idx;\n"
"      while ((idx = self._lineBuf.indexOf('\\n')) !== -1) {\n"
"        var line = self._lineBuf.substring(0, idx);\n"
"        self._lineBuf = self._lineBuf.substring(idx + 1);\n"
"        /* strip trailing \\r for \\r\\n line endings */\n"
"        if (line.charAt(line.length - 1) === '\\r') line = line.substring(0, line.length - 1);\n"
"        self.emit('line', line);\n"
"      }\n"
"    });\n"
"  };\n"
"\n"
"  /* Tier 2: full interactive editor over a TTY. */\n"
"  Interface.prototype._installTerminal = function() {\n"
"    var self = this;\n"
"    /* Enter raw mode if the input is a tty.ReadStream. */\n"
"    if (self.input && typeof self.input.setRawMode === 'function') {\n"
"      self.input.setRawMode(true);\n"
"    }\n"
"    emitKeypressEvents(self.input, self);\n"
"    self.input.on('keypress', function(s, key) {\n"
"      if (self._closed) return;\n"
"      self._onKeypress(s, key);\n"
"    });\n"
"  };\n"
"\n"
"  Interface.prototype._write = function(s) {\n"
"    if (this.output && typeof this.output.write === 'function') this.output.write(s);\n"
"  };\n"
"  Interface.prototype._redraw = function() {\n"
"    /* Erase current line, write prompt + buffer, move cursor to spot. */\n"
"    var prompt = this._prompt || '';\n"
"    this._write('\\x1b[2K\\r' + prompt + this._line);\n"
"    var col = prompt.length + this._cursor;\n"
"    this._write('\\r\\x1b[' + (col + 1) + 'G');\n"
"  };\n"
"  Interface.prototype.setPrompt = function(p) { this._prompt = String(p); };\n"
"  Interface.prototype.getPrompt = function()  { return this._prompt; };\n"
"  Interface.prototype.prompt    = function(preserveCursor) {\n"
"    if (this._closed) return;\n"
"    if (!preserveCursor) this._cursor = this._line.length;\n"
"    this._redraw();\n"
"  };\n"
"  Interface.prototype.write     = function(d, key) {\n"
"    if (this._closed) return;\n"
"    if (this.terminal && key) { this._onKeypress(d || '', key); return; }\n"
"    if (typeof d === 'string') {\n"
"      this._line = this._line.slice(0, this._cursor) + d + this._line.slice(this._cursor);\n"
"      this._cursor += d.length;\n"
"      if (this.terminal) this._redraw();\n"
"    }\n"
"  };\n"
"  Interface.prototype.pause     = function() { if (this.input.pause) this.input.pause(); this.emit('pause'); return this; };\n"
"  Interface.prototype.resume    = function() { if (this.input.resume) this.input.resume(); this.emit('resume'); return this; };\n"
"  Interface.prototype.close     = function() {\n"
"    if (this._closed) return;\n"
"    this._closed = true;\n"
"    if (this.terminal && this.input && typeof this.input.setRawMode === 'function') {\n"
"      this.input.setRawMode(false);\n"
"    }\n"
"    this.emit('close');\n"
"  };\n"
"\n"
"  Interface.prototype._onKeypress = function(s, key) {\n"
"    if (!key) return;\n"
"    var self = this, n = key.name;\n"
"    /* Ctrl-C: SIGINT, then close. */\n"
"    if (key.ctrl && n === 'c') {\n"
"      if (self.listenerCount && self.listenerCount('SIGINT') > 0) {\n"
"        self.emit('SIGINT');\n"
"      } else {\n"
"        self._write('\\n');\n"
"        self.close();\n"
"      }\n"
"      return;\n"
"    }\n"
"    /* Ctrl-D: EOF on empty line, else delete-forward. */\n"
"    if (key.ctrl && n === 'd') {\n"
"      if (self._line.length === 0) { self.close(); return; }\n"
"      if (self._cursor < self._line.length) {\n"
"        self._line = self._line.slice(0, self._cursor) + self._line.slice(self._cursor + 1);\n"
"        self._redraw();\n"
"      }\n"
"      return;\n"
"    }\n"
"    if (key.ctrl && n === 'l') {\n"
"      self._write('\\x1b[2J\\x1b[H');  /* clear screen + home */\n"
"      self._redraw();\n"
"      return;\n"
"    }\n"
"    if (key.ctrl && n === 'a') { self._cursor = 0; self._redraw(); return; }\n"
"    if (key.ctrl && n === 'e') { self._cursor = self._line.length; self._redraw(); return; }\n"
"    if (key.ctrl && n === 'k') {\n"
"      self._line = self._line.slice(0, self._cursor); self._redraw(); return;\n"
"    }\n"
"    if (key.ctrl && n === 'u') {\n"
"      self._line = self._line.slice(self._cursor); self._cursor = 0; self._redraw(); return;\n"
"    }\n"
"    if (key.ctrl && n === 'w') {\n"
"      /* delete previous word */\n"
"      var start = self._cursor;\n"
"      while (start > 0 && self._line.charAt(start - 1) === ' ') start--;\n"
"      while (start > 0 && self._line.charAt(start - 1) !== ' ') start--;\n"
"      self._line = self._line.slice(0, start) + self._line.slice(self._cursor);\n"
"      self._cursor = start;\n"
"      self._redraw();\n"
"      return;\n"
"    }\n"
"    if (n === 'return' || n === 'enter') {\n"
"      self._write('\\n');\n"
"      var line = self._line;\n"
"      self._line = ''; self._cursor = 0; self._histIdx = -1;\n"
"      if (line.length > 0 && (self.history.length === 0 || self.history[0] !== line)) {\n"
"        self.history.unshift(line);\n"
"        if (self.history.length > self._historySize) self.history.pop();\n"
"        self.emit('history', self.history);\n"
"      }\n"
"      self.emit('line', line);\n"
"      return;\n"
"    }\n"
"    if (n === 'backspace') {\n"
"      if (self._cursor > 0) {\n"
"        self._line = self._line.slice(0, self._cursor - 1) + self._line.slice(self._cursor);\n"
"        self._cursor--;\n"
"        self._redraw();\n"
"      }\n"
"      return;\n"
"    }\n"
"    if (n === 'delete') {\n"
"      if (self._cursor < self._line.length) {\n"
"        self._line = self._line.slice(0, self._cursor) + self._line.slice(self._cursor + 1);\n"
"        self._redraw();\n"
"      }\n"
"      return;\n"
"    }\n"
"    if (n === 'left')  { if (self._cursor > 0)               { self._cursor--; self._redraw(); } return; }\n"
"    if (n === 'right') { if (self._cursor < self._line.length){ self._cursor++; self._redraw(); } return; }\n"
"    if (n === 'home')  { self._cursor = 0;               self._redraw(); return; }\n"
"    if (n === 'end')   { self._cursor = self._line.length; self._redraw(); return; }\n"
"    if (n === 'up') {\n"
"      if (self._histIdx === -1) self._savedLine = self._line;\n"
"      if (self._histIdx + 1 < self.history.length) {\n"
"        self._histIdx++;\n"
"        self._line = self.history[self._histIdx];\n"
"        self._cursor = self._line.length;\n"
"        self._redraw();\n"
"      }\n"
"      return;\n"
"    }\n"
"    if (n === 'down') {\n"
"      if (self._histIdx > 0) {\n"
"        self._histIdx--;\n"
"        self._line = self.history[self._histIdx];\n"
"        self._cursor = self._line.length;\n"
"      } else if (self._histIdx === 0) {\n"
"        self._histIdx = -1;\n"
"        self._line = self._savedLine || '';\n"
"        self._cursor = self._line.length;\n"
"      } else { return; }\n"
"      self._redraw();\n"
"      return;\n"
"    }\n"
"    if (n === 'tab') {\n"
"      if (typeof self.completer === 'function') {\n"
"        var beforeCursor = self._line.slice(0, self._cursor);\n"
"        try {\n"
"          self.completer(beforeCursor, function(err, result) {\n"
"            if (err || !result) return;\n"
"            var completions = result[0], substr = result[1];\n"
"            if (completions && completions.length === 1) {\n"
"              var rest = completions[0].slice(substr.length);\n"
"              self._line = beforeCursor + rest + self._line.slice(self._cursor);\n"
"              self._cursor += rest.length;\n"
"              self._redraw();\n"
"            } else if (completions && completions.length > 1) {\n"
"              self._write('\\n' + completions.join('  ') + '\\n');\n"
"              self._redraw();\n"
"            }\n"
"          });\n"
"        } catch(_) {}\n"
"      }\n"
"      return;\n"
"    }\n"
"    /* Printable input: insert at cursor. */\n"
"    if (s && s.length > 0 && (key.sequence ? key.sequence.charCodeAt(0) >= 0x20 : true)) {\n"
"      self._line = self._line.slice(0, self._cursor) + s + self._line.slice(self._cursor);\n"
"      self._cursor += s.length;\n"
"      self._redraw();\n"
"    }\n"
"  };\n"
"\n"
"  function createInterface(opts) { return new Interface(opts); }\n"
"\n"
"  /* Static helpers — write VT100 escapes to a stream.  Some npm code\n"
"     uses these for line-drawing UIs. */\n"
"  function cursorTo(stream, x, y, cb) {\n"
"    if (stream && typeof stream.cursorTo === 'function') return stream.cursorTo(x, y, cb);\n"
"    var s = (typeof y === 'number') ? '\\x1b[' + (y+1) + ';' + (x+1) + 'H'\n"
"                                    : '\\x1b[' + (x+1) + 'G';\n"
"    if (stream && stream.write) stream.write(s);\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  }\n"
"  function moveCursor(stream, dx, dy, cb) {\n"
"    if (stream && typeof stream.moveCursor === 'function') return stream.moveCursor(dx, dy, cb);\n"
"    var s = '';\n"
"    if (dy < 0) s += '\\x1b[' + (-dy) + 'A';\n"
"    else if (dy > 0) s += '\\x1b[' + dy + 'B';\n"
"    if (dx > 0) s += '\\x1b[' + dx + 'C';\n"
"    else if (dx < 0) s += '\\x1b[' + (-dx) + 'D';\n"
"    if (s && stream && stream.write) stream.write(s);\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  }\n"
"  function clearLine(stream, dir, cb) {\n"
"    if (stream && typeof stream.clearLine === 'function') return stream.clearLine(dir, cb);\n"
"    var code = (dir < 0) ? '1K' : (dir > 0 ? '0K' : '2K');\n"
"    if (stream && stream.write) stream.write('\\x1b[' + code);\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  }\n"
"  function clearScreenDown(stream, cb) {\n"
"    if (stream && typeof stream.clearScreenDown === 'function') return stream.clearScreenDown(cb);\n"
"    if (stream && stream.write) stream.write('\\x1b[0J');\n"
"    if (typeof cb === 'function') cb();\n"
"    return true;\n"
"  }\n"
"\n"
"  return {\n"
"    Interface:           Interface,\n"
"    createInterface:     createInterface,\n"
"    emitKeypressEvents:  emitKeypressEvents,\n"
"    cursorTo:            cursorTo,\n"
"    moveCursor:          moveCursor,\n"
"    clearLine:           clearLine,\n"
"    clearScreenDown:     clearScreenDown\n"
"  };\n"
"}";

static void nodeshim_init_readline(duk_context *ctx)
{
    /* Pass EventEmitter (same pattern as net_js / http_js).  Slot order
       guarantees `events` is set before `readline`. */
    duk_push_string(ctx, "rampart-nodeshim.c:readline_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, readline_js);
    duk_get_prop_string(ctx, -2, "events");
    duk_get_prop_string(ctx, -1, "EventEmitter");
    duk_remove(ctx, -2);
    duk_call(ctx, 1);
}

/* ============================================================
 * child_process — fresh-C implementation (not wrapping rampart.utils.exec).
 *
 * Three native primitives back the JS surface:
 *   cp_spawn_native(opts)  → {pid, stdinFd, stdoutFd, stderrFd}
 *   cp_waitpid_native(pid) → null (still running) or {exitCode, signalCode}
 *   cp_kill_native(pid, sig) → bool
 *
 * The pipe-fd reads + waitpid polling happen in JS via setTimeout, mirroring
 * the existing fs.ReadStream pump pattern.  Avoids the SIGCHLD coordination
 * complexity with rampart-sql / rampart-utils.exec (which has its own
 * exec_sigchld_lock save/restore mutex) at the cost of ~25ms reap latency.
 * Acceptable for v1; can upgrade to libevent SIGCHLD watcher later.
 *
 * fork() in a multi-threaded process is safe as long as the child only
 * calls async-signal-safe functions before execve.  Our child path is
 * exactly that: close(), dup2(), execvp(), _exit() — all on the
 * async-signal-safe list.
 * ============================================================ */

/* Set O_CLOEXEC on a file descriptor. */
static int _cp_set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int _cp_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Free a char ** array previously built from JS (terminating-NULL convention). */
static void _cp_free_strv(char **v)
{
    if (!v) return;
    for (char **p = v; *p; p++) free(*p);
    free(v);
}

/* Build a NULL-terminated char ** from a JS array of strings on the stack at idx.
   Caller frees with _cp_free_strv(). */
static char **_cp_build_strv(duk_context *ctx, duk_idx_t idx)
{
    if (!duk_is_array(ctx, idx)) return NULL;
    duk_size_t n = duk_get_length(ctx, idx);
    char **v = (char **)calloc(n + 1, sizeof(char *));
    if (!v) return NULL;
    for (duk_size_t i = 0; i < n; i++) {
        duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
        const char *s = duk_safe_to_string(ctx, -1);
        v[i] = s ? strdup(s) : strdup("");
        duk_pop(ctx);
        if (!v[i]) { _cp_free_strv(v); return NULL; }
    }
    return v;
}

/* spawn(opts).  opts = { file, args, cwd, env, stdio_pipe: bool x3 }.
   Returns { pid, stdinFd, stdoutFd, stderrFd } where any of the *Fds may
   be -1 if that stdio slot wasn't 'pipe'. */
static duk_ret_t nodeshim_cp_spawn_native(duk_context *ctx)
{
    if (!duk_is_object(ctx, 0))
        RP_THROW(ctx, "child_process spawn: opts object required");

    /* file */
    duk_get_prop_string(ctx, 0, "file");
    const char *file = duk_require_string(ctx, -1);
    char *file_copy = strdup(file);
    duk_pop(ctx);

    /* args[] — strv */
    duk_get_prop_string(ctx, 0, "args");
    char **args = _cp_build_strv(ctx, -1);
    duk_pop(ctx);

    /* env[] — strv of "K=V" strings, or null to inherit */
    duk_get_prop_string(ctx, 0, "env");
    char **envv = duk_is_array(ctx, -1) ? _cp_build_strv(ctx, -1) : NULL;
    duk_pop(ctx);

    /* cwd */
    duk_get_prop_string(ctx, 0, "cwd");
    const char *cwd = duk_is_string(ctx, -1) ? duk_get_string(ctx, -1) : NULL;
    char *cwd_copy = cwd ? strdup(cwd) : NULL;
    duk_pop(ctx);

    /* Which stdio slots get pipes (vs inherit parent's). */
    duk_get_prop_string(ctx, 0, "stdinPipe");
    int pipe_stdin = duk_to_boolean(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "stdoutPipe");
    int pipe_stdout = duk_to_boolean(ctx, -1); duk_pop(ctx);
    duk_get_prop_string(ctx, 0, "stderrPipe");
    int pipe_stderr = duk_to_boolean(ctx, -1); duk_pop(ctx);

    int in_fds[2] = {-1, -1}, out_fds[2] = {-1, -1}, err_fds[2] = {-1, -1};

    if (pipe_stdin  && pipe(in_fds)  != 0) goto err_pipe;
    if (pipe_stdout && pipe(out_fds) != 0) goto err_pipe;
    if (pipe_stderr && pipe(err_fds) != 0) goto err_pipe;

    pid_t pid = fork();
    if (pid == -1) {
        int e = errno;
        if (pipe_stdin)  { close(in_fds[0]);  close(in_fds[1]);  }
        if (pipe_stdout) { close(out_fds[0]); close(out_fds[1]); }
        if (pipe_stderr) { close(err_fds[0]); close(err_fds[1]); }
        free(file_copy); free(cwd_copy);
        _cp_free_strv(args); _cp_free_strv(envv);
        RP_THROW(ctx, "child_process spawn: fork failed: %s", strerror(e));
    }

    if (pid == 0) {
        /* Child.  ONLY async-signal-safe calls between here and execvp. */
        if (pipe_stdin)  { dup2(in_fds[0],  STDIN_FILENO);  close(in_fds[0]);  close(in_fds[1]);  }
        if (pipe_stdout) { dup2(out_fds[1], STDOUT_FILENO); close(out_fds[0]); close(out_fds[1]); }
        if (pipe_stderr) { dup2(err_fds[1], STDERR_FILENO); close(err_fds[0]); close(err_fds[1]); }
        if (cwd_copy && chdir(cwd_copy) != 0) _exit(127);
        if (envv) execvpe(file_copy, args ? args : (char *[]){ file_copy, NULL }, envv);
        else      execvp (file_copy, args ? args : (char *[]){ file_copy, NULL });
        _exit(127);
    }

    /* Parent: close child-end fds, mark our ends CLOEXEC + non-blocking
       (so JS-side u.read() returns EAGAIN immediately instead of stalling
       the rampart event loop until the child writes or exits). */
    if (pipe_stdin)  { close(in_fds[0]);  _cp_set_cloexec(in_fds[1]);  _cp_set_nonblock(in_fds[1]);  }
    if (pipe_stdout) { close(out_fds[1]); _cp_set_cloexec(out_fds[0]); _cp_set_nonblock(out_fds[0]); }
    if (pipe_stderr) { close(err_fds[1]); _cp_set_cloexec(err_fds[0]); _cp_set_nonblock(err_fds[0]); }

    free(file_copy); free(cwd_copy);
    _cp_free_strv(args); _cp_free_strv(envv);

    duk_push_object(ctx);
    duk_push_int(ctx, (duk_int_t)pid);                 duk_put_prop_string(ctx, -2, "pid");
    duk_push_int(ctx, pipe_stdin  ? in_fds[1]  : -1);  duk_put_prop_string(ctx, -2, "stdinFd");
    duk_push_int(ctx, pipe_stdout ? out_fds[0] : -1);  duk_put_prop_string(ctx, -2, "stdoutFd");
    duk_push_int(ctx, pipe_stderr ? err_fds[0] : -1);  duk_put_prop_string(ctx, -2, "stderrFd");
    return 1;

err_pipe: {
    int e = errno;
    if (in_fds[0]  != -1) close(in_fds[0]);
    if (in_fds[1]  != -1) close(in_fds[1]);
    if (out_fds[0] != -1) close(out_fds[0]);
    if (out_fds[1] != -1) close(out_fds[1]);
    if (err_fds[0] != -1) close(err_fds[0]);
    if (err_fds[1] != -1) close(err_fds[1]);
    free(file_copy); free(cwd_copy);
    _cp_free_strv(args); _cp_free_strv(envv);
    RP_THROW(ctx, "child_process spawn: pipe failed: %s", strerror(e));
} }

/* waitpid_native(pid) — non-blocking reap.  Returns null if still running,
   or {exitCode: N|null, signalCode: "SIGTERM"|null}. */
static duk_ret_t nodeshim_cp_waitpid_native(duk_context *ctx)
{
    duk_int_t pid_in = duk_require_int(ctx, 0);
    pid_t pid = (pid_t)pid_in;
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) { duk_push_null(ctx); return 1; }
    if (r == -1) {
        /* ECHILD = already reaped or never was our child; treat as exited
           with no info rather than throw. */
        duk_push_object(ctx);
        duk_push_null(ctx); duk_put_prop_string(ctx, -2, "exitCode");
        duk_push_null(ctx); duk_put_prop_string(ctx, -2, "signalCode");
        return 1;
    }
    duk_push_object(ctx);
    if (WIFEXITED(status)) {
        duk_push_int(ctx, WEXITSTATUS(status));
        duk_put_prop_string(ctx, -2, "exitCode");
        duk_push_null(ctx); duk_put_prop_string(ctx, -2, "signalCode");
    } else if (WIFSIGNALED(status)) {
        duk_push_null(ctx); duk_put_prop_string(ctx, -2, "exitCode");
        /* Map signal number to a "SIG*" name string per node's convention. */
        int sig = WTERMSIG(status);
        const char *name = NULL;
        switch (sig) {
            case SIGHUP:  name = "SIGHUP";  break;
            case SIGINT:  name = "SIGINT";  break;
            case SIGQUIT: name = "SIGQUIT"; break;
            case SIGILL:  name = "SIGILL";  break;
            case SIGTRAP: name = "SIGTRAP"; break;
            case SIGABRT: name = "SIGABRT"; break;
            case SIGFPE:  name = "SIGFPE";  break;
            case SIGKILL: name = "SIGKILL"; break;
            case SIGBUS:  name = "SIGBUS";  break;
            case SIGSEGV: name = "SIGSEGV"; break;
            case SIGPIPE: name = "SIGPIPE"; break;
            case SIGALRM: name = "SIGALRM"; break;
            case SIGTERM: name = "SIGTERM"; break;
            case SIGUSR1: name = "SIGUSR1"; break;
            case SIGUSR2: name = "SIGUSR2"; break;
            default: break;
        }
        if (name) duk_push_string(ctx, name);
        else      duk_push_sprintf(ctx, "SIG_%d", sig);
        duk_put_prop_string(ctx, -2, "signalCode");
    } else {
        /* Stopped / continued — uncommon for our usage; treat as still running. */
        duk_pop(ctx);
        duk_push_null(ctx);
    }
    return 1;
}

/* kill_native(pid, sig)  — sig may be number or "SIG*" string.  Returns bool. */
static duk_ret_t nodeshim_cp_kill_native(duk_context *ctx)
{
    pid_t pid = (pid_t)duk_require_int(ctx, 0);
    int sig = SIGTERM;
    if (duk_is_number(ctx, 1)) {
        sig = duk_get_int(ctx, 1);
    } else if (duk_is_string(ctx, 1)) {
        const char *s = duk_get_string(ctx, 1);
        /* Allow "SIGINT" or "INT". */
        if (strncmp(s, "SIG", 3) == 0) s += 3;
        if      (!strcmp(s, "HUP"))  sig = SIGHUP;
        else if (!strcmp(s, "INT"))  sig = SIGINT;
        else if (!strcmp(s, "QUIT")) sig = SIGQUIT;
        else if (!strcmp(s, "KILL")) sig = SIGKILL;
        else if (!strcmp(s, "USR1")) sig = SIGUSR1;
        else if (!strcmp(s, "USR2")) sig = SIGUSR2;
        else if (!strcmp(s, "PIPE")) sig = SIGPIPE;
        else if (!strcmp(s, "TERM")) sig = SIGTERM;
        else if (!strcmp(s, "STOP")) sig = SIGSTOP;
        else if (!strcmp(s, "CONT")) sig = SIGCONT;
        else { duk_push_boolean(ctx, 0); return 1; }
    }
    int r = kill(pid, sig);
    duk_push_boolean(ctx, r == 0 ? 1 : 0);
    return 1;
}

static const char *child_process_js =
"function(EventEmitter, natives) {\n"
"  'use strict';\n"
"  var u = rampart.utils;\n"
"\n"
"  /* Map a file descriptor → polling Readable that emits 'data'/'end'. */\n"
"  function _fdReadable(fd) {\n"
"    if (fd < 0) return null;\n"
"    var em = new EventEmitter();\n"
"    em.fd = fd;\n"
"    em.readable = true;\n"
"    em._closed = false;\n"
"    em._paused = false;\n"
"    em._encoding = null;\n"
"    em.bytesRead = 0;\n"
"    em.setEncoding = function(enc) { em._encoding = enc; return em; };\n"
"    em.pause  = function() { em._paused = true;  return em; };\n"
"    em.resume = function() { em._paused = false; return em; };\n"
"    em.destroy = function() {\n"
"      if (em._closed) return em;\n"
"      em._closed = true;\n"
"      try { u.close(fd); } catch(_){}\n"
"      em.emit('close');\n"
"      return em;\n"
"    };\n"
"    /* pipe(dest) — wire 'data' → dest.write, 'end' → dest.end. */\n"
"    em.pipe = function(dest, opts) {\n"
"      opts = opts || {};\n"
"      em.on('data', function(c) { if (dest.write) dest.write(c); });\n"
"      em.on('end',  function()  { if (opts.end !== false && dest.end) dest.end(); });\n"
"      return dest;\n"
"    };\n"
"    /* setTimeout-based pump.  Same pattern as fs.ReadStream. */\n"
"    function pump() {\n"
"      if (em._closed) return;\n"
"      if (em._paused) { setTimeout(pump, 25); return; }\n"
"      var b;\n"
"      try { b = u.read(fd, 65536); }\n"
"      catch (e) {\n"
"        /* EAGAIN/EWOULDBLOCK → retry; EBADF/etc → real error. */\n"
"        if (e && e.message && /EAGAIN|EWOULDBLOCK|temporarily/i.test(e.message)) {\n"
"          setTimeout(pump, 25);\n"
"          return;\n"
"        }\n"
"        em.emit('error', e);\n"
"        em._closed = true;\n"
"        try { u.close(fd); } catch(_){}\n"
"        return;\n"
"      }\n"
"      if (b.length === 0) {\n"
"        em._closed = true;\n"
"        try { u.close(fd); } catch(_){}\n"
"        em.emit('end');\n"
"        em.emit('close');\n"
"        return;\n"
"      }\n"
"      em.bytesRead += b.length;\n"
"      var emit = Buffer.from(b);\n"
"      if (em._encoding) emit = emit.toString(em._encoding);\n"
"      em.emit('data', emit);\n"
"      /* Yield to event loop between reads.  0ms keeps throughput high\n"
"         when there's data, while still letting other timers fire. */\n"
"      setTimeout(pump, 0);\n"
"    }\n"
"    setTimeout(pump, 0);\n"
"    return em;\n"
"  }\n"
"\n"
"  /* Map a file descriptor → Writable that buffers + flushes on each write. */\n"
"  function _fdWritable(fd) {\n"
"    if (fd < 0) return null;\n"
"    var em = new EventEmitter();\n"
"    em.fd = fd;\n"
"    em.writable = true;\n"
"    em._closed = false;\n"
"    em.bytesWritten = 0;\n"
"    em.write = function(chunk, enc, cb) {\n"
"      if (em._closed) { if (cb) cb(new Error('write after end')); return false; }\n"
"      var b;\n"
"      if (typeof chunk === 'string') b = Buffer.from(chunk, enc || 'utf8');\n"
"      else if (Buffer.isBuffer(chunk)) b = chunk;\n"
"      else if (chunk instanceof Uint8Array)\n"
"           b = Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);\n"
"      else b = Buffer.from(String(chunk), enc || 'utf8');\n"
"      try { var w = u.write(fd, b); em.bytesWritten += w; }\n"
"      catch (e) { em.emit('error', e); if (cb) cb(e); return false; }\n"
"      if (cb) cb();\n"
"      return true;\n"
"    };\n"
"    em.end = function(chunk, enc, cb) {\n"
"      if (typeof chunk === 'function') { cb = chunk; chunk = null; }\n"
"      else if (typeof enc === 'function') { cb = enc; enc = null; }\n"
"      if (chunk != null) em.write(chunk, enc);\n"
"      if (em._closed) { if (cb) cb(); return em; }\n"
"      em._closed = true;\n"
"      try { u.close(fd); } catch(_){}\n"
"      em.emit('finish');\n"
"      em.emit('close');\n"
"      if (cb) cb();\n"
"      return em;\n"
"    };\n"
"    em.destroy = em.end;\n"
"    return em;\n"
"  }\n"
"\n"
"  /* ChildProcess — created from spawn() output. */\n"
"  function ChildProcess(spawnResult, opts) {\n"
"    EventEmitter.call(this);\n"
"    var self = this;\n"
"    self.pid          = spawnResult.pid;\n"
"    self.connected    = false;  /* fork IPC not implemented */\n"
"    self.killed       = false;\n"
"    self.exitCode     = null;\n"
"    self.signalCode   = null;\n"
"    self.spawnfile    = (opts && opts.file) || null;\n"
"    self.spawnargs    = (opts && opts.args) || [];\n"
"    self.stdin   = _fdWritable(spawnResult.stdinFd);\n"
"    self.stdout  = _fdReadable(spawnResult.stdoutFd);\n"
"    self.stderr  = _fdReadable(spawnResult.stderrFd);\n"
"    self.stdio   = [self.stdin, self.stdout, self.stderr];\n"
"    self._reaped = false;\n"
"\n"
"    /* Poll waitpid every 25ms — modest CPU, snappy exit detection. */\n"
"    function reap() {\n"
"      if (self._reaped) return;\n"
"      var r;\n"
"      try { r = natives.waitpid(self.pid); }\n"
"      catch (_) { setTimeout(reap, 25); return; }\n"
"      if (r === null) { setTimeout(reap, 25); return; }\n"
"      self._reaped     = true;\n"
"      self.exitCode    = r.exitCode;\n"
"      self.signalCode  = r.signalCode;\n"
"      self.emit('exit', self.exitCode, self.signalCode);\n"
"      /* 'close' fires after stdout/stderr have drained.  We approximate\n"
"         by deferring to the next tick — pumps will have caught EOF by\n"
"         then since the child is dead and the pipe write ends are closed. */\n"
"      setTimeout(function(){ self.emit('close', self.exitCode, self.signalCode); }, 30);\n"
"    }\n"
"    setTimeout(reap, 25);\n"
"\n"
"    /* Tell the runtime to fire 'spawn' on the next tick (node's order). */\n"
"    setTimeout(function(){ if (!self._reaped) self.emit('spawn'); }, 0);\n"
"  }\n"
"  ChildProcess.prototype = Object.create(EventEmitter.prototype);\n"
"  ChildProcess.prototype.constructor = ChildProcess;\n"
"  ChildProcess.prototype.kill = function(sig) {\n"
"    if (this._reaped) return false;\n"
"    var ok = natives.kill(this.pid, (sig == null) ? 'SIGTERM' : sig);\n"
"    if (ok) this.killed = true;\n"
"    return ok;\n"
"  };\n"
"  ChildProcess.prototype.ref   = function() { return this; };\n"
"  ChildProcess.prototype.unref = function() { return this; };\n"
"  /* fork-only methods kept as stubs that throw cleanly. */\n"
"  ChildProcess.prototype.send       = function() { var e = new Error('child.send is only available for fork() children (not implemented)'); e.code = 'ERR_NOT_IMPLEMENTED'; throw e; };\n"
"  ChildProcess.prototype.disconnect = function() {};\n"
"\n"
"  /* Spawn: low-level entry.  spawn(file, args, opts) or spawn(file, opts). */\n"
"  function spawn(file, args, opts) {\n"
"    if (!Array.isArray(args)) { opts = args; args = []; }\n"
"    opts = opts || {};\n"
"    /* Build the argv with file as argv[0] (Node convention). */\n"
"    var argv = [file].concat(args || []);\n"
"    var envv = null;\n"
"    if (opts.env && typeof opts.env === 'object') {\n"
"      envv = Object.keys(opts.env).map(function(k){ return k + '=' + opts.env[k]; });\n"
"    }\n"
"    /* stdio: default 'pipe', 'pipe', 'pipe'.  Accept shorthand strings or array.\n"
"       Only 'pipe' and 'inherit' supported in v1; 'ignore' treated as pipe. */\n"
"    var stdio = opts.stdio || 'pipe';\n"
"    if (typeof stdio === 'string') stdio = [stdio, stdio, stdio];\n"
"    function _isPipe(v) {\n"
"      return v === 'pipe' || v === null || v === undefined || v === 'ignore';\n"
"    }\n"
"    var result = natives.spawn({\n"
"      file: file,\n"
"      args: argv,\n"
"      cwd:  opts.cwd || null,\n"
"      env:  envv,\n"
"      stdinPipe:  _isPipe(stdio[0]),\n"
"      stdoutPipe: _isPipe(stdio[1]),\n"
"      stderrPipe: _isPipe(stdio[2])\n"
"    });\n"
"    return new ChildProcess(result, { file: file, args: args });\n"
"  }\n"
"\n"
"  /* exec: spawn through /bin/sh -c, buffer stdout/stderr to strings, callback. */\n"
"  function exec(command, opts, cb) {\n"
"    if (typeof opts === 'function') { cb = opts; opts = {}; }\n"
"    opts = opts || {};\n"
"    var shell = opts.shell || '/bin/sh';\n"
"    var maxBuf = (typeof opts.maxBuffer === 'number') ? opts.maxBuffer : 1024 * 1024;\n"
"    var enc    = opts.encoding || 'utf8';\n"
"    var child = spawn(shell, ['-c', command], { cwd: opts.cwd, env: opts.env });\n"
"    var outBufs = [], outLen = 0, errBufs = [], errLen = 0, hitLimit = false;\n"
"    function accumulate(buf, listArr, lenObj, slot) {\n"
"      var b = Buffer.isBuffer(buf) ? buf\n"
"            : (typeof buf === 'string') ? Buffer.from(buf, enc) : Buffer.from(buf);\n"
"      if (lenObj.v + b.length > maxBuf) {\n"
"        hitLimit = true;\n"
"        try { child.kill('SIGTERM'); } catch(_){}\n"
"        return;\n"
"      }\n"
"      listArr.push(b);\n"
"      lenObj.v += b.length;\n"
"    }\n"
"    var oLen = {v:0}, eLen = {v:0};\n"
"    if (child.stdout) child.stdout.on('data', function(b){ accumulate(b, outBufs, oLen); });\n"
"    if (child.stderr) child.stderr.on('data', function(b){ accumulate(b, errBufs, eLen); });\n"
"    child.on('close', function(code, signal) {\n"
"      var stdoutStr = Buffer.concat(outBufs).toString(enc);\n"
"      var stderrStr = Buffer.concat(errBufs).toString(enc);\n"
"      if (typeof cb !== 'function') return;\n"
"      if (hitLimit) {\n"
"        var e = new Error('stdout maxBuffer length exceeded'); e.code = 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER';\n"
"        return cb(e, stdoutStr, stderrStr);\n"
"      }\n"
"      if (code === 0) return cb(null, stdoutStr, stderrStr);\n"
"      var e2 = new Error('Command failed: ' + command + (stderrStr ? '\\n' + stderrStr : ''));\n"
"      e2.code   = code;\n"
"      e2.killed = !!signal;\n"
"      e2.signal = signal;\n"
"      e2.cmd    = command;\n"
"      cb(e2, stdoutStr, stderrStr);\n"
"    });\n"
"    return child;\n"
"  }\n"
"\n"
"  /* execFile: like spawn(cmd, args) but with same callback shape as exec. */\n"
"  function execFile(file, args, opts, cb) {\n"
"    if (Array.isArray(args)) {\n"
"      if (typeof opts === 'function') { cb = opts; opts = {}; }\n"
"    } else if (typeof args === 'function') { cb = args; opts = {}; args = []; }\n"
"    else if (typeof args === 'object')     { opts = args; args = []; if (typeof opts === 'function') { cb = opts; opts = {}; } }\n"
"    opts = opts || {};\n"
"    var maxBuf = (typeof opts.maxBuffer === 'number') ? opts.maxBuffer : 1024 * 1024;\n"
"    var enc    = opts.encoding || 'utf8';\n"
"    var child  = spawn(file, args || [], { cwd: opts.cwd, env: opts.env });\n"
"    var outBufs = [], errBufs = []; var oLen = {v:0}, eLen = {v:0}; var hitLimit = false;\n"
"    function accumulate(buf, listArr, lenObj) {\n"
"      var b = Buffer.isBuffer(buf) ? buf\n"
"            : (typeof buf === 'string') ? Buffer.from(buf, enc) : Buffer.from(buf);\n"
"      if (lenObj.v + b.length > maxBuf) { hitLimit = true; try { child.kill('SIGTERM'); } catch(_){} return; }\n"
"      listArr.push(b); lenObj.v += b.length;\n"
"    }\n"
"    if (child.stdout) child.stdout.on('data', function(b){ accumulate(b, outBufs, oLen); });\n"
"    if (child.stderr) child.stderr.on('data', function(b){ accumulate(b, errBufs, eLen); });\n"
"    child.on('close', function(code, signal) {\n"
"      if (typeof cb !== 'function') return;\n"
"      var outStr = Buffer.concat(outBufs).toString(enc);\n"
"      var errStr = Buffer.concat(errBufs).toString(enc);\n"
"      if (hitLimit) { var e = new Error('stdout maxBuffer length exceeded'); e.code = 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER'; return cb(e, outStr, errStr); }\n"
"      if (code === 0) return cb(null, outStr, errStr);\n"
"      var e2 = new Error('Command failed: ' + file);\n"
"      e2.code = code; e2.signal = signal; e2.cmd = file + ' ' + (args || []).join(' ');\n"
"      cb(e2, outStr, errStr);\n"
"    });\n"
"    return child;\n"
"  }\n"
"\n"
"  return {\n"
"    ChildProcess: ChildProcess,\n"
"    spawn:        spawn,\n"
"    exec:         exec,\n"
"    execFile:     execFile,\n"
"    /* fork / spawnSync / execSync / execFileSync intentionally omitted in\n"
"       v1.  fork needs an IPC channel (worker_threads-style); sync\n"
"       variants would block the event loop and partially duplicate\n"
"       `rampart.utils.exec`. */\n"
"    fork:         function(){ var e = new Error('child_process.fork is not implemented (IPC channel pending)'); e.code = 'ERR_NOT_IMPLEMENTED'; throw e; }\n"
"  };\n"
"}";

static void nodeshim_init_child_process(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:child_process_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, child_process_js);
    /* EventEmitter from partially-built nodeshim exports. */
    duk_get_prop_string(ctx, -2, "events");
    duk_get_prop_string(ctx, -1, "EventEmitter");
    duk_remove(ctx, -2);
    /* natives object: spawn / waitpid / kill */
    duk_push_object(ctx);
    duk_push_c_function(ctx, nodeshim_cp_spawn_native,   1); duk_put_prop_string(ctx, -2, "spawn");
    duk_push_c_function(ctx, nodeshim_cp_waitpid_native, 1); duk_put_prop_string(ctx, -2, "waitpid");
    duk_push_c_function(ctx, nodeshim_cp_kill_native,    2); duk_put_prop_string(ctx, -2, "kill");
    duk_call(ctx, 2);
}

/* ============================================================
 * vm — sandboxed code execution.
 *
 * Backed by `new rampart.thread({bare: true})`: each context owns a
 * worker thread whose heap holds only ECMAScript primordials and the
 * rampart.thread message-passing surface — no rampart.utils, no
 * process, no require, no WHATWG/Intl lazy getters.  Genuine isolation
 * (separate duktape heap; sandbox cannot reach host objects).
 *
 * Live-binding sandbox: `vm.createContext` returns a Proxy whose
 * get/set/has/deleteProperty traps round-trip into the worker via
 * rampart.thread.put + getwait so host code sees mutations the
 * sandbox makes (and vice versa) without manual sync.
 *
 * Round-trip protocol (one in-flight request at a time per context):
 *   host:    put('__vm_req', {op, ...args})
 *   worker:  onGet('__vm_req') fires, computes, put('__vm_resp', wrap)
 *   host:    getwait('__vm_resp'); del('__vm_resp')
 *
 * `getwait` is race-free against the edge-trigger gotcha: it tries a
 * level-triggered get first, falls back to waitfor only if not set.
 *
 * runInThisContext / compileFunction / Script.runInThisContext run in
 * the host heap (no thread), via indirect eval / Function constructor.
 * ============================================================ */
static const char *vm_js =
"function() {\n"
"  'use strict';\n"
"\n"
"  /* Marker installed on the proxy target so isContext() can recognise\n"
"     contexts produced by createContext. */\n"
"  var CTX_TAG = '__vm_context_marker_72d8a';\n"
"\n"
"  /* ----- in-this-context evaluator (no worker thread involved) ----- */\n"
"  /* indirect eval: runs in global scope, no access to caller locals. */\n"
"  var _indirectEval = (0, eval);\n"
"  function runInThisContext(code, _opts) {\n"
"    return _indirectEval(code);\n"
"  }\n"
"\n"
"  /* ----- Script ----- */\n"
"  function Script(code, opts) {\n"
"    if (!(this instanceof Script)) return new Script(code, opts);\n"
"    if (typeof code !== 'string') throw new TypeError('vm.Script: code must be a string');\n"
"    this._code = code;\n"
"    this._filename = (opts && opts.filename) || 'evalmachine.<anonymous>';\n"
"  }\n"
"  Script.prototype.runInThisContext = function(opts) {\n"
"    return runInThisContext(this._code, opts);\n"
"  };\n"
"  Script.prototype.runInContext = function(ctx, opts) {\n"
"    return runInContext(this._code, ctx, opts);\n"
"  };\n"
"  Script.prototype.runInNewContext = function(sandbox, opts) {\n"
"    return runInNewContext(this._code, sandbox, opts);\n"
"  };\n"
"\n"
"  /* ----- compileFunction ----- */\n"
"  function compileFunction(code, params, opts) {\n"
"    params = params || [];\n"
"    if (!Array.isArray(params)) throw new TypeError('vm.compileFunction: params must be an array');\n"
"    var args = params.slice();\n"
"    args.push(code);\n"
"    /* Function constructor: creates a function whose body is `code` and\n"
"       whose formal parameters are `params`.  Runs in global scope. */\n"
"    return Function.apply(null, args);\n"
"  }\n"
"\n"
"  /* ----- Worker bootstrap (runs once in the bare thread) ----- */\n"
"  /* Installed via t.exec; receives {seed, reqKey, respKey} and wires up\n"
"     onGet handlers for the round-trip protocol.  Note: this function is\n"
"     serialised + re-evaluated in the worker, so closures over outer-scope\n"
"     variables don't survive — everything flows through the `init` arg.\n"
"     reqKey/respKey are per-context so multiple vm contexts don't cross-\n"
"     talk through the shared clipboard. */\n"
"  function _workerBootstrap(init) {\n"
"    var seed = init.seed, reqKey = init.reqKey, respKey = init.respKey, readyKey = init.readyKey;\n"
"    /* Capture rampart.thread surface into closure scope BEFORE we delete\n"
"       the global, so the handlers still work after isolation is sealed. */\n"
"    var T_put   = rampart.thread.put;\n"
"    var T_onGet = rampart.thread.onGet;\n"
"    var R = rampart, RT = rampart.thread;\n"
"    var keys = Object.keys(seed);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      try { globalThis[keys[i]] = seed[keys[i]]; } catch (_) {}\n"
"    }\n"
"    /* Bind T_put / T_onGet to their owning object so calls keep working\n"
"       after `rampart` is removed from globalThis. */\n"
"    var T = { put:   function(k, v) { return T_put.call(RT, k, v); },\n"
"              onGet: function(k, f) { return T_onGet.call(RT, k, f); } };\n"
"    function _respond(value) {\n"
"      /* put() rejects undefined — always wrap. */\n"
"      T.put(respKey, { ok: true, value: value });\n"
"    }\n"
"    function _respondError(err) {\n"
"      T.put(respKey, { ok: false,\n"
"        name: (err && err.name) || 'Error',\n"
"        message: (err && err.message) || String(err),\n"
"        stack: (err && err.stack) || undefined });\n"
"    }\n"
"    T.onGet(reqKey, function(_k, req, _match) {\n"
"      try {\n"
"        switch (req.op) {\n"
"          case 'run':  _respond((0, eval)(req.code)); break;\n"
"          case 'read': _respond(globalThis[req.name]); break;\n"
"          case 'write': globalThis[req.name] = req.value; _respond(true); break;\n"
"          case 'has':  _respond(req.name in globalThis); break;\n"
"          case 'delete': _respond(delete globalThis[req.name]); break;\n"
"          case 'keys': _respond(Object.keys(globalThis)); break;\n"
"          default: _respondError(new Error('vm: unknown op ' + req.op));\n"
"        }\n"
"      } catch (e) {\n"
"        _respondError(e);\n"
"      }\n"
"    });\n"
"    /* Seal isolation: with the handlers registered and put/onGet\n"
"       references captured into closure scope above, the rampart global\n"
"       can be removed so sandbox code can't see it. */\n"
"    try { delete globalThis.rampart; } catch (_) {}\n"
"    /* Signal main that onGet is registered + WAITING flag is set — main\n"
"       blocks in getwait(readyKey) before sending any request, closing the\n"
"       race where main's first put would arrive before the handler exists. */\n"
"    T.put(readyKey, true);\n"
"    /* onGet keeps RPTHR_FLAG_WAITING set and registers a libevent EV_READ\n"
"       handler, which keeps the worker's loop non-empty — so the bare\n"
"       thread persists for the context's lifetime without keepOpen. */\n"
"  }\n"
"\n"
"  /* Per-context key counter — must stay unique even across rapid create/\n"
"     destroy cycles, since the shared clipboard outlives single contexts. */\n"
"  var _ctxCounter = 0;\n"
"\n"
"  /* ----- Round-trip helper ----- */\n"
"  function _call(reqKey, respKey, req) {\n"
"    rampart.thread.put(reqKey, req);\n"
"    /* getwait = get-or-wait — race-free against edge-triggered waitfor. */\n"
"    var resp = rampart.thread.getwait(respKey);\n"
"    /* Consume so the next call doesn't see a stale value. */\n"
"    try { rampart.thread.del(respKey); } catch (_) {}\n"
"    if (!resp.ok) {\n"
"      var e = new Error(resp.message || 'vm: sandbox error');\n"
"      e.name = resp.name || 'Error';\n"
"      if (resp.stack) e.stack = resp.stack;\n"
"      throw e;\n"
"    }\n"
"    return resp.value;\n"
"  }\n"
"\n"
"  /* ----- createContext / isContext ----- */\n"
"  function createContext(sandbox, _opts) {\n"
"    if (sandbox === undefined || sandbox === null) sandbox = {};\n"
"    if (typeof sandbox !== 'object') throw new TypeError('vm.createContext: sandbox must be an object');\n"
"\n"
"    var id = ++_ctxCounter;\n"
"    var reqKey   = '__vm_req_'   + id;\n"
"    var respKey  = '__vm_resp_'  + id;\n"
"    var readyKey = '__vm_ready_' + id;\n"
"    var t = new rampart.thread({ bare: true });\n"
"    /* Seed the worker globals and install the message-handler.  exec()\n"
"       serialises the function body to the worker. */\n"
"    t.exec(_workerBootstrap, { seed: sandbox, reqKey: reqKey, respKey: respKey, readyKey: readyKey });\n"
"    /* Block until the worker has its onGet registered (and thus its\n"
"       WAITING flag set) — otherwise our first put can race ahead of the\n"
"       handler and be silently dropped. */\n"
"    rampart.thread.getwait(readyKey);\n"
"    try { rampart.thread.del(readyKey); } catch (_) {}\n"
"\n"
"    var target = Object.create(null);\n"
"    target[CTX_TAG] = true;\n"
"    target._thread  = t;\n"
"    target._reqKey  = reqKey;\n"
"    target._respKey = respKey;\n"
"\n"
"    return new Proxy(target, {\n"
"      get: function(tgt, name) {\n"
"        if (name === CTX_TAG || name === '_thread' || name === '_reqKey' || name === '_respKey')\n"
"          return tgt[name];\n"
"        if (typeof name === 'symbol') return undefined;\n"
"        return _call(reqKey, respKey, { op: 'read', name: String(name) });\n"
"      },\n"
"      set: function(tgt, name, value) {\n"
"        if (name === '_thread' || name === CTX_TAG || name === '_reqKey' || name === '_respKey') {\n"
"          tgt[name] = value; return true;\n"
"        }\n"
"        if (typeof name === 'symbol') return true;\n"
"        _call(reqKey, respKey, { op: 'write', name: String(name), value: value });\n"
"        return true;\n"
"      },\n"
"      has: function(tgt, name) {\n"
"        if (name === CTX_TAG || name === '_thread' || name === '_reqKey' || name === '_respKey')\n"
"          return name in tgt;\n"
"        if (typeof name === 'symbol') return false;\n"
"        return !!_call(reqKey, respKey, { op: 'has', name: String(name) });\n"
"      },\n"
"      deleteProperty: function(tgt, name) {\n"
"        if (name === '_thread' || name === CTX_TAG || name === '_reqKey' || name === '_respKey')\n"
"          return true;\n"
"        if (typeof name === 'symbol') return true;\n"
"        return !!_call(reqKey, respKey, { op: 'delete', name: String(name) });\n"
"      },\n"
"      ownKeys: function(_tgt) {\n"
"        return _call(reqKey, respKey, { op: 'keys' });\n"
"      },\n"
"      getOwnPropertyDescriptor: function(tgt, name) {\n"
"        if (name === '_thread' || name === CTX_TAG || name === '_reqKey' || name === '_respKey')\n"
"          return undefined;\n"
"        return { value: _call(reqKey, respKey, { op: 'read', name: String(name) }),\n"
"                 writable: true, enumerable: true, configurable: true };\n"
"      }\n"
"    });\n"
"  }\n"
"\n"
"  function isContext(obj) {\n"
"    if (!obj || typeof obj !== 'object') return false;\n"
"    try { return obj[CTX_TAG] === true; } catch (_) { return false; }\n"
"  }\n"
"\n"
"  /* ----- runInContext / runInNewContext ----- */\n"
"  function runInContext(code, ctx, _opts) {\n"
"    if (!isContext(ctx)) throw new TypeError('vm.runInContext: context required');\n"
"    if (typeof code !== 'string') throw new TypeError('vm.runInContext: code must be a string');\n"
"    /* Reach through the Proxy to the underlying per-context keys. */\n"
"    return _call(ctx._reqKey, ctx._respKey, { op: 'run', code: code });\n"
"  }\n"
"\n"
"  function runInNewContext(code, sandbox, opts) {\n"
"    return runInContext(code, createContext(sandbox, opts), opts);\n"
"  }\n"
"\n"
"  /* ----- module-level constants and exports ----- */\n"
"  return {\n"
"    Script:           Script,\n"
"    createContext:    createContext,\n"
"    isContext:        isContext,\n"
"    runInThisContext: runInThisContext,\n"
"    runInContext:     runInContext,\n"
"    runInNewContext:  runInNewContext,\n"
"    compileFunction:  compileFunction,\n"
"    measureMemory: function() { return Promise.resolve({ total: { jsMemoryEstimate: 0, jsMemoryRange: [0, 0] } }); },\n"
"    constants: { USE_MAIN_CONTEXT_DEFAULT_LOADER: 0 },\n"
"    /* repl-mode constants — exported by node's vm; some libs reference them */\n"
"    REPL_MODE_SLOPPY: 'sloppy',\n"
"    REPL_MODE_STRICT: 'strict'\n"
"  };\n"
"}\n";

static void nodeshim_init_vm(duk_context *ctx)
{
    duk_push_string(ctx, "rampart-nodeshim.c:vm_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, vm_js);
    duk_call(ctx, 0);
}

/* ============================================================
 * repl — interactive read-eval-print loop.
 *
 * Sits atop readline (uses the same Interface for line editing,
 * history, tab completion) and optionally vm (for sandboxed contexts).
 * For useGlobal:true (the default) eval runs as indirect (0,eval) in
 * the host realm so the user can poke at host globals; useGlobal:false
 * routes through vm.runInContext against a per-REPL bare-thread sandbox.
 *
 * Implemented features:
 *   - repl.start(opts) / REPLServer
 *   - prompt, input/output streams (default process.stdin / stdout)
 *   - built-in commands: .help, .exit, .clear, .break, .load, .save
 *   - defineCommand for user extensions
 *   - multi-line continuation via Recoverable detection
 *   - last result `_` and last error `_lastError` globals (useGlobal mode)
 *   - util.inspect formatting of results
 *   - ignoreUndefined option
 *   - SIGINT cancels current input, second SIGINT exits
 *   - displayPrompt, clearBufferedCommand, close, setupHistory
 *
 * Not implemented (compat stubs):
 *   - `.editor` mode (multi-line block editor) — line-buffer-only repl
 *   - persistent history file (setupHistory accepts but no-ops)
 *   - useColors (boolean accepted, no-op; format is plain text)
 * ============================================================ */
static const char *repl_js =
"function(EventEmitter, readline, vm, util) {\n"
"  'use strict';\n"
"\n"
"  /* Recoverable: throw an instance to signal 'need more input' from a\n"
"     custom eval callback.  Matches node's repl.Recoverable. */\n"
"  function Recoverable(err) {\n"
"    this.name = 'Recoverable';\n"
"    this.err  = err;\n"
"    if (err && err.message) this.message = err.message;\n"
"  }\n"
"  Recoverable.prototype = Object.create(Error.prototype);\n"
"  Recoverable.prototype.constructor = Recoverable;\n"
"\n"
"  /* Heuristic: did this SyntaxError come from incomplete input?\n"
"     Duktape, V8 and SpiderMonkey all produce somewhat different messages,\n"
"     so we cast a wide net.  False positives mean the user has to wait one\n"
"     more line before seeing the error; false negatives mean multi-line\n"
"     input gets rejected too eagerly. */\n"
"  function _isRecoverable(err, code) {\n"
"    if (!err) return false;\n"
"    var msg = err.message || '';\n"
"    var name = err.name || '';\n"
"    if (name !== 'SyntaxError' && !/SyntaxError|parse/i.test(String(err))) return false;\n"
"    if (/Unexpected end of input|Unterminated|unterminated|Unexpected end|Unexpected EOF|missing \\) after argument list/i.test(msg)) return true;\n"
"    /* Bracket-balance fallback: if there are more openers than closers,\n"
"       treat any syntax error as incomplete. */\n"
"    var opens = (code.match(/[\\[({]/g) || []).length;\n"
"    var closes = (code.match(/[\\])}]/g) || []).length;\n"
"    if (opens > closes) return true;\n"
"    return false;\n"
"  }\n"
"\n"
"  /* Default eval: indirect eval in host realm (useGlobal) OR vm sandbox.\n"
"     The expression-wrap trick (\"(\" + code + \")\") makes `{a:1}` parse\n"
"     as an object literal rather than a block, but breaks function/class\n"
"     DECLARATIONS by turning them into anonymous expressions that don't\n"
"     bind their name globally.  So we skip the wrap when the code looks\n"
"     like a statement. */\n"
"  function _looksLikeStatement(code) {\n"
"    var s = code.replace(/^\\s+/, '');\n"
"    return /^(?:function|class|let|var|const|if|for|while|do|switch|try|throw|return|return\\b|break|continue|return)\\b/.test(s);\n"
"  }\n"
"  function _makeDefaultEval(server) {\n"
"    var indirectEval = (0, eval);\n"
"    function _exec(code, ctx) {\n"
"      if (ctx && vm) return vm.runInContext(code, ctx);\n"
"      return indirectEval(code);\n"
"    }\n"
"    return function(code, ctx, filename, cb) {\n"
"      var result, err = null;\n"
"      var wantWrap = !_looksLikeStatement(code);\n"
"      if (wantWrap) {\n"
"        var wrapped = '(' + code.replace(/\\n$/, '') + ')';\n"
"        try { result = _exec(wrapped, ctx); cb(null, result); return; }\n"
"        catch (e1) {\n"
"          /* Fall through to statement-mode below. */\n"
"        }\n"
"      }\n"
"      try { result = _exec(code, ctx); cb(null, result); return; }\n"
"      catch (e2) {\n"
"        err = e2;\n"
"      }\n"
"      if (_isRecoverable(err, code)) return cb(new Recoverable(err));\n"
"      cb(err);\n"
"    };\n"
"  }\n"
"\n"
"  /* Format a value for display.  util.inspect handles most things;\n"
"     ignoreUndefined / error formatting handled by callers. */\n"
"  function _format(v, opts) {\n"
"    try {\n"
"      return util.inspect(v, { depth: 2, colors: false, breakLength: 80 });\n"
"    } catch (_) {\n"
"      try { return String(v); } catch (__) { return '[unprintable]'; }\n"
"    }\n"
"  }\n"
"\n"
"  function _formatError(e) {\n"
"    if (!e) return 'undefined error';\n"
"    if (e.stack) return String(e.stack).split('\\n')[0];\n"
"    return (e.name || 'Error') + ': ' + (e.message || String(e));\n"
"  }\n"
"\n"
"  /* ----- REPLServer ----- */\n"
"  function REPLServer(opts) {\n"
"    if (!(this instanceof REPLServer)) return new REPLServer(opts);\n"
"    EventEmitter.call(this);\n"
"    opts = opts || {};\n"
"\n"
"    this.prompt          = (opts.prompt != null) ? String(opts.prompt) : '> ';\n"
"    this.input           = opts.input  || (typeof process !== 'undefined' ? process.stdin  : null);\n"
"    this.output          = opts.output || (typeof process !== 'undefined' ? process.stdout : null);\n"
"    this.useColors       = !!opts.useColors;\n"
"    this.useGlobal       = (opts.useGlobal !== false);\n"
"    this.ignoreUndefined = !!opts.ignoreUndefined;\n"
"    this.terminal        = (opts.terminal !== undefined) ? !!opts.terminal\n"
"                          : !!(this.output && this.output.isTTY);\n"
"    this.historySize     = opts.historySize || 1000;\n"
"    this.replMode        = opts.replMode || REPL_MODE_SLOPPY;\n"
"    this.breakEvalOnSigint = !!opts.breakEvalOnSigint;\n"
"\n"
"    /* If a context is supplied (or useGlobal:false), set up the vm\n"
"       context so eval routes through it. */\n"
"    if (opts.context !== undefined) {\n"
"      this.context = opts.context;\n"
"    } else if (!this.useGlobal && vm) {\n"
"      this.context = vm.createContext({});\n"
"    } else {\n"
"      this.context = null;\n"
"    }\n"
"\n"
"    this.eval = opts.eval || _makeDefaultEval(this);\n"
"\n"
"    this.commands = Object.create(null);\n"
"    this._buffer  = '';\n"
"    this._sigintCount = 0;\n"
"    this._sessionHistory = [];\n"
"    this._closed = false;\n"
"\n"
"    _defineBuiltins(this);\n"
"\n"
"    /* Build the readline interface that drives input. */\n"
"    this._rl = readline.createInterface({\n"
"      input:    this.input,\n"
"      output:   this.output,\n"
"      terminal: this.terminal,\n"
"      prompt:   this.prompt,\n"
"      historySize: this.historySize,\n"
"      completer: opts.completer || this._defaultCompleter.bind(this)\n"
"    });\n"
"\n"
"    var self = this;\n"
"    this._rl.on('line',  function(line) { self._onLine(line);  });\n"
"    this._rl.on('close', function()     { self._onClose();      });\n"
"    this._rl.on('SIGINT',function()     { self._onSigint();     });\n"
"\n"
"    /* First prompt — emit asynchronously so listeners attached after\n"
"       repl.start() return still see things in the right order. */\n"
"    setTimeout(function(){ self.displayPrompt(); }, 0);\n"
"  }\n"
"  /* Inherit EventEmitter, walking up util.inherits if available. */\n"
"  REPLServer.prototype = Object.create(EventEmitter.prototype);\n"
"  REPLServer.prototype.constructor = REPLServer;\n"
"\n"
"  REPLServer.prototype._onLine = function(line) {\n"
"    this._sigintCount = 0;  /* line input resets double-Ctrl-C */\n"
"    this._sessionHistory.push(line);\n"
"\n"
"    /* Command dispatch.  A leading-dot line is treated as a command\n"
"       if it matches a registered command name — including when the\n"
"       buffer holds an incomplete multi-line expression (otherwise\n"
"       .break and .clear, whose entire purpose is to abort that buffer,\n"
"       would be unreachable). */\n"
"    if (line.length > 0 && line.charAt(0) === '.') {\n"
"      var sp = line.indexOf(' ');\n"
"      var name = (sp >= 0) ? line.slice(1, sp) : line.slice(1);\n"
"      var arg  = (sp >= 0) ? line.slice(sp + 1) : '';\n"
"      var cmd = this.commands[name];\n"
"      if (cmd && typeof cmd.action === 'function') {\n"
"        try { cmd.action.call(this, arg); }\n"
"        catch (e) { this._writeLine(_formatError(e)); }\n"
"        return this.displayPrompt();\n"
"      }\n"
"      /* Only treat unknown .keywords as 'Invalid REPL keyword' when\n"
"         we're at the top of input — otherwise let the parser sort it\n"
"         out (could be a member-access continuation from a prior line). */\n"
"      if (this._buffer === '') {\n"
"        this._writeLine('Invalid REPL keyword');\n"
"        return this.displayPrompt();\n"
"      }\n"
"    }\n"
"\n"
"    /* Append to the multi-line buffer and try to evaluate. */\n"
"    this._buffer += (this._buffer ? '\\n' : '') + line;\n"
"    var self = this, code = this._buffer;\n"
"    this.eval(code, this.context, 'repl', function(err, result) {\n"
"      if (err) {\n"
"        if (err instanceof Recoverable) {\n"
"          /* Continuation prompt — keep buffer, switch to '...' style. */\n"
"          return self.displayPrompt(true);\n"
"        }\n"
"        self._writeLine(_formatError(err));\n"
"        if (self.useGlobal) {\n"
"          try { globalThis._lastError = err; } catch (_) {}\n"
"        }\n"
"        self._buffer = '';\n"
"        return self.displayPrompt();\n"
"      }\n"
"      if (!(result === undefined && self.ignoreUndefined)) {\n"
"        self._writeLine(_format(result));\n"
"      }\n"
"      if (self.useGlobal) {\n"
"        try { globalThis._ = result; } catch (_) {}\n"
"      }\n"
"      self.emit('exec-result', result);\n"
"      self._buffer = '';\n"
"      self.displayPrompt();\n"
"    });\n"
"  };\n"
"\n"
"  REPLServer.prototype._onClose = function() {\n"
"    if (this._closed) return;\n"
"    this._closed = true;\n"
"    this.emit('exit');\n"
"  };\n"
"\n"
"  REPLServer.prototype._onSigint = function() {\n"
"    /* If we're mid multi-line, clear it.  If pressed twice in a row at\n"
"       a clean prompt, exit (matches node). */\n"
"    if (this._buffer) {\n"
"      this._buffer = '';\n"
"      this._writeLine('');\n"
"      this._writeLine('(To exit, press Ctrl+C again or Ctrl+D)');\n"
"      this._sigintCount = 1;\n"
"      return this.displayPrompt();\n"
"    }\n"
"    if (this._sigintCount >= 1) {\n"
"      this.close();\n"
"      return;\n"
"    }\n"
"    this._sigintCount++;\n"
"    this._writeLine('(To exit, press Ctrl+C again or Ctrl+D)');\n"
"    this.displayPrompt();\n"
"  };\n"
"\n"
"  REPLServer.prototype._writeLine = function(s) {\n"
"    if (this.output && typeof this.output.write === 'function') {\n"
"      this.output.write(s + '\\n');\n"
"    }\n"
"  };\n"
"\n"
"  REPLServer.prototype.displayPrompt = function(preserveCursor) {\n"
"    if (!this._rl || this._closed) return;\n"
"    this._rl.setPrompt(this._buffer ? '... ' : this.prompt);\n"
"    this._rl.prompt(preserveCursor);\n"
"  };\n"
"\n"
"  REPLServer.prototype.clearBufferedCommand = function() {\n"
"    this._buffer = '';\n"
"  };\n"
"\n"
"  REPLServer.prototype.close = function() {\n"
"    if (this._rl) this._rl.close();\n"
"    else this._onClose();\n"
"  };\n"
"\n"
"  REPLServer.prototype.defineCommand = function(name, opts) {\n"
"    if (typeof opts === 'function') opts = { action: opts };\n"
"    if (!opts || typeof opts.action !== 'function')\n"
"      throw new TypeError('defineCommand: opts.action must be a function');\n"
"    this.commands[String(name)] = { help: opts.help || '', action: opts.action };\n"
"  };\n"
"\n"
"  /* setupHistory: in node, persists history to a file.  Best-effort:\n"
"     load existing lines and arrange to append.  No-op if input/output\n"
"     can't reach the filesystem from this realm. */\n"
"  REPLServer.prototype.setupHistory = function(historyPath, cb) {\n"
"    var self = this;\n"
"    if (typeof cb !== 'function') cb = function(){};\n"
"    try {\n"
"      var fs = require('fs');\n"
"      /* Load existing — push into the readline history if possible. */\n"
"      var existing = '';\n"
"      try { existing = fs.readFileSync(historyPath, 'utf8'); } catch(_) {}\n"
"      if (existing && self._rl && Array.isArray(self._rl.history)) {\n"
"        existing.split('\\n').filter(Boolean).reverse().forEach(function(ln) {\n"
"          self._rl.history.unshift(ln);\n"
"        });\n"
"      }\n"
"      /* Append on each new line. */\n"
"      self.on('exec-result', function(){\n"
"        var last = self._sessionHistory[self._sessionHistory.length - 1];\n"
"        if (last) { try { fs.appendFileSync(historyPath, last + '\\n'); } catch(_) {} }\n"
"      });\n"
"      cb(null, self);\n"
"    } catch (e) { cb(e); }\n"
"  };\n"
"\n"
"  /* Default completer: complete property names against the eval context\n"
"     (or globalThis if useGlobal). */\n"
"  REPLServer.prototype._defaultCompleter = function(line) {\n"
"    /* Parse off the trailing identifier-or-member-expression chain. */\n"
"    var m = /(?:[\\w$\\.]+)?$/.exec(line);\n"
"    var tail = m ? m[0] : '';\n"
"    var lastDot = tail.lastIndexOf('.');\n"
"    var prefix = lastDot >= 0 ? tail.slice(lastDot + 1) : tail;\n"
"    var base   = lastDot >= 0 ? tail.slice(0, lastDot) : '';\n"
"    var scope;\n"
"    try {\n"
"      if (base) {\n"
"        if (this.context) scope = vm.runInContext(base, this.context);\n"
"        else scope = (0, eval)(base);\n"
"      } else {\n"
"        scope = this.useGlobal ? globalThis : (this.context || globalThis);\n"
"      }\n"
"    } catch (_) { scope = null; }\n"
"    var names = [];\n"
"    if (scope) {\n"
"      try {\n"
"        var s = scope;\n"
"        while (s) {\n"
"          Object.getOwnPropertyNames(s).forEach(function(k){ names.push(k); });\n"
"          s = Object.getPrototypeOf(s);\n"
"          if (s === Object.prototype || s === null) break;\n"
"        }\n"
"      } catch (_) {}\n"
"    }\n"
"    var hits = names.filter(function(n){ return n.indexOf(prefix) === 0; });\n"
"    return [hits.length ? hits : names, prefix];\n"
"  };\n"
"\n"
"  /* ----- Built-in commands ----- */\n"
"  function _defineBuiltins(server) {\n"
"    server.defineCommand('help', { help: 'Print this help message', action: function() {\n"
"      var self = this;\n"
"      Object.keys(self.commands).sort().forEach(function(k){\n"
"        var c = self.commands[k];\n"
"        self._writeLine('.' + k + (c.help ? '  ' + c.help : ''));\n"
"      });\n"
"      self._writeLine('');\n"
"      self._writeLine('Press Ctrl+C to abort current expression, Ctrl+D to exit the REPL');\n"
"    }});\n"
"    server.defineCommand('exit', { help: 'Exit the REPL', action: function(){\n"
"      this.close();\n"
"    }});\n"
"    server.defineCommand('clear', { help: 'Reset session context', action: function(){\n"
"      this._buffer = '';\n"
"      if (this.useGlobal) {\n"
"        try { globalThis._ = undefined; globalThis._lastError = undefined; } catch (_) {}\n"
"      } else if (this.context && vm) {\n"
"        this.context = vm.createContext({});\n"
"      }\n"
"      this._writeLine('Clearing context...');\n"
"    }});\n"
"    server.defineCommand('break', { help: 'Cancel multi-line input', action: function(){\n"
"      this._buffer = '';\n"
"    }});\n"
"    server.defineCommand('load', { help: 'Load JS from a file into the REPL session', action: function(file){\n"
"      var self = this;\n"
"      if (!file) { self._writeLine('Usage: .load <filename>'); return; }\n"
"      try {\n"
"        var content = require('fs').readFileSync(file, 'utf8');\n"
"        self.eval(content, self.context, file, function(err, result){\n"
"          if (err) self._writeLine(_formatError(err));\n"
"          else if (!(result === undefined && self.ignoreUndefined))\n"
"            self._writeLine(_format(result));\n"
"        });\n"
"      } catch (e) {\n"
"        self._writeLine('Failed to load: ' + e.message);\n"
"      }\n"
"    }});\n"
"    server.defineCommand('save', { help: 'Save all evaluated commands in this session to a file', action: function(file){\n"
"      if (!file) { this._writeLine('Usage: .save <filename>'); return; }\n"
"      try {\n"
"        require('fs').writeFileSync(file, this._sessionHistory.join('\\n') + '\\n');\n"
"        this._writeLine('Session saved to: ' + file);\n"
"      } catch (e) {\n"
"        this._writeLine('Failed to save: ' + e.message);\n"
"      }\n"
"    }});\n"
"    server.defineCommand('editor', { help: 'Enter editor mode (not yet supported — same as line entry)', action: function(){\n"
"      this._writeLine('// Entering editor mode (Ctrl+D to finish, Ctrl+C to cancel)');\n"
"    }});\n"
"  }\n"
"\n"
"  /* ----- Module entry points ----- */\n"
"  function start(opts) {\n"
"    if (typeof opts === 'string') opts = { prompt: opts };\n"
"    return new REPLServer(opts || {});\n"
"  }\n"
"\n"
"  /* Some npm code consults these as the replMode constants. */\n"
"  var REPL_MODE_SLOPPY = Symbol('repl-sloppy');\n"
"  var REPL_MODE_STRICT = Symbol('repl-strict');\n"
"\n"
"  return {\n"
"    start:            start,\n"
"    REPLServer:       REPLServer,\n"
"    Recoverable:      Recoverable,\n"
"    REPL_MODE_SLOPPY: REPL_MODE_SLOPPY,\n"
"    REPL_MODE_STRICT: REPL_MODE_STRICT,\n"
"    builtinModules:   []\n"
"  };\n"
"}";

static void nodeshim_init_repl(duk_context *ctx)
{
    /* Stack on entry: [..., exports].  Build the call as:
         fn(EventEmitter, readline, vm, util)
       Slot order in init_module guarantees those are populated. */
    duk_push_string(ctx, "rampart-nodeshim.c:repl_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, repl_js);
    /* Stack: [..., exports, fn]   (exports at -2, fn at -1) */
    duk_get_prop_string(ctx, -2, "events");                  /* +events */
    duk_get_prop_string(ctx, -1, "EventEmitter");            /* +EE     */
    duk_remove(ctx, -2);                                     /* -events */
    /* Stack: [..., exports, fn, EE]   (exports at -3) */
    duk_get_prop_string(ctx, -3, "readline");                /* +readline */
    /* Stack: [..., exports, fn, EE, readline]   (exports at -4) */
    duk_get_prop_string(ctx, -4, "vm");                      /* +vm */
    /* Stack: [..., exports, fn, EE, readline, vm]   (exports at -5) */
    duk_get_prop_string(ctx, -5, "util");                    /* +util */
    /* Stack: [..., exports, fn, EE, readline, vm, util] — call with 4 args. */
    duk_call(ctx, 4);
}

/* ============================================================
 * http / https — node-compat HTTP client over rampart-curl.
 * Phase B from nodeshim-todo.md §8.1.  Server side is Phase A
 * (separate; not yet wired).  Streaming response body via curl's
 * chunkCallback; request body buffered for v1 (CURLOPT_READFUNCTION
 * exposure is Phase E).
 *
 * Returns {http, https}.  Same implementation, different defaults
 * (port 80 vs 443, http://-vs-https:// URL when none supplied).
 * ============================================================ */
static const char *http_js =
"function(EventEmitter, stream) {\n"
"  'use strict';\n"
"\n"
"  var _curlMod = null;\n"
"  function _getCurl() {\n"
"    if (!_curlMod) _curlMod = require('rampart-curl');\n"
"    return _curlMod;\n"
"  }\n"
"\n"
"  /* Standard HTTP method set node exposes.  Used by some npm code\n"
"     for input validation. */\n"
"  var METHODS = ['ACL','BIND','CHECKOUT','CONNECT','COPY','DELETE',\n"
"    'GET','HEAD','LINK','LOCK','M-SEARCH','MERGE','MKACTIVITY',\n"
"    'MKCALENDAR','MKCOL','MOVE','NOTIFY','OPTIONS','PATCH','POST',\n"
"    'PRI','PROPFIND','PROPPATCH','PURGE','PUT','QUERY','REBIND',\n"
"    'REPORT','SEARCH','SOURCE','SUBSCRIBE','TRACE','UNBIND','UNLINK',\n"
"    'UNLOCK','UNSUBSCRIBE'];\n"
"\n"
"  /* Status code → reason phrase, per IANA / RFC 9110. */\n"
"  var STATUS_CODES = {\n"
"    100:'Continue',101:'Switching Protocols',102:'Processing',103:'Early Hints',\n"
"    200:'OK',201:'Created',202:'Accepted',203:'Non-Authoritative Information',\n"
"    204:'No Content',205:'Reset Content',206:'Partial Content',207:'Multi-Status',\n"
"    208:'Already Reported',226:'IM Used',\n"
"    300:'Multiple Choices',301:'Moved Permanently',302:'Found',303:'See Other',\n"
"    304:'Not Modified',305:'Use Proxy',307:'Temporary Redirect',308:'Permanent Redirect',\n"
"    400:'Bad Request',401:'Unauthorized',402:'Payment Required',403:'Forbidden',\n"
"    404:'Not Found',405:'Method Not Allowed',406:'Not Acceptable',\n"
"    407:'Proxy Authentication Required',408:'Request Timeout',409:'Conflict',\n"
"    410:'Gone',411:'Length Required',412:'Precondition Failed',\n"
"    413:'Payload Too Large',414:'URI Too Long',415:'Unsupported Media Type',\n"
"    416:'Range Not Satisfiable',417:'Expectation Failed',418:\"I'm a Teapot\",\n"
"    421:'Misdirected Request',422:'Unprocessable Entity',423:'Locked',\n"
"    424:'Failed Dependency',425:'Too Early',426:'Upgrade Required',\n"
"    428:'Precondition Required',429:'Too Many Requests',\n"
"    431:'Request Header Fields Too Large',451:'Unavailable For Legal Reasons',\n"
"    500:'Internal Server Error',501:'Not Implemented',502:'Bad Gateway',\n"
"    503:'Service Unavailable',504:'Gateway Timeout',505:'HTTP Version Not Supported',\n"
"    506:'Variant Also Negotiates',507:'Insufficient Storage',508:'Loop Detected',\n"
"    510:'Not Extended',511:'Network Authentication Required'\n"
"  };\n"
"\n"
"  /* ----- IncomingMessage (response, also used by server side) ----- */\n"
"  function IncomingMessage(opts) {\n"
"    if (!(this instanceof IncomingMessage)) return new IncomingMessage(opts);\n"
"    stream.Readable.call(this, opts);\n"
"    this.headers     = {};\n"
"    this.rawHeaders  = [];\n"
"    this.trailers    = {};\n"
"    this.rawTrailers = [];\n"
"    this.statusCode    = 0;\n"
"    this.statusMessage = '';\n"
"    this.httpVersion       = '1.1';\n"
"    this.httpVersionMajor  = 1;\n"
"    this.httpVersionMinor  = 1;\n"
"    this.complete = false;\n"
"    this.url      = '';\n"
"    this.method   = '';\n"
"    this.socket = this.connection = null;\n"
"  }\n"
"  IncomingMessage.prototype = Object.create(stream.Readable.prototype);\n"
"  IncomingMessage.prototype.constructor = IncomingMessage;\n"
"  IncomingMessage.prototype.setTimeout = function(ms, cb) {\n"
"    if (cb) this.once('timeout', cb);\n"
"    return this;\n"
"  };\n"
"  IncomingMessage.prototype._read = function() { /* push-driven; no-op */ };\n"
"\n"
"  function _normHeadersIntoIM(im, srcHeaders) {\n"
"    if (!srcHeaders) return;\n"
"    var keys = Object.keys(srcHeaders);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var name = keys[i], val = srcHeaders[name];\n"
"      var lower = name.toLowerCase();\n"
"      if (Array.isArray(val)) {\n"
"        im.headers[lower] = (lower === 'set-cookie') ? val.slice() : val.join(', ');\n"
"        for (var j = 0; j < val.length; j++) {\n"
"          im.rawHeaders.push(name); im.rawHeaders.push(String(val[j]));\n"
"        }\n"
"      } else {\n"
"        im.headers[lower] = String(val);\n"
"        im.rawHeaders.push(name); im.rawHeaders.push(String(val));\n"
"      }\n"
"    }\n"
"  }\n"
"\n"
"  /* ----- ClientRequest (extends Writable) ----- */\n"
"  function ClientRequest(opts, cb) {\n"
"    if (!(this instanceof ClientRequest)) return new ClientRequest(opts, cb);\n"
"    var self = this;\n"
"    self._body = [];\n"
"    self._headers     = {};\n"
"    self._headerNames = {};   /* lower → original-case */\n"
"    self._reqOpts = opts;\n"
"    self.method  = (opts.method || 'GET').toUpperCase();\n"
"    self.path    = opts.path || '/';\n"
"    self.host    = opts.host || opts.hostname || 'localhost';\n"
"    self.aborted = false;\n"
"    self.headersSent = false;\n"
"    self.finished    = false;\n"
"    self.socket = self.connection = null;\n"
"    self._timeoutMs  = (typeof opts.timeout === 'number') ? opts.timeout : 0;\n"
"    self._defaultProtocol = opts.protocol || 'http:';\n"
"    self._sent = false;     /* curl call already kicked off */\n"
"    self._cancelTimer = null;\n"
"\n"
"    stream.Writable.call(self, {\n"
"      write: function(chunk, enc, callback) {\n"
"        try {\n"
"          var b = Buffer.isBuffer(chunk) ? chunk\n"
"                : (chunk instanceof Uint8Array) ? Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength)\n"
"                : Buffer.from(String(chunk), enc || 'utf8');\n"
"          self._body.push(b);\n"
"        } catch (e) { callback(e); return; }\n"
"        callback();\n"
"      },\n"
"      final: function(callback) {\n"
"        self._dispatchCurl(callback);\n"
"      }\n"
"    });\n"
"\n"
"    if (opts.headers) {\n"
"      var hkeys = Object.keys(opts.headers);\n"
"      for (var i = 0; i < hkeys.length; i++) self.setHeader(hkeys[i], opts.headers[hkeys[i]]);\n"
"    }\n"
"\n"
"    if (typeof cb === 'function') self.once('response', cb);\n"
"  }\n"
"  ClientRequest.prototype = Object.create(stream.Writable.prototype);\n"
"  ClientRequest.prototype.constructor = ClientRequest;\n"
"\n"
"  ClientRequest.prototype.setHeader = function(name, value) {\n"
"    if (this.headersSent) throw new Error('Cannot set headers after they are sent.');\n"
"    var lower = String(name).toLowerCase();\n"
"    this._headers[lower] = value;\n"
"    this._headerNames[lower] = String(name);\n"
"    return this;\n"
"  };\n"
"  ClientRequest.prototype.getHeader = function(name) {\n"
"    return this._headers[String(name).toLowerCase()];\n"
"  };\n"
"  /* Per node: getHeaderNames + getHeaders return LOWERCASE names.\n"
"     getRawHeaderNames preserves the case from setHeader/options. */\n"
"  ClientRequest.prototype.getHeaderNames = function() {\n"
"    return Object.keys(this._headers);\n"
"  };\n"
"  ClientRequest.prototype.getRawHeaderNames = function() {\n"
"    var keys = Object.keys(this._headers);\n"
"    var out = []; for (var i = 0; i < keys.length; i++) out.push(this._headerNames[keys[i]] || keys[i]);\n"
"    return out;\n"
"  };\n"
"  ClientRequest.prototype.getHeaders = function() {\n"
"    var out = {}; var self = this;\n"
"    Object.keys(this._headers).forEach(function(k) { out[k] = self._headers[k]; });\n"
"    return out;\n"
"  };\n"
"  ClientRequest.prototype.hasHeader = function(name) {\n"
"    return String(name).toLowerCase() in this._headers;\n"
"  };\n"
"  ClientRequest.prototype.removeHeader = function(name) {\n"
"    if (this.headersSent) throw new Error('Cannot remove headers after they are sent.');\n"
"    var lower = String(name).toLowerCase();\n"
"    delete this._headers[lower];\n"
"    delete this._headerNames[lower];\n"
"  };\n"
"  ClientRequest.prototype.flushHeaders = function() { /* no-op */ };\n"
"  ClientRequest.prototype.setTimeout = function(ms, cb) {\n"
"    this._timeoutMs = ms;\n"
"    if (cb) this.once('timeout', cb);\n"
"    return this;\n"
"  };\n"
"  ClientRequest.prototype.abort = function() {\n"
"    if (this.aborted) return;\n"
"    this.aborted = true;\n"
"    this.emit('abort');\n"
"    this.emit('close');\n"
"  };\n"
"  ClientRequest.prototype.destroy = function(err) {\n"
"    if (err) this.emit('error', err);\n"
"    this.abort();\n"
"    return this;\n"
"  };\n"
"\n"
"  ClientRequest.prototype._buildUrl = function() {\n"
"    var o = this._reqOpts;\n"
"    if (o._fullUrl) return o._fullUrl;\n"
"    var proto = o.protocol || this._defaultProtocol;\n"
"    if (proto && proto.charAt(proto.length - 1) !== ':') proto = proto + ':';\n"
"    var host = o.host || o.hostname || 'localhost';\n"
"    /* Strip any embedded port from host */\n"
"    var colon = host.lastIndexOf(':');\n"
"    if (colon >= 0 && host.charAt(0) !== '[') host = host.substring(0, colon);\n"
"    var port = o.port;\n"
"    var portStr = '';\n"
"    if (port != null && port !== '' && Number(port) !== 0) portStr = ':' + port;\n"
"    var auth = o.auth ? (encodeURIComponent(o.auth.split(':')[0])\n"
"               + (o.auth.indexOf(':') >= 0 ? ':' + encodeURIComponent(o.auth.substring(o.auth.indexOf(':')+1)) : '')\n"
"               + '@') : '';\n"
"    var path = o.path || '/';\n"
"    return proto + '//' + auth + host + portStr + path;\n"
"  };\n"
"\n"
"  ClientRequest.prototype._dispatchCurl = function(doneCallback) {\n"
"    if (this._sent) return;\n"
"    this._sent = true;\n"
"    var self = this;\n"
"    var url = self._buildUrl();\n"
"    var bodyBuf = self._body.length ? Buffer.concat(self._body) : null;\n"
"\n"
"    var copts = {};\n"
"    if (self.method !== 'GET') copts.request = self.method;\n"
"    /* HEAD must set NOBODY in libcurl, else curl waits for a body that\n"
"       never arrives (server sends Content-Length but no payload). */\n"
"    if (self.method === 'HEAD') copts.nobody = true;\n"
"\n"
"    /* Add Host header if user didn't supply one (node auto-adds it) */\n"
"    if (!('host' in self._headers) && (self._reqOpts.host || self._reqOpts.hostname)) {\n"
"      var hhost = self._reqOpts.host || self._reqOpts.hostname;\n"
"      var hp = self._reqOpts.port;\n"
"      var defaultPort = (self._defaultProtocol === 'https:') ? 443 : 80;\n"
"      if (hp && Number(hp) !== defaultPort) hhost = hhost + ':' + hp;\n"
"      self._headers['host'] = hhost;\n"
"      self._headerNames['host'] = 'Host';\n"
"    }\n"
"\n"
"    /* Build curl header array */\n"
"    var hArr = [];\n"
"    var sawCT = false;\n"
"    var hk = Object.keys(self._headers);\n"
"    for (var i = 0; i < hk.length; i++) {\n"
"      var lower = hk[i];\n"
"      var name = self._headerNames[lower] || lower;\n"
"      var v = self._headers[lower];\n"
"      if (lower === 'content-type') sawCT = true;\n"
"      if (Array.isArray(v)) {\n"
"        for (var j = 0; j < v.length; j++) hArr.push(name + ': ' + v[j]);\n"
"      } else {\n"
"        hArr.push(name + ': ' + v);\n"
"      }\n"
"    }\n"
"    /* If we have a body and no explicit Content-Type, suppress curl's\n"
"       libcurl-default 'application/x-www-form-urlencoded' for POST.\n"
"       (Matches the WHATWG fetch path's behavior.) */\n"
"    if (bodyBuf && !sawCT) hArr.push('Content-Type:');\n"
"    if (hArr.length) copts.header = hArr;\n"
"\n"
"    if (bodyBuf && bodyBuf.length > 0) copts.post = bodyBuf;\n"
"\n"
"    /* Timeout — convert ms → seconds for curl */\n"
"    if (self._timeoutMs > 0) copts.timeout = Math.max(1, Math.floor(self._timeoutMs / 1000));\n"
"\n"
"    /* Default: do NOT follow redirects (matches node http behavior).\n"
"       Set opts.followRedirects = true on the request to opt in. */\n"
"    copts.location = (self._reqOpts.followRedirects === true);\n"
"\n"
"    /* Build IncomingMessage */\n"
"    var res = new IncomingMessage();\n"
"    res.url = url;\n"
"    self._res = res;\n"
"\n"
"    var headersDelivered = false;\n"
"    var streamEnded = false;\n"
"    function _onHeaders(curlRes) {\n"
"      if (headersDelivered) return;\n"
"      headersDelivered = true;\n"
"      res.statusCode    = curlRes.status || 0;\n"
"      res.statusMessage = curlRes.statusText || STATUS_CODES[res.statusCode] || '';\n"
"      _normHeadersIntoIM(res, curlRes.headers);\n"
"      self.headersSent = true;\n"
"      self.emit('response', res);\n"
"    }\n"
"    function _endStream(err) {\n"
"      if (streamEnded) return;\n"
"      streamEnded = true;\n"
"      res.complete = !err;\n"
"      if (err) {\n"
"        try { res.destroy(err); } catch (_) {}\n"
"      } else {\n"
"        try { res.push(null); } catch (_) {}\n"
"      }\n"
"    }\n"
"\n"
"    copts.chunkCallback = function(curlRes) {\n"
"      if (self.aborted) return;\n"
"      if (!headersDelivered) _onHeaders(curlRes);\n"
"      if (curlRes.body) {\n"
"        var u8 = curlRes.body;\n"
"        if (!Buffer.isBuffer(u8)) {\n"
"          u8 = (u8 instanceof Uint8Array)\n"
"            ? Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength)\n"
"            : Buffer.from(u8);\n"
"        }\n"
"        try { res.push(u8); } catch (_) {}\n"
"      }\n"
"    };\n"
"    copts.callback = function(curlRes) {\n"
"      if (self._cancelTimer) { try { clearTimeout(self._cancelTimer); } catch(_){} self._cancelTimer = null; }\n"
"      if (self.aborted) {\n"
"        _endStream(new Error('aborted'));\n"
"        if (doneCallback) doneCallback();\n"
"        return;\n"
"      }\n"
"      if (!headersDelivered) {\n"
"        if (curlRes.errMsg && !curlRes.status) {\n"
"          var e = new Error(curlRes.errMsg);\n"
"          e.code = 'ECONNRESET';\n"
"          self.emit('error', e);\n"
"          if (doneCallback) doneCallback();\n"
"          return;\n"
"        }\n"
"        _onHeaders(curlRes);\n"
"      }\n"
"      _endStream(null);\n"
"      self.finished = true;\n"
"      self.emit('close');\n"
"      if (doneCallback) doneCallback();\n"
"    };\n"
"    /* curl chunkCallback path: skip the duplicated full-body buffer */\n"
"    copts.skipFinalRes = true;\n"
"\n"
"    /* Wire user-level timeout — emit 'timeout' if curl callback hasn't\n"
"       fired in time.  Does NOT abort by default (matches node). */\n"
"    if (self._timeoutMs > 0) {\n"
"      self._cancelTimer = setTimeout(function() {\n"
"        if (!streamEnded && !self.aborted) self.emit('timeout');\n"
"      }, self._timeoutMs);\n"
"    }\n"
"\n"
"    try {\n"
"      _getCurl().fetchAsync(url, copts);\n"
"    } catch (err) {\n"
"      if (self._cancelTimer) { try { clearTimeout(self._cancelTimer); } catch(_){} }\n"
"      self.emit('error', err);\n"
"      if (doneCallback) doneCallback();\n"
"    }\n"
"  };\n"
"\n"
"  /* ----- Agent (cosmetic — libcurl handles pooling) ----- */\n"
"  function Agent(opts) {\n"
"    if (!(this instanceof Agent)) return new Agent(opts);\n"
"    EventEmitter.call(this);\n"
"    opts = opts || {};\n"
"    this.keepAlive       = opts.keepAlive !== false;\n"
"    this.keepAliveMsecs  = opts.keepAliveMsecs || 1000;\n"
"    this.maxSockets      = opts.maxSockets || Infinity;\n"
"    this.maxFreeSockets  = opts.maxFreeSockets || 256;\n"
"    this.scheduling      = opts.scheduling || 'lifo';\n"
"    this.options         = opts;\n"
"    this.requests = {};\n"
"    this.sockets = {};\n"
"    this.freeSockets = {};\n"
"    this.protocol    = 'http:';\n"
"    this.defaultPort = 80;\n"
"  }\n"
"  Agent.prototype = Object.create(EventEmitter.prototype);\n"
"  Agent.prototype.constructor = Agent;\n"
"  Agent.prototype.destroy = function() {};\n"
"  Agent.prototype.getName = function(opts) {\n"
"    opts = opts || {};\n"
"    return (opts.host || 'localhost') + ':' + (opts.port || this.defaultPort)\n"
"         + ':' + (opts.localAddress || '') + (opts.family ? ':' + opts.family : '');\n"
"  };\n"
"  Agent.prototype.createConnection = function() { /* libcurl-managed */ };\n"
"\n"
"  function HttpsAgent(opts) {\n"
"    Agent.call(this, opts);\n"
"    this.protocol    = 'https:';\n"
"    this.defaultPort = 443;\n"
"  }\n"
"  HttpsAgent.prototype = Object.create(Agent.prototype);\n"
"  HttpsAgent.prototype.constructor = HttpsAgent;\n"
"\n"
"  /* ----- Options normalization ----- */\n"
"  function _parseUrlIntoOpts(u, defaultProtocol) {\n"
"    var proto = u.protocol || defaultProtocol;\n"
"    var pathname = u.pathname || '/';\n"
"    var search   = u.search   || '';\n"
"    return {\n"
"      protocol: proto,\n"
"      hostname: u.hostname || u.host || 'localhost',\n"
"      host:     u.host     || u.hostname || 'localhost',\n"
"      port:     u.port || undefined,\n"
"      path:     pathname + search,\n"
"      auth:     (u.username || u.password) ? ((u.username || '') + (u.password ? ':' + u.password : '')) : undefined\n"
"    };\n"
"  }\n"
"  function _normalize(arg1, arg2, arg3, defaultProtocol) {\n"
"    var opts, cb, urlStr;\n"
"    /* Accept (url, cb), (url, opts, cb), (url, opts), (opts, cb), (opts) */\n"
"    if (typeof arg1 === 'string'\n"
"        || (arg1 && typeof arg1 === 'object' && typeof arg1.href === 'string'\n"
"            && typeof arg1.protocol === 'string')) {\n"
"      var u;\n"
"      if (typeof arg1 === 'string') {\n"
"        urlStr = arg1;\n"
"        try { u = new URL(arg1); } catch (e) { throw new TypeError('Invalid URL: ' + arg1); }\n"
"      } else {\n"
"        u = arg1;\n"
"        urlStr = arg1.href;\n"
"      }\n"
"      if (typeof arg2 === 'function') { cb = arg2; opts = {}; }\n"
"      else { opts = arg2 || {}; cb = (typeof arg3 === 'function') ? arg3 : undefined; }\n"
"      var fromUrl = _parseUrlIntoOpts(u, defaultProtocol);\n"
"      opts = Object.assign({}, fromUrl, opts);\n"
"      opts._fullUrl = urlStr;\n"
"    } else {\n"
"      opts = arg1 || {};\n"
"      cb = (typeof arg2 === 'function') ? arg2 : undefined;\n"
"      opts = Object.assign({}, opts);\n"
"      if (!opts.protocol) opts.protocol = defaultProtocol;\n"
"    }\n"
"    return { opts: opts, cb: cb };\n"
"  }\n"
"\n"
"  /* ========================================================\n"
"   *  Server side (Phase A of nodeshim-todo.md §8.1)\n"
"   *\n"
"   *  http.createServer(listener) → Server.  Server.listen(port,...)\n"
"   *  defers an underlying rampart.server.start() via setImmediate;\n"
"   *  multiple sync createServer().listen() calls coalesce into one\n"
"   *  start({listen:[...]}) so HTTP+HTTPS or multiple ports work in\n"
"   *  the same process.  ServerResponse picks one of three modes at\n"
"   *  user-handler-return time:\n"
"   *    - buffered  → plain return object\n"
"   *    - chunked   → {chunk:true, chunkDelay:N, data:cb} polling\n"
"   *    - deferred  → {defer:true} + req.reply() later\n"
"   * ======================================================== */\n"
"  var _serverModule = null;  /* rampart-server, lazy */\n"
"  function _getServerMod() {\n"
"    if (!_serverModule) _serverModule = require('rampart-server');\n"
"    return _serverModule;\n"
"  }\n"
"  var _pendingServers = [];\n"
"  var _startScheduled = false;\n"
"  var _startFired = false;\n"
"  /* Module-local flag: when true, http.createServer() / .listen() are\n"
"     stubbed to capture the listener instead of actually starting a\n"
"     server.  Set transiently by _dispatchInWorker while re-running the\n"
"     user's script (process.argv[1]) inside the worker thread, so the\n"
"     worker ends up with a fully-constructed handler with worker-local\n"
"     closures intact.  See Phase A'' in nodeshim-todo.md §8.1. */\n"
"  var _inWorkerSetup = false;\n"
"  var _capturedListener = null;\n"
"  /* Per-worker cache: once setup runs, the captured listener is held\n"
"     here keyed by port and reused for every subsequent request to that\n"
"     port in this worker.  Each rampart-server worker has its own\n"
"     copy of this module (and this cache). */\n"
"  var _workerHandlerCache = {};\n"
"\n"
"  function _scheduleStart(srv) {\n"
"    _pendingServers.push(srv);\n"
"    if (_startScheduled) return;\n"
"    _startScheduled = true;\n"
"    /* Use setImmediate so all listens in the current tick coalesce */\n"
"    setImmediate(_actuallyStart);\n"
"  }\n"
"  function _actuallyStart() {\n"
"    if (_startFired) return;\n"
"    _startFired = true;\n"
"    var pending = _pendingServers;\n"
"    _pendingServers = [];\n"
"    if (!pending.length) return;\n"
"    /* Register each server's user listener(s) in a globalThis registry\n"
"       keyed by port.  rampart-server worker threads get a copy of\n"
"       globalThis at start time, so workers can look up the listener\n"
"       to invoke for each incoming request. */\n"
"    if (!globalThis.__rampart_http_handlers) globalThis.__rampart_http_handlers = {};\n"
"    if (!globalThis.__rampart_http_scopes)   globalThis.__rampart_http_scopes   = {};\n"
"    if (!globalThis.__rampart_http_sources)  globalThis.__rampart_http_sources  = {};\n"
"    if (!globalThis.__rampart_http_scripts)  globalThis.__rampart_http_scripts  = {};\n"
"    /* Detect the user's script path for auto-re-run-in-worker.  Unlike\n"
"       node, rampart leaves CLI flags (-t, -gc, etc.) in argv, so walk\n"
"       past leading dash-args to find the actual script.  Falls back\n"
"       to undefined for REPL / -gc inline / non-script invocation. */\n"
"    var scriptPath;\n"
"    if (process && process.argv) {\n"
"      for (var ai = 1; ai < process.argv.length; ai++) {\n"
"        var a = process.argv[ai];\n"
"        if (typeof a === 'string' && a.length > 0 && a.charAt(0) !== '-') {\n"
"          scriptPath = a; break;\n"
"        }\n"
"        /* -gc consumes the next arg as inline code; skip it */\n"
"        if (a === '-gc' || a === '-e') { ai++; continue; }\n"
"      }\n"
"    }\n"
"    var blocks = pending.map(function(srv) {\n"
"      var listeners = srv.listeners('request').slice();\n"
"      /* When auto-setup is on, the worker re-runs the script to build\n"
"         its own listener — don't stash the main-thread listener in\n"
"         globalThis, because rampart-server's cross-thread copy would\n"
"         eagerly invoke any property getters on it (e.g. Express's\n"
"         protocol getter at request.js:308 explodes during copy because\n"
"         `this.connection` is undefined at copy time). */\n"
"      var wantAutoStash = srv._autoSetup === true ||\n"
"        (srv._autoSetup !== false && globalThis.__rampart_http_auto_setup === true);\n"
"      if (!wantAutoStash) {\n"
"        globalThis.__rampart_http_handlers[srv._port] = listeners;\n"
"      }\n"
"      /* Capture-by-scope opt-in: if user passed {scope: getScopeVars().collapse()}\n"
"         to createServer, also stash the function sources so the worker can\n"
"         re-eval them in a scope that has the captured names localized. */\n"
"      if (srv._capturedScope) {\n"
"        globalThis.__rampart_http_scopes[srv._port] = srv._capturedScope;\n"
"        globalThis.__rampart_http_sources[srv._port] = listeners.map(function(fn){ return fn.toString(); });\n"
"      }\n"
"      /* Auto-re-run-in-worker: opt-in via opts.autoSetup:true, OR\n"
"         globalThis.__rampart_http_auto_setup = true.  Stashes the\n"
"         script path so the worker can re-execute it with a stubbed\n"
"         createServer that captures the constructed handler.  Default\n"
"         off — test scripts that run requests in the listen callback\n"
"         shouldn't re-run those requests inside the worker. */\n"
"      var wantAuto = srv._autoSetup === true ||\n"
"        (srv._autoSetup !== false && globalThis.__rampart_http_auto_setup === true);\n"
"      if (scriptPath && wantAuto) {\n"
"        globalThis.__rampart_http_scripts[srv._port] = scriptPath;\n"
"      }\n"
"      /* The map handler must be CLOSURE-FREE (cross-thread-copied) AND\n"
"         a named function (rampart-server rejects anonymous functions in\n"
"         the map).  eval a named-function-expression with the port baked\n"
"         into the body literal so no enclosing scope is needed. */\n"
"      var fnName = '_rampart_http_dispatch_' + srv._port;\n"
"      var handler = eval('(function ' + fnName + '(rreq) {\\n'\n"
"        + '  return require(\"http\")._dispatchInWorker(' + srv._port + ', rreq);\\n'\n"
"        + '})');\n"
"      var block = {\n"
"        bind: (srv._host || '0.0.0.0') + ':' + srv._port,\n"
"        map:  { '~.*': handler }\n"
"      };\n"
"      if (srv._secure) {\n"
"        block.secure = true;\n"
"        if (srv._sslKeyFile)  block.sslKeyFile  = srv._sslKeyFile;\n"
"        if (srv._sslCertFile) block.sslCertFile = srv._sslCertFile;\n"
"      }\n"
"      return block;\n"
"    });\n"
"    /* Pick threading config from the FIRST listening server (rampart-server\n"
"       start is process-wide).  Default useThreads:false matches the prior\n"
"       behavior — Express in-memory state stays coherent. */\n"
"    var primary = pending[0];\n"
"    var startOpts = { listen: blocks, useThreads: !!primary._useThreads };\n"
"    if (primary._threads) startOpts.threads = primary._threads;\n"
"    try {\n"
"      _getServerMod().start(startOpts);\n"
"    } catch (e) {\n"
"      pending.forEach(function(s) { s.emit('error', e); });\n"
"      return;\n"
"    }\n"
"    pending.forEach(function(s) {\n"
"      s._listening = true;\n"
"      setImmediate(function() { s.emit('listening'); });\n"
"    });\n"
"  }\n"
"\n"
"  /* Lazy header getter — first read of req.headers normalizes the\n"
"     raw map (lowercase keys, set-cookie aware), builds rawHeaders\n"
"     in lockstep, and caches both on the instance.  Subsequent reads\n"
"     hit the cache.  Cost is paid once, only if a handler asks. */\n"
"  function _imBuildHeaders() {\n"
"    if (this._headersCache) return this._headersCache;\n"
"    var out = {}, raw = [], src = this._rawSrc;\n"
"    if (src) {\n"
"      var keys = Object.keys(src);\n"
"      for (var i = 0; i < keys.length; i++) {\n"
"        var name = keys[i], val = src[name], lower = name.toLowerCase();\n"
"        if (Array.isArray(val)) {\n"
"          out[lower] = (lower === 'set-cookie') ? val.slice() : val.join(', ');\n"
"          for (var j = 0; j < val.length; j++) { raw.push(name); raw.push(String(val[j])); }\n"
"        } else {\n"
"          out[lower] = String(val);\n"
"          raw.push(name); raw.push(String(val));\n"
"        }\n"
"      }\n"
"    }\n"
"    this._headersCache    = out;\n"
"    this._rawHeadersCache = raw;\n"
"    return out;\n"
"  }\n"
"  function _imHeadersGet()    { return _imBuildHeaders.call(this); }\n"
"  function _imRawHeadersGet() { _imBuildHeaders.call(this); return this._rawHeadersCache; }\n"
"  function _imHeadersSet(v) {\n"
"    Object.defineProperty(this, 'headers',\n"
"      { value: v, writable: true, configurable: true, enumerable: true });\n"
"  }\n"
"  function _imRawHeadersSet(v) {\n"
"    Object.defineProperty(this, 'rawHeaders',\n"
"      { value: v, writable: true, configurable: true, enumerable: true });\n"
"  }\n"
"\n"
"  /* Lazy socket — plain data, upgrade to a real EventEmitter on first\n"
"     .on/.once.  ~99% of handlers read socket.remoteAddress or\n"
"     socket.encrypted and never wire listeners. */\n"
"  function _makeLazySocket(rreq) {\n"
"    var sock = {\n"
"      remoteAddress: rreq.ip || null,\n"
"      remotePort:    rreq.port || 0,\n"
"      localAddress:  null, localPort: 0,\n"
"      encrypted:     false, destroyed: false,\n"
"      writable:      true,  readable:  true,\n"
"      bytesRead:     0,     bytesWritten: 0,\n"
"      _ee:           false\n"
"    };\n"
"    sock.setTimeout    = function() { return this; };\n"
"    sock.setKeepAlive  = function() { return this; };\n"
"    sock.setNoDelay    = function() { return this; };\n"
"    sock.ref           = function() { return this; };\n"
"    sock.unref         = function() { return this; };\n"
"    function _up(self) {\n"
"      if (self._ee) return; self._ee = true; EventEmitter.call(self);\n"
"    }\n"
"    sock.on = sock.addListener = function(ev, fn) {\n"
"      _up(this); return EventEmitter.prototype.on.call(this, ev, fn);\n"
"    };\n"
"    sock.once = function(ev, fn) {\n"
"      _up(this); return EventEmitter.prototype.once.call(this, ev, fn);\n"
"    };\n"
"    sock.emit = function() {\n"
"      if (!this._ee) return false;\n"
"      return EventEmitter.prototype.emit.apply(this, arguments);\n"
"    };\n"
"    sock.removeListener = function(ev, fn) {\n"
"      if (!this._ee) return this;\n"
"      return EventEmitter.prototype.removeListener.call(this, ev, fn);\n"
"    };\n"
"    sock.destroy = function() {\n"
"      this.destroyed = true;\n"
"      if (this._ee) try { EventEmitter.prototype.emit.call(this, 'close'); } catch(_){}\n"
"    };\n"
"    return sock;\n"
"  }\n"
"\n"
"  /* Lazy IM-body attach.  rampart-server worker drains setImmediate but\n"
"     NOT Promise microtasks while a handler is in deferred mode — so a\n"
"     full WHATWG-stream-backed Readable would hang because reader.read()\n"
"     returns a Promise that never resolves.  Instead, set up a plain\n"
"     EventEmitter and schedule the body 'data'/'end' emit via\n"
"     setImmediate.  Covers the standard handler patterns\n"
"     (`req.on('data', c=>chunks.push(c)).on('end', ...)`) used by\n"
"     express, fastify, koa, and bare http.createServer code. */\n"
"  function _imAttachReadable(im) {\n"
"    if (im._readableAttached) return;\n"
"    im._readableAttached = true;\n"
"    delete im.on; delete im.addListener; delete im.once; delete im.emit;\n"
"    EventEmitter.call(im);\n"
"    var buf = im._pendingBody;\n"
"    im._pendingBody = null;\n"
"    /* setEncoding stub on the instance — if set before the body is\n"
"       emitted, decode the buffer to a string. */\n"
"    im.setEncoding = function (enc) { this._encoding = enc; return this; };\n"
"    im.pause       = function ()    { return this; };\n"
"    im.resume      = function ()    { return this; };\n"
"    im.readable    = true;\n"
"    setImmediate(function () {\n"
"      if (buf) {\n"
"        var v = buf;\n"
"        if (im._encoding) {\n"
"          try { v = buf.toString(im._encoding); } catch (_e) {}\n"
"        }\n"
"        try { im.emit('data', v); } catch (_) {}\n"
"      }\n"
"      try { im.emit('end'); } catch (_) {}\n"
"      im.readable = false;\n"
"      im.complete = true;\n"
"      try { im.emit('close'); } catch (_) {}\n"
"    });\n"
"  }\n"
"  function _imLazyOn(ev, fn) {\n"
"    if (ev === 'data' || ev === 'end' || ev === 'readable') {\n"
"      _imAttachReadable(this);\n"
"      return this.on(ev, fn);  /* now wired to Readable's wrapped .on */\n"
"    }\n"
"    if (!this._events) { this._events = {}; this._eventsCount = 0; }\n"
"    return EventEmitter.prototype.addListener.call(this, ev, fn);\n"
"  }\n"
"  function _imLazyOnce(ev, fn) {\n"
"    if (ev === 'data' || ev === 'end' || ev === 'readable') {\n"
"      _imAttachReadable(this);\n"
"      return this.once(ev, fn);\n"
"    }\n"
"    if (!this._events) { this._events = {}; this._eventsCount = 0; }\n"
"    return EventEmitter.prototype.once.call(this, ev, fn);\n"
"  }\n"
"  function _imLazyEmit() {\n"
"    if (!this._events) return false;\n"
"    return EventEmitter.prototype.emit.apply(this, arguments);\n"
"  }\n"
"\n"
"  function _buildIncomingMessage(rreq) {\n"
"    /* Skip the IncomingMessage/Readable/EventEmitter ctor chain.\n"
"       Stamp the prototype, set the handful of fields handlers read,\n"
"       and lazy everything else. */\n"
"    var im = Object.create(IncomingMessage.prototype);\n"
"    im.method = (rreq.method || 'GET').toUpperCase();\n"
"    var p = (rreq.path && rreq.path.path) ? rreq.path.path : '/';\n"
"    if (rreq.query_raw) p += '?' + rreq.query_raw;\n"
"    im.url      = p;\n"
"    im.complete = false;\n"
"    im.httpVersion      = '1.1';\n"
"    im.httpVersionMajor = 1;\n"
"    im.httpVersionMinor = 1;\n"
"    im.statusCode    = 0;\n"
"    im.statusMessage = '';\n"
"    im.trailers      = {};\n"
"    im.rawTrailers   = [];\n"
"    im.readable      = true;\n"
"    /* Lazy headers */\n"
"    im._rawSrc = rreq.headers || null;\n"
"    Object.defineProperty(im, 'headers',\n"
"      { get: _imHeadersGet,    set: _imHeadersSet,    configurable: true, enumerable: true });\n"
"    Object.defineProperty(im, 'rawHeaders',\n"
"      { get: _imRawHeadersGet, set: _imRawHeadersSet, configurable: true, enumerable: true });\n"
"    /* Stash body for lazy push */\n"
"    var body = rreq.body, buf = null;\n"
"    if (body != null) {\n"
"      if (Buffer.isBuffer(body)) buf = body;\n"
"      else if (body instanceof Uint8Array) buf = Buffer.from(body.buffer, body.byteOffset, body.byteLength);\n"
"      else if (typeof body === 'string') buf = Buffer.from(body, 'utf8');\n"
"      else buf = Buffer.from(String(body), 'utf8');\n"
"      if (buf.length === 0) buf = null;\n"
"    }\n"
"    im._pendingBody = buf;\n"
"    /* Lazy stream + EE on instance: triggers full Readable on data/end/readable */\n"
"    im.on = im.addListener = _imLazyOn;\n"
"    im.once = _imLazyOnce;\n"
"    im.emit = _imLazyEmit;\n"
"    /* Lazy socket */\n"
"    im.socket = im.connection = _makeLazySocket(rreq);\n"
"    return im;\n"
"  }\n"
"\n"
"  /* ----- ServerResponse (extends Writable shape; overrides write/end\n"
"     for the hot path) -----\n"
"     write/end are overridden on the prototype to bypass the Writable\n"
"     constructor + _write dispatch.  Most responses are sync res.end(body)\n"
"     with no prior writes — the fast path stashes body on _endBody and\n"
"     the dispatcher returns {bin: _endBody} directly.  Multi-write sync\n"
"     responses flush chunks via req.put to rampart-server's C buffer at\n"
"     dispatch time, avoiding a JS-side Buffer.concat. */\n"
"  function ServerResponse(rreq, server) {\n"
"    var self = this;\n"
"    self._rreq    = rreq;\n"
"    self._server  = server;\n"
"    self._chunks  = null; /* lazy-allocated only if res.write is called */\n"
"    self._endBody = null; /* body from res.end(body) when there were no prior writes */\n"
"    self._headers = {};\n"
"    self._headerNames = {};\n"
"    self.statusCode    = 200;\n"
"    self.statusMessage = '';\n"
"    self.headersSent   = false;\n"
"    self.sendDate      = true;\n"
"    self.writableEnded = false;\n"
"    self.finished      = false;\n"
"    self.socket = self.connection = _makeLazySocket(rreq);\n"
"    self._mode   = null;    /* 'buffered' | 'chunked' | 'deferred' — decided at handler return */\n"
"    self._didWrite = false;\n"
"    self._ended    = false;\n"
"    self._chunkCb  = null;\n"
"  }\n"
"  ServerResponse.prototype = Object.create(stream.Writable.prototype);\n"
"  ServerResponse.prototype.constructor = ServerResponse;\n"
"\n"
"  function _toBuf(chunk, enc) {\n"
"    if (Buffer.isBuffer(chunk)) return chunk;\n"
"    if (chunk instanceof Uint8Array)\n"
"      return Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength);\n"
"    return Buffer.from(String(chunk), enc || 'utf8');\n"
"  }\n"
"\n"
"  ServerResponse.prototype.write = function(chunk, encoding, cb) {\n"
"    if (typeof encoding === 'function') { cb = encoding; encoding = undefined; }\n"
"    if (chunk == null || chunk === '') { if (cb) cb(); return true; }\n"
"    var b = _toBuf(chunk, encoding);\n"
"    this._didWrite = true;\n"
"    if (!this._chunks) this._chunks = [];\n"
"    this._chunks.push(b);\n"
"    if (cb) cb();\n"
"    return true;\n"
"  };\n"
"\n"
"  ServerResponse.prototype.end = function(chunk, encoding, cb) {\n"
"    if (typeof chunk === 'function')   { cb = chunk;    chunk = undefined; }\n"
"    else if (typeof encoding === 'function') { cb = encoding; encoding = undefined; }\n"
"    if (this._ended) { if (cb) cb(); return this; }\n"
"    if (chunk != null && chunk !== '') {\n"
"      var b = _toBuf(chunk, encoding);\n"
"      /* If prior writes accumulated, append as another chunk. Otherwise\n"
"         stash as the single fast-path body. */\n"
"      if (this._chunks && this._chunks.length) this._chunks.push(b);\n"
"      else this._endBody = b;\n"
"    }\n"
"    this._ended = true;\n"
"    this.writableEnded = true;\n"
"    this.finished = true;\n"
"    if (this._mode === 'deferred') this._dispatchDeferred();\n"
"    if (cb) cb();\n"
"    return this;\n"
"  };\n"
"\n"
"  /* No-op stubs for Writable APIs that some middleware probes. */\n"
"  ServerResponse.prototype.cork   = function() {};\n"
"  ServerResponse.prototype.uncork = function() {};\n"
"  ServerResponse.prototype.setDefaultEncoding = function() { return this; };\n"
"  /* assignSocket / detachSocket — Node-internal hooks used by test\n"
"     harnesses like light-my-request (fastify's inject()) that fake\n"
"     out a socket without binding a real port.  Capture the\n"
"     reference so .socket is populated; no real I/O happens. */\n"
"  ServerResponse.prototype.assignSocket = function(socket) {\n"
"    this.socket = socket;\n"
"    this.connection = socket;\n"
"    if (socket) this.emit('socket', socket);\n"
"  };\n"
"  ServerResponse.prototype.detachSocket = function(socket) {\n"
"    this.socket = null;\n"
"    this.connection = null;\n"
"  };\n"
"  ServerResponse.prototype.destroy = function(err) {\n"
"    this.destroyed = true;\n"
"    if (this.socket && this.socket.destroy) this.socket.destroy();\n"
"    return this;\n"
"  };\n"
"\n"
"  /* Fast-path lookup for header names Express sets on nearly every\n"
"     response.  Skips String() coercion and toLowerCase() — Duktape's\n"
"     toLowerCase walks every codepoint and is a measurable per-request\n"
"     cost when hit 2-3x. */\n"
"  function _lowerHdr(name) {\n"
"    if (typeof name === 'string') {\n"
"      switch (name) {\n"
"        case 'Content-Type':   return 'content-type';\n"
"        case 'Content-Length': return 'content-length';\n"
"        case 'Date':           return 'date';\n"
"        case 'ETag':           return 'etag';\n"
"        case 'X-Powered-By':   return 'x-powered-by';\n"
"        case 'Cache-Control':  return 'cache-control';\n"
"        case 'Connection':     return 'connection';\n"
"        case 'Set-Cookie':     return 'set-cookie';\n"
"        case 'content-type':   return 'content-type';\n"
"        case 'content-length': return 'content-length';\n"
"      }\n"
"      return name.toLowerCase();\n"
"    }\n"
"    return String(name).toLowerCase();\n"
"  }\n"
"\n"
"  ServerResponse.prototype.setHeader = function(name, value) {\n"
"    if (this.headersSent) throw new Error('Cannot set headers after they are sent.');\n"
"    var lower = _lowerHdr(name);\n"
"    this._headers[lower] = value;\n"
"    this._headerNames[lower] = (typeof name === 'string') ? name : String(name);\n"
"    return this;\n"
"  };\n"
"  ServerResponse.prototype.getHeader = function(name) {\n"
"    return this._headers[_lowerHdr(name)];\n"
"  };\n"
"  ServerResponse.prototype.getHeaderNames = function() { return Object.keys(this._headers); };\n"
"  ServerResponse.prototype.getRawHeaderNames = function() {\n"
"    var keys = Object.keys(this._headers);\n"
"    var out = []; for (var i = 0; i < keys.length; i++) out.push(this._headerNames[keys[i]] || keys[i]);\n"
"    return out;\n"
"  };\n"
"  ServerResponse.prototype.getHeaders = function() {\n"
"    var out = {}; var keys = Object.keys(this._headers);\n"
"    for (var i = 0; i < keys.length; i++) out[keys[i]] = this._headers[keys[i]];\n"
"    return out;\n"
"  };\n"
"  ServerResponse.prototype.hasHeader = function(name) {\n"
"    return _lowerHdr(name) in this._headers;\n"
"  };\n"
"  ServerResponse.prototype.removeHeader = function(name) {\n"
"    if (this.headersSent) throw new Error('Cannot remove headers after they are sent.');\n"
"    var lower = _lowerHdr(name);\n"
"    delete this._headers[lower];\n"
"    delete this._headerNames[lower];\n"
"  };\n"
"  ServerResponse.prototype.flushHeaders = function() { /* no-op: rampart-server sends with body */ };\n"
"  ServerResponse.prototype.writeHead = function(status, statusMessage, headers) {\n"
"    if (this.headersSent) throw new Error('Cannot writeHead after headers sent.');\n"
"    this.statusCode = status | 0;\n"
"    if (typeof statusMessage === 'string') this.statusMessage = statusMessage;\n"
"    else if (statusMessage && typeof statusMessage === 'object' && !Array.isArray(statusMessage)) {\n"
"      headers = statusMessage; statusMessage = undefined;\n"
"    }\n"
"    if (headers) {\n"
"      if (Array.isArray(headers)) {\n"
"        for (var i = 0; i + 1 < headers.length; i += 2) this.setHeader(headers[i], headers[i+1]);\n"
"      } else {\n"
"        var keys = Object.keys(headers);\n"
"        for (var k = 0; k < keys.length; k++) this.setHeader(keys[k], headers[keys[k]]);\n"
"      }\n"
"    }\n"
"    return this;\n"
"  };\n"
"\n"
"  /* Per-worker Date header cache — invalidated every second.  Node's\n"
"     own http server does the same: Date string formatting is comparable\n"
"     in cost to the rest of the response build, but only changes once\n"
"     per second.  At 1000+ rps the cache hit rate is essentially 100%. */\n"
"  var _cachedDateStr = '';\n"
"  var _cachedDateSec = 0;\n"
"  function _getDateHeader() {\n"
"    var now = (Date.now() / 1000) | 0;\n"
"    if (now !== _cachedDateSec) {\n"
"      _cachedDateSec = now;\n"
"      _cachedDateStr = (new Date()).toUTCString();\n"
"    }\n"
"    return _cachedDateStr;\n"
"  }\n"
"\n"
"  ServerResponse.prototype._buildHeaders = function() {\n"
"    var hdrsObj = {};\n"
"    var hdrs = this._headers, names = this._headerNames;\n"
"    var keys = Object.keys(hdrs);\n"
"    for (var i = 0; i < keys.length; i++) {\n"
"      var k = keys[i];\n"
"      var orig = names[k] || k;\n"
"      var v = hdrs[k];\n"
"      hdrsObj[orig] = Array.isArray(v) ? v.join(', ') : String(v);\n"
"    }\n"
"    if (this.sendDate && !('date' in hdrs)) {\n"
"      hdrsObj['Date'] = _getDateHeader();\n"
"    }\n"
"    return hdrsObj;\n"
"  };\n"
"\n"
"  /* Used by _dispatchDeferred (req.reply path) — must build a single\n"
"     Buffer because deferred dispatch lacks the buffered req.put state. */\n"
"  ServerResponse.prototype._buildRespObj = function() {\n"
"    if (this._sendFilePath) {\n"
"      /* rampart-server-native file send via @prefix.  Same dedup as\n"
"         _buildAndFlush: rampart-server unconditionally appends\n"
"         Content-Length, Date, and Accept-Ranges for @-file responses,\n"
"         so we strip our copies to avoid duplicates that break strict\n"
"         HTTP parsers (wrk).  Last-Modified gets stripped too — Express's\n"
"         duktape-toUTCString output isn't standard HTTP date format. */\n"
"      var hdrs = {};\n"
"      var src = this._headers, names = this._headerNames;\n"
"      var keys = Object.keys(src);\n"
"      for (var i = 0; i < keys.length; i++) {\n"
"        var k = keys[i];\n"
"        if (k === 'content-length' || k === 'last-modified'\n"
"         || k === 'date' || k === 'accept-ranges') continue;\n"
"        var orig = names[k] || k;\n"
"        var v = src[k];\n"
"        hdrs[orig] = Array.isArray(v) ? v.join(', ') : String(v);\n"
"      }\n"
"      var out = { bin: '@' + this._sendFilePath,\n"
"                  status: this.statusCode,\n"
"                  noAcceptRanges: ('accept-ranges' in src) };\n"
"      if (Object.keys(hdrs).length) out.headers = hdrs;\n"
"      return out;\n"
"    }\n"
"    var parts = [];\n"
"    if (this._chunks && this._chunks.length) {\n"
"      for (var i = 0; i < this._chunks.length; i++) parts.push(this._chunks[i]);\n"
"    }\n"
"    if (this._endBody) parts.push(this._endBody);\n"
"    var body = parts.length ? Buffer.concat(parts) : Buffer.alloc(0);\n"
"    var hdrsObj = this._buildHeaders();\n"
"    var out = { bin: body, status: this.statusCode };\n"
"    if (Object.keys(hdrsObj).length) out.headers = hdrsObj;\n"
"    return out;\n"
"  };\n"
"\n"
"  /* Sync-buffered fast path used by _dispatchInWorker.  For multi-write\n"
"     responses, flushes accumulated chunks to rampart-server's C buffer\n"
"     via req.put — avoids a JS-side Buffer.concat across the array.  For\n"
"     single-shot res.end(body), _chunks is empty/null and we return the\n"
"     stashed _endBody directly.  In both cases rampart-server prepends\n"
"     any buffer accumulated via req.put to our returned {bin: ...}. */\n"
"  ServerResponse.prototype._buildAndFlush = function() {\n"
"    if (this._sendFilePath) {\n"
"      /* rampart-server-native file send via @prefix.  rampart-server\n"
"         unconditionally appends Content-Length, Last-Modified, Date,\n"
"         and Accept-Ranges for @-prefix file responses — even if our\n"
"         headers object already supplies them.  Strict HTTP parsers\n"
"         (wrk, some intermediaries) reject duplicate Content-Length /\n"
"         Date.  Filter our copies out and let rampart-server be the\n"
"         single source for those.  We also set noAcceptRanges so it\n"
"         doesn't re-add that header. */\n"
"      var hdrs0 = {};\n"
"      var src = this._headers, names = this._headerNames;\n"
"      var keys = Object.keys(src);\n"
"      for (var i = 0; i < keys.length; i++) {\n"
"        var k = keys[i];\n"
"        if (k === 'content-length' || k === 'last-modified'\n"
"         || k === 'date' || k === 'accept-ranges') continue;\n"
"        var orig = names[k] || k;\n"
"        var v = src[k];\n"
"        hdrs0[orig] = Array.isArray(v) ? v.join(', ') : String(v);\n"
"      }\n"
"      var out0 = { bin: '@' + this._sendFilePath,\n"
"                   status: this.statusCode,\n"
"                   noAcceptRanges: ('accept-ranges' in src) };\n"
"      if (Object.keys(hdrs0).length) out0.headers = hdrs0;\n"
"      return out0;\n"
"    }\n"
"    var ch = this._chunks;\n"
"    if (ch) {\n"
"      for (var i = 0; i < ch.length; i++) {\n"
"        try { this._rreq.put(ch[i]); } catch(_) {}\n"
"      }\n"
"      ch.length = 0;\n"
"    }\n"
"    var hdrsObj = this._buildHeaders();\n"
"    var out = { bin: this._endBody || Buffer.alloc(0), status: this.statusCode };\n"
"    if (Object.keys(hdrsObj).length) out.headers = hdrsObj;\n"
"    return out;\n"
"  };\n"
"\n"
"  ServerResponse.prototype._dispatchDeferred = function() {\n"
"    if (this._deferredFired) return;\n"
"    this._deferredFired = true;\n"
"    this.headersSent = true;\n"
"    try { this._rreq.reply(this._buildRespObj()); }\n"
"    catch (e) { /* connection probably gone — nothing we can do */ }\n"
"    if (this._events) { this.emit('finish'); this.emit('close'); }\n"
"  };\n"
"\n"
"  /* ----- Server (extends EventEmitter) ----- */\n"
"  function Server(opts) {\n"
"    if (!(this instanceof Server)) return new Server(opts);\n"
"    EventEmitter.call(this);\n"
"    opts = opts || {};\n"
"    this._listening = false;\n"
"    this._port = null;\n"
"    this._host = null;\n"
"    this._secure = !!opts.secure;\n"
"    this._sslKeyFile  = opts.key  || opts.sslKeyFile  || null;\n"
"    this._sslCertFile = opts.cert || opts.sslCertFile || null;\n"
"    /* rampart-server's process-wide thread pool config.  Default is\n"
"       single-threaded — matches the in-process state model that vanilla\n"
"       Express apps assume (in-memory counters, multer memoryStorage,\n"
"       app.locals, etc. all fragment per-worker under useThreads:true).\n"
"       Opt in via createServer(handler, {useThreads:true, threads:N}).\n"
"       Note: only the first listening server in the process gets to set\n"
"       these — rampart-server.start() is a process-wide singleton. */\n"
"    this._useThreads = (opts.useThreads === true);\n"
"    this._threads    = (typeof opts.threads === 'number' && opts.threads > 0)\n"
"                         ? (opts.threads | 0) : null;\n"
"    this.timeout = 120000;\n"
"    this.keepAliveTimeout = 5000;\n"
"    this.headersTimeout = 60000;\n"
"    this.requestTimeout = 0;\n"
"    this.maxHeadersCount = null;\n"
"    this.maxConnections = null;\n"
"  }\n"
"  Server.prototype = Object.create(EventEmitter.prototype);\n"
"  Server.prototype.constructor = Server;\n"
"\n"
"  Server.prototype.listen = function() {\n"
"    var args = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof args[args.length - 1] === 'function') ? args.pop() : null;\n"
"    var port, host;\n"
"    if (typeof args[0] === 'object' && args[0] !== null) {\n"
"      port = args[0].port;\n"
"      host = args[0].host || '0.0.0.0';\n"
"    } else {\n"
"      port = args[0];\n"
"      host = (typeof args[1] === 'string') ? args[1] : '0.0.0.0';\n"
"    }\n"
"    if (port === undefined || port === null) port = 0;\n"
"    /* Auto-re-run pattern: if we're running inside a worker (re-running\n"
"       the user's script to capture the handler), just stash the\n"
"       listener and return without starting anything.  The worker's\n"
"       _dispatchInWorker did this transient setup; control returns to\n"
"       it after the user's script finishes. */\n"
"    if (_inWorkerSetup) {\n"
"      _capturedListener = this.listeners('request')[0] || null;\n"
"      this._listening = true;\n"
"      if (cb) setImmediate(cb);\n"
"      return this;\n"
"    }\n"
"    /* Port 0 = OS-assigned ephemeral — rampart-server doesn't support\n"
"       this directly; pick one in 49152-65535 (IANA dynamic range). */\n"
"    if (Number(port) === 0) {\n"
"      port = 49152 + Math.floor(Math.random() * (65535 - 49152));\n"
"    }\n"
"    this._port = Number(port);\n"
"    this._host = host;\n"
"    if (cb) this.once('listening', cb);\n"
"    if (_startFired) {\n"
"      var e = new Error('listen EADDRINUSE: rampart.server.start already fired this process');\n"
"      e.code = 'EADDRINUSE';\n"
"      var self = this;\n"
"      setImmediate(function() { self.emit('error', e); });\n"
"      return this;\n"
"    }\n"
"    _scheduleStart(this);\n"
"    return this;\n"
"  };\n"
"\n"
"  Server.prototype.address = function() {\n"
"    if (!this._listening) return null;\n"
"    return { port: this._port, address: this._host, family: 'IPv4' };\n"
"  };\n"
"  Server.prototype.close = function(cb) {\n"
"    /* rampart-server doesn't support per-listener shutdown without\n"
"       tearing down the whole process.  Mark as closed for code that\n"
"       checks; the actual listener stays up until process exit. */\n"
"    this._listening = false;\n"
"    if (cb) setImmediate(cb);\n"
"    setImmediate(this.emit.bind(this, 'close'));\n"
"    return this;\n"
"  };\n"
"  Server.prototype.setTimeout = function(ms, cb) {\n"
"    this.timeout = ms;\n"
"    if (cb) this.on('timeout', cb);\n"
"    return this;\n"
"  };\n"
"  Server.prototype.ref   = function() { return this; };\n"
"  Server.prototype.unref = function() { return this; };\n"
"  Server.prototype.getConnections = function(cb) { setImmediate(function() { cb(null, 0); }); };\n"
"\n"
"  /* _dispatchInWorker — runs INSIDE the rampart-server worker thread.\n"
"     Looks up the registered user-listener(s) for this port (copied\n"
"     into the worker's globalThis at server.start time), builds the\n"
"     IM + ServerResponse, invokes the listener, and returns the\n"
"     appropriate rampart-server response descriptor based on what the\n"
"     handler did synchronously.\n"
"     Exposed on the http module as `_dispatchInWorker` so the inline\n"
"     map handler can find it via `require('http')`. */\n"
"  function _dispatchInWorker(port, rreq) {\n"
"    var listeners;\n"
"    if (_workerHandlerCache[port]) {\n"
"      listeners = [_workerHandlerCache[port]];\n"
"    } else {\n"
"      var scripts = globalThis.__rampart_http_scripts;\n"
"      if (scripts && scripts[port]) {\n"
"        var path = scripts[port];\n"
"        _inWorkerSetup = true;\n"
"        _capturedListener = null;\n"
"        try {\n"
"          require(path);\n"
"        } catch (setupErr) {\n"
"          _inWorkerSetup = false;\n"
"          return { status: 500,\n"
"                   text: 'auto-setup failed: ' + (setupErr && setupErr.message || setupErr),\n"
"                   headers: { 'Content-Type': 'text/plain' } };\n"
"        }\n"
"        _inWorkerSetup = false;\n"
"        if (_capturedListener) {\n"
"          _workerHandlerCache[port] = _capturedListener;\n"
"          listeners = [_capturedListener];\n"
"        }\n"
"      }\n"
"    }\n"
"    /* Fallback: registry from main thread (closure-free path). */\n"
"    if (!listeners) {\n"
"      var reg = globalThis.__rampart_http_handlers;\n"
"      listeners = (reg && reg[port]) ? reg[port] : null;\n"
"    }\n"
"    if (!listeners || !listeners.length) {\n"
"      return { status: 500, text: 'no http listener registered for port ' + port,\n"
"               headers: { 'Content-Type': 'text/plain' } };\n"
"    }\n"
"    /* Capture-by-scope: if user opted in by passing {scope: getScopeVars()\n"
"       .collapse()} to createServer, localize the captured names here so\n"
"       any handler that references them via closure can resolve them.\n"
"       NOTE: duktape's cross-thread copy preserves function closures, so\n"
"       no re-eval of source is needed (and isn't even possible — duktape's\n"
"       Function.prototype.toString returns a [ecmascript code] stub). */\n"
"    var scopes  = globalThis.__rampart_http_scopes;\n"
"    if (scopes && scopes[port] && !_workerHandlerCache[port]) {\n"
"      try { rampart.localize(scopes[port], true); } catch(_){}\n"
"    }\n"
"    /* Native IM/SR builders are attached to the http module exports by\n"
"       nodeshim_install_native_builders after the outer IIFE returns.\n"
"       Look them up lazily on first dispatch and cache on the dispatch\n"
"       function itself so subsequent requests skip the require lookup. */\n"
"    if (!_dispatchInWorker._natIM) {\n"
"      var _hm = require('http');\n"
"      _dispatchInWorker._natIM = _hm._buildIMNative || _buildIncomingMessage;\n"
"      _dispatchInWorker._natSR = _hm._buildSRNative || function(r){ return new ServerResponse(r); };\n"
"    }\n"
"    var im  = _dispatchInWorker._natIM(rreq);\n"
"    var res = _dispatchInWorker._natSR(rreq);\n"
"\n"
"    try {\n"
"      for (var i = 0; i < listeners.length; i++) listeners[i](im, res);\n"
"    } catch (e) {\n"
"      if (!res.headersSent && !res._didWrite && !res._ended) {\n"
"        return { status: 500, text: 'Internal Server Error: ' + (e && e.message || e),\n"
"                 headers: { 'Content-Type': 'text/plain' } };\n"
"      }\n"
"      /* Already partly-sent — best-effort error log; can't fix the wire. */\n"
"    }\n"
"\n"
"    if (res._ended) {\n"
"      res._mode = 'buffered';\n"
"      res.headersSent = true;\n"
"      /* Fast path: flush any res.write chunks via req.put, return\n"
"         {bin: _endBody}.  Avoids the JS-side Buffer.concat in the\n"
"         common single-write case and for multi-write sync-end. */\n"
"      var out = res._buildAndFlush();\n"
"      if (res._events) setImmediate(function() { res.emit('finish'); res.emit('close'); });\n"
"      return out;\n"
"    }\n"
"    if (res._didWrite) {\n"
"      res._mode = 'chunked';\n"
"      res.headersSent = true;\n"
"      var hdrsCopy = res._buildHeaders();\n"
"      return {\n"
"        chunk: true,\n"
"        chunkDelay: 10,\n"
"        status: res.statusCode,\n"
"        headers: hdrsCopy,\n"
"        data: function(rreq2) {\n"
"          var ch = res._chunks;\n"
"          if (ch) while (ch.length) {\n"
"            try { rreq2.chunkSend(ch.shift()); }\n"
"            catch (_) { return; }\n"
"          }\n"
"          if (res._ended) {\n"
"            /* If end() was called with a body and there were prior\n"
"               writes, that body was appended to _chunks already.  If\n"
"               it was called with a body and no prior writes, end\n"
"               stashed it as _endBody — send it as the final chunk. */\n"
"            try {\n"
"              if (res._endBody) { rreq2.chunkEnd(res._endBody); res._endBody = null; }\n"
"              else rreq2.chunkEnd();\n"
"            } catch (_) {}\n"
"            if (res._events) setImmediate(function() { res.emit('finish'); res.emit('close'); });\n"
"          }\n"
"        }\n"
"      };\n"
"    }\n"
"    /* Defer — handler will call res.end() asynchronously. */\n"
"    res._mode = 'deferred';\n"
"    return { defer: true };\n"
"  }\n"
"\n"
"  function _makeModule(defaultProtocol) {\n"
"    var AgentCtor = (defaultProtocol === 'https:') ? HttpsAgent : Agent;\n"
"    var globalAgent = new AgentCtor();\n"
"    function request(a, b, c) {\n"
"      var n = _normalize(a, b, c, defaultProtocol);\n"
"      return new ClientRequest(n.opts, n.cb);\n"
"    }\n"
"    function get(a, b, c) {\n"
"      var req = request(a, b, c);\n"
"      req.end();\n"
"      return req;\n"
"    }\n"
"    function createServer(opts, listener) {\n"
"      /* Accept both (opts, listener) [node canonical] and (listener,\n"
"         opts) [common convenience] arg orders. */\n"
"      if (typeof opts === 'function' && typeof listener === 'object' && listener) {\n"
"        var t = listener; listener = opts; opts = t;\n"
"      } else if (typeof opts === 'function') {\n"
"        listener = opts; opts = {};\n"
"      }\n"
"      opts = opts || {};\n"
"      if (defaultProtocol === 'https:') opts.secure = true;\n"
"      var srv = new Server(opts);\n"
"      /* Opt-in capture-by-scope: user passes their own getScopeVars()\n"
"         .collapse() result so the worker can restore closures over the\n"
"         main-thread locals.  See _dispatchInWorker. */\n"
"      if (opts.scope) srv._capturedScope = opts.scope;\n"
"      /* Auto-re-run-in-worker for unmodified Express/Fastify/etc.\n"
"         Opt-in via opts.autoSetup:true, or globalThis-wide via\n"
"         globalThis.__rampart_http_auto_setup = true.  Default off so\n"
"         test scripts that drive their own server from the main\n"
"         thread don't accidentally re-fire requests in the worker. */\n"
"      if (opts.autoSetup === true) srv._autoSetup = true;\n"
"      else if (opts.autoSetup === false) srv._autoSetup = false;\n"
"      if (listener) srv.on('request', listener);\n"
"      return srv;\n"
"    }\n"
"    return {\n"
"      request:        request,\n"
"      get:            get,\n"
"      Agent:          AgentCtor,\n"
"      globalAgent:    globalAgent,\n"
"      METHODS:        METHODS.slice(),\n"
"      STATUS_CODES:   STATUS_CODES,\n"
"      IncomingMessage: IncomingMessage,\n"
"      ClientRequest:  ClientRequest,\n"
"      ServerResponse: ServerResponse,\n"
"      Server:         Server,\n"
"      createServer:   createServer,\n"
"      maxHeaderSize:  16384,\n"
"      /* INTERNAL — called by closure-free map handler in worker threads.\n"
"         Not part of node's surface; do not depend on this externally. */\n"
"      _dispatchInWorker: _dispatchInWorker\n"
"    };\n"
"  }\n"
"\n"
"  /* Body conversion helper used by native IM builder.  Splitting out\n"
"     of _buildIncomingMessage so the C side can call into one entry\n"
"     point rather than replicating the Buffer.isBuffer/Uint8Array/string\n"
"     coercion logic. */\n"
"  function _imBuildBody(body) {\n"
"    if (body == null) return null;\n"
"    var buf;\n"
"    if (Buffer.isBuffer(body)) buf = body;\n"
"    else if (body instanceof Uint8Array) buf = Buffer.from(body.buffer, body.byteOffset, body.byteLength);\n"
"    else if (typeof body === 'string') buf = Buffer.from(body, 'utf8');\n"
"    else buf = Buffer.from(String(body), 'utf8');\n"
"    return (buf.length === 0) ? null : buf;\n"
"  }\n"
"\n"
"  /* Internal slot — captured at module init by the native C builder.\n"
"     Not part of node's surface; the slot is removed from the exports\n"
"     after init so user code never sees it. */\n"
"  var _natives = {\n"
"    IM_proto:        IncomingMessage.prototype,\n"
"    SR_proto:        ServerResponse.prototype,\n"
"    imHeadersGet:    _imHeadersGet,\n"
"    imHeadersSet:    _imHeadersSet,\n"
"    imRawHeadersGet: _imRawHeadersGet,\n"
"    imRawHeadersSet: _imRawHeadersSet,\n"
"    imLazyOn:        _imLazyOn,\n"
"    imLazyOnce:      _imLazyOnce,\n"
"    imLazyEmit:      _imLazyEmit,\n"
"    makeLazySocket:  _makeLazySocket,\n"
"    imBuildBody:     _imBuildBody\n"
"  };\n"
"\n"
"  return { http: _makeModule('http:'), https: _makeModule('https:'), _natives: _natives };\n"
"}";
/* ============================================================
 * Native IM / SR construction.
 *
 * Per-request hot path: every request had to run a JS IIFE that did
 * Object.create + ~13 property writes + 2× Object.defineProperty for
 * the lazy headers getters + lazy method assignment.  The defineProperty
 * calls alone are very expensive on Duktape (they go through the slow
 * property-descriptor path).  These native builders do the same work
 * with duk_def_prop + duk_put_prop_string, skipping the JS interpreter
 * overhead entirely.  The lazy headers getter/setter functions, the
 * lazy on/once/emit functions, the lazy socket factory, and the body
 * coercion helper remain JS (captured at init time and cached on the
 * native C function as hidden properties).
 * ============================================================ */

/* Pull a heap pointer from a hidden property on the current C function. */
static void *_nshim_grab_heapptr(duk_context *ctx, const char *hidden_key)
{
    void *p;
    duk_get_prop_string(ctx, -1, hidden_key);
    p = duk_get_heapptr(ctx, -1);
    duk_pop(ctx);
    return p;
}

/* _buildIMNative(rreq) — replacement for inline JS _buildIncomingMessage. */
static duk_ret_t nodeshim_http_build_im(duk_context *ctx)
{
    /* Stack on entry: [rreq] */
    void *im_proto, *hdrs_get, *hdrs_set, *raw_hdrs_get, *raw_hdrs_set;
    void *lazy_on, *lazy_once, *lazy_emit, *make_socket, *build_body;

    duk_push_current_function(ctx);  /* [rreq, fn] */
    im_proto     = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("im_proto"));
    hdrs_get     = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("hdrs_get"));
    hdrs_set     = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("hdrs_set"));
    raw_hdrs_get = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("raw_hdrs_get"));
    raw_hdrs_set = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("raw_hdrs_set"));
    lazy_on      = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("lazy_on"));
    lazy_once    = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("lazy_once"));
    lazy_emit    = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("lazy_emit"));
    make_socket  = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("make_socket"));
    build_body   = _nshim_grab_heapptr(ctx, DUK_HIDDEN_SYMBOL("build_body"));
    duk_pop(ctx);  /* current function */

    /* Stack: [rreq] */
    duk_idx_t im_idx = duk_push_object(ctx);             /* [rreq, im] */
    duk_push_heapptr(ctx, im_proto);                     /* [rreq, im, proto] */
    duk_set_prototype(ctx, im_idx);                      /* [rreq, im] */

    /* im.method = (rreq.method || 'GET').toUpperCase() */
    duk_get_prop_string(ctx, 0, "method");
    if (duk_is_string(ctx, -1)) {
        duk_size_t mlen;
        const char *m = duk_get_lstring(ctx, -1, &mlen);
        int needs_upper = 0;
        duk_size_t i;
        for (i = 0; i < mlen; i++) {
            char c = m[i];
            if (c >= 'a' && c <= 'z') { needs_upper = 1; break; }
        }
        if (needs_upper) {
            duk_push_string(ctx, "toUpperCase");
            duk_call_prop(ctx, -2, 0);                   /* [rreq, im, upper] */
            duk_remove(ctx, -2);                         /* [rreq, im, upper] */
        }
        /* else already uppercase, leave as-is */
    } else {
        duk_pop(ctx);
        duk_push_string(ctx, "GET");
    }
    duk_put_prop_string(ctx, im_idx, "method");          /* [rreq, im] */

    /* im.url = (rreq.path && rreq.path.path) || '/' + (rreq.query_raw ? '?' + rreq.query_raw : '') */
    {
        const char *path_str = "/";
        const char *query_str = NULL;
        duk_idx_t path_top = -1;
        duk_get_prop_string(ctx, 0, "path");             /* [rreq, im, path_obj] */
        if (duk_is_object(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "path");        /* [rreq, im, path_obj, path_str] */
            if (duk_is_string(ctx, -1)) path_str = duk_get_string(ctx, -1);
            path_top = duk_get_top(ctx) - 1;
        }
        duk_get_prop_string(ctx, 0, "query_raw");        /* [..., query_raw] */
        if (duk_is_string(ctx, -1)) query_str = duk_get_string(ctx, -1);
        if (query_str && *query_str) {
            duk_push_sprintf(ctx, "%s?%s", path_str, query_str);
        } else {
            duk_push_string(ctx, path_str);
        }
        /* Put url onto im, then pop scratch. */
        duk_put_prop_string(ctx, im_idx, "url");         /* [rreq, im, path_obj?, path_str?, query_raw] */
        duk_pop(ctx);  /* query_raw */
        if (path_top >= 0) duk_pop(ctx);  /* path_str */
        duk_pop(ctx);  /* path_obj */
    }

    /* Cheap scalar fields. */
    duk_push_false(ctx);    duk_put_prop_string(ctx, im_idx, "complete");
    duk_push_string(ctx, "1.1"); duk_put_prop_string(ctx, im_idx, "httpVersion");
    duk_push_int(ctx, 1);   duk_put_prop_string(ctx, im_idx, "httpVersionMajor");
    duk_push_int(ctx, 1);   duk_put_prop_string(ctx, im_idx, "httpVersionMinor");
    duk_push_int(ctx, 0);   duk_put_prop_string(ctx, im_idx, "statusCode");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, im_idx, "statusMessage");
    duk_push_object(ctx);   duk_put_prop_string(ctx, im_idx, "trailers");
    duk_push_array(ctx);    duk_put_prop_string(ctx, im_idx, "rawTrailers");
    duk_push_true(ctx);     duk_put_prop_string(ctx, im_idx, "readable");

    /* im._rawSrc = rreq.headers || null */
    duk_get_prop_string(ctx, 0, "headers");
    if (duk_is_null_or_undefined(ctx, -1)) {
        duk_pop(ctx);
        duk_push_null(ctx);
    }
    duk_put_prop_string(ctx, im_idx, "_rawSrc");

    /* defineProperty(im, 'headers', {get, set, configurable: true, enumerable: true}) */
    duk_push_string(ctx, "headers");
    duk_push_heapptr(ctx, hdrs_get);
    duk_push_heapptr(ctx, hdrs_set);
    duk_def_prop(ctx, im_idx,
        DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER |
        DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE |
        DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);

    duk_push_string(ctx, "rawHeaders");
    duk_push_heapptr(ctx, raw_hdrs_get);
    duk_push_heapptr(ctx, raw_hdrs_set);
    duk_def_prop(ctx, im_idx,
        DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER |
        DUK_DEFPROP_HAVE_CONFIGURABLE | DUK_DEFPROP_CONFIGURABLE |
        DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);

    /* im._pendingBody = _imBuildBody(rreq.body) */
    duk_push_heapptr(ctx, build_body);
    duk_get_prop_string(ctx, 0, "body");
    duk_call(ctx, 1);
    duk_put_prop_string(ctx, im_idx, "_pendingBody");

    /* im.on = im.addListener = _imLazyOn; im.once = _imLazyOnce; im.emit = _imLazyEmit */
    duk_push_heapptr(ctx, lazy_on);
    duk_dup_top(ctx);
    duk_put_prop_string(ctx, im_idx, "on");
    duk_put_prop_string(ctx, im_idx, "addListener");
    duk_push_heapptr(ctx, lazy_once);
    duk_put_prop_string(ctx, im_idx, "once");
    duk_push_heapptr(ctx, lazy_emit);
    duk_put_prop_string(ctx, im_idx, "emit");

    /* im.socket = im.connection = _makeLazySocket(rreq) */
    duk_push_heapptr(ctx, make_socket);
    duk_dup(ctx, 0);  /* rreq */
    duk_call(ctx, 1);
    duk_dup_top(ctx);
    duk_put_prop_string(ctx, im_idx, "socket");
    duk_put_prop_string(ctx, im_idx, "connection");

    /* Return im — top of stack. */
    return 1;
}

/* _buildSRNative(rreq, server) — replacement for `new ServerResponse(rreq)`. */
static duk_ret_t nodeshim_http_build_sr(duk_context *ctx)
{
    /* Stack on entry: [rreq, server?] (server may be undefined) */
    void *sr_proto, *make_socket;

    duk_push_current_function(ctx);
    {
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sr_proto"));
        sr_proto = duk_get_heapptr(ctx, -1); duk_pop(ctx);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("make_socket"));
        make_socket = duk_get_heapptr(ctx, -1); duk_pop(ctx);
    }
    duk_pop(ctx);  /* current function */

    /* Ensure server slot exists (default undefined). */
    if (duk_get_top(ctx) < 2) duk_push_undefined(ctx);

    duk_idx_t sr_idx = duk_push_object(ctx);             /* [rreq, server, sr] */
    duk_push_heapptr(ctx, sr_proto);
    duk_set_prototype(ctx, sr_idx);

    /* sr._rreq = rreq */
    duk_dup(ctx, 0);
    duk_put_prop_string(ctx, sr_idx, "_rreq");
    /* sr._server = server */
    duk_dup(ctx, 1);
    duk_put_prop_string(ctx, sr_idx, "_server");

    duk_push_null(ctx);  duk_put_prop_string(ctx, sr_idx, "_chunks");
    duk_push_null(ctx);  duk_put_prop_string(ctx, sr_idx, "_endBody");
    duk_push_object(ctx); duk_put_prop_string(ctx, sr_idx, "_headers");
    duk_push_object(ctx); duk_put_prop_string(ctx, sr_idx, "_headerNames");
    duk_push_int(ctx, 200);  duk_put_prop_string(ctx, sr_idx, "statusCode");
    duk_push_string(ctx, ""); duk_put_prop_string(ctx, sr_idx, "statusMessage");
    duk_push_false(ctx); duk_put_prop_string(ctx, sr_idx, "headersSent");
    duk_push_true(ctx);  duk_put_prop_string(ctx, sr_idx, "sendDate");
    duk_push_false(ctx); duk_put_prop_string(ctx, sr_idx, "writableEnded");
    duk_push_false(ctx); duk_put_prop_string(ctx, sr_idx, "finished");
    duk_push_null(ctx);  duk_put_prop_string(ctx, sr_idx, "_mode");
    duk_push_false(ctx); duk_put_prop_string(ctx, sr_idx, "_didWrite");
    duk_push_false(ctx); duk_put_prop_string(ctx, sr_idx, "_ended");
    duk_push_null(ctx);  duk_put_prop_string(ctx, sr_idx, "_chunkCb");

    /* sr.socket = sr.connection = _makeLazySocket(rreq) */
    duk_push_heapptr(ctx, make_socket);
    duk_dup(ctx, 0);
    duk_call(ctx, 1);
    duk_dup_top(ctx);
    duk_put_prop_string(ctx, sr_idx, "socket");
    duk_put_prop_string(ctx, sr_idx, "connection");

    return 1;
}

/* Pull a heap pointer from a property on the _natives object at -1. */
static void _nshim_install_hidden(duk_context *ctx, duk_idx_t fn_idx,
                                  duk_idx_t natives_idx,
                                  const char *src_key, const char *hidden_key)
{
    duk_get_prop_string(ctx, natives_idx, src_key);
    duk_put_prop_string(ctx, fn_idx, hidden_key);
}

/* Attach the C function at stack top to both http and https module
   exports under the given key.  Pops the C function. */
static void _nshim_attach_to_both(duk_context *ctx, duk_idx_t pair_idx, const char *key)
{
    /* Stack: [..., fn] */
    duk_get_prop_string(ctx, pair_idx, "http");   /* [..., fn, http] */
    duk_dup(ctx, -2);                              /* [..., fn, http, fn] */
    duk_put_prop_string(ctx, -2, key);             /* [..., fn, http] */
    duk_pop(ctx);                                  /* [..., fn] */
    duk_get_prop_string(ctx, pair_idx, "https");
    duk_dup(ctx, -2);
    duk_put_prop_string(ctx, -2, key);
    duk_pop(ctx);
    duk_pop(ctx);  /* fn */
}

/* After the http_js IIFE runs, capture all the JS-side helpers + protos
   and attach native builders to both http and https module exports. */
static void nodeshim_install_native_builders(duk_context *ctx, duk_idx_t pair_idx)
{
    duk_get_prop_string(ctx, pair_idx, "_natives");
    duk_idx_t nat = duk_get_top_index(ctx);

    /* Build the IM-native function with all helper refs cached. */
    duk_push_c_function(ctx, nodeshim_http_build_im, 1);
    {
        duk_idx_t fn = duk_get_top_index(ctx);
        _nshim_install_hidden(ctx, fn, nat, "IM_proto",        DUK_HIDDEN_SYMBOL("im_proto"));
        _nshim_install_hidden(ctx, fn, nat, "imHeadersGet",    DUK_HIDDEN_SYMBOL("hdrs_get"));
        _nshim_install_hidden(ctx, fn, nat, "imHeadersSet",    DUK_HIDDEN_SYMBOL("hdrs_set"));
        _nshim_install_hidden(ctx, fn, nat, "imRawHeadersGet", DUK_HIDDEN_SYMBOL("raw_hdrs_get"));
        _nshim_install_hidden(ctx, fn, nat, "imRawHeadersSet", DUK_HIDDEN_SYMBOL("raw_hdrs_set"));
        _nshim_install_hidden(ctx, fn, nat, "imLazyOn",        DUK_HIDDEN_SYMBOL("lazy_on"));
        _nshim_install_hidden(ctx, fn, nat, "imLazyOnce",      DUK_HIDDEN_SYMBOL("lazy_once"));
        _nshim_install_hidden(ctx, fn, nat, "imLazyEmit",      DUK_HIDDEN_SYMBOL("lazy_emit"));
        _nshim_install_hidden(ctx, fn, nat, "makeLazySocket",  DUK_HIDDEN_SYMBOL("make_socket"));
        _nshim_install_hidden(ctx, fn, nat, "imBuildBody",     DUK_HIDDEN_SYMBOL("build_body"));
    }
    _nshim_attach_to_both(ctx, pair_idx, "_buildIMNative");

    /* SR-native function. */
    duk_push_c_function(ctx, nodeshim_http_build_sr, 2);
    {
        duk_idx_t fn = duk_get_top_index(ctx);
        _nshim_install_hidden(ctx, fn, nat, "SR_proto",       DUK_HIDDEN_SYMBOL("sr_proto"));
        _nshim_install_hidden(ctx, fn, nat, "makeLazySocket", DUK_HIDDEN_SYMBOL("make_socket"));
    }
    _nshim_attach_to_both(ctx, pair_idx, "_buildSRNative");

    duk_pop(ctx);  /* the _natives object */
    duk_del_prop_string(ctx, pair_idx, "_natives");
}

static void nodeshim_init_http_module_pair(duk_context *ctx)
{
    /* Compile + call once with (EventEmitter, stream) lifted from the
       partially-built top-level exports object.  Returns {http, https,
       _natives}.  Slot order guarantees `events` and `stream` slots
       ran first. */
    duk_push_string(ctx, "rampart-nodeshim.c:http_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, http_js);
    duk_get_prop_string(ctx, -2, "events");
    duk_get_prop_string(ctx, -3, "stream");
    duk_call(ctx, 2);
    /* Top of stack: {http, https, _natives}.  Wire up native builders
       and strip the _natives slot. */
    nodeshim_install_native_builders(ctx, duk_get_top_index(ctx));
}
static void nodeshim_init_http(duk_context *ctx)
{
    nodeshim_init_http_module_pair(ctx);
    duk_get_prop_string(ctx, -1, "http");
    duk_remove(ctx, -2);
}
static void nodeshim_init_https(duk_context *ctx)
{
    nodeshim_init_http_module_pair(ctx);
    duk_get_prop_string(ctx, -1, "https");
    duk_remove(ctx, -2);
}

/* ============================================================
 * net — minimal stub.  Real TCP Server/Socket would wrap
 * rampart-net.c (separate effort).  For now: `isIP`/`isIPv4`/`isIPv6`
 * cover what npm packages need at load time (express, koa).
 * ============================================================ */
static const char *net_js =
"function(EventEmitter) {\n"
"  'use strict';\n"
"  /* RFC 5321 / 5891: IPv4 = 4 dot-separated decimals 0-255;\n"
"     IPv6 = colon-separated hex groups (with optional :: compression\n"
"     and IPv4-mapped tail).  Returns 0 for non-IP, 4 for IPv4, 6 for IPv6. */\n"
"  function isIPv4(s) {\n"
"    if (typeof s !== 'string') return false;\n"
"    var p = s.split('.');\n"
"    if (p.length !== 4) return false;\n"
"    for (var i = 0; i < 4; i++) {\n"
"      var seg = p[i];\n"
"      if (!/^[0-9]+$/.test(seg)) return false;\n"
"      if (seg.length > 1 && seg.charAt(0) === '0') return false;\n"
"      var n = Number(seg);\n"
"      if (n < 0 || n > 255) return false;\n"
"    }\n"
"    return true;\n"
"  }\n"
"  function isIPv6(s) {\n"
"    if (typeof s !== 'string' || !s) return false;\n"
"    /* Strip zone-id suffix (%eth0 etc.) */\n"
"    var pct = s.indexOf('%'); if (pct >= 0) s = s.substring(0, pct);\n"
"    /* IPv4-mapped tail */\n"
"    var lastColon = s.lastIndexOf(':');\n"
"    var v4tail = false;\n"
"    if (lastColon >= 0 && s.substring(lastColon+1).indexOf('.') >= 0) {\n"
"      if (!isIPv4(s.substring(lastColon+1))) return false;\n"
"      s = s.substring(0, lastColon+1) + '0:0';\n"
"      v4tail = true;\n"
"    }\n"
"    var dbl = s.indexOf('::');\n"
"    if (dbl !== s.lastIndexOf('::')) return false;\n"
"    var parts;\n"
"    if (dbl >= 0) {\n"
"      var left  = s.substring(0, dbl);\n"
"      var right = s.substring(dbl+2);\n"
"      var lp = left  ? left.split(':')  : [];\n"
"      var rp = right ? right.split(':') : [];\n"
"      if (lp.length + rp.length > 7) return false;\n"
"      parts = lp.concat(rp);\n"
"    } else {\n"
"      parts = s.split(':');\n"
"      if (parts.length !== 8) return false;\n"
"    }\n"
"    for (var i = 0; i < parts.length; i++) {\n"
"      var seg = parts[i];\n"
"      if (!/^[0-9a-fA-F]{1,4}$/.test(seg)) return false;\n"
"    }\n"
"    return true;\n"
"  }\n"
"  function isIP(s) { return isIPv4(s) ? 4 : (isIPv6(s) ? 6 : 0); }\n"
"\n"
"  /* rampart-net.so provides the underlying TCP machinery.  We wrap it\n"
"     to provide Node's net.Socket / net.Server shape: Buffer-typed data\n"
"     chunks, the Duplex methods Express et al expect, server.address(),\n"
"     port:0 auto-pick, and the .emit alias for rampart-net's .trigger.\n"
"\n"
"     Strategy is to wrap (not mutate rampart-net's prototypes) so bare\n"
"     rampart-net callers see no behavioral change. */\n"
"  var _rnet = require('rampart-net');\n"
"\n"
"  function _toBuf(d) {\n"
"    if (Buffer.isBuffer(d)) return d;\n"
"    if (d instanceof Uint8Array) return Buffer.from(d.buffer, d.byteOffset, d.byteLength);\n"
"    return Buffer.from(String(d), 'utf8');\n"
"  }\n"
"\n"
"  /* rampart-net produces Error objects with .message set but no\n"
"     .code/.errno/.syscall.  Node-shape libs key off err.code, so\n"
"     synthesize a code from the message text. */\n"
"  function _populateCode(e) {\n"
"    if (!e || e.code) return e;\n"
"    var m = String(e.message || '');\n"
"    if      (/Connection refused/i.test(m))        { e.code = 'ECONNREFUSED'; e.errno = -111; e.syscall = 'connect'; }\n"
"    else if (/Connection reset/i.test(m))          { e.code = 'ECONNRESET';   e.errno = -104; }\n"
"    else if (/timed out|Operation timed out/i.test(m)) { e.code = 'ETIMEDOUT';     e.errno = -110; e.syscall = 'connect'; }\n"
"    else if (/No route to host/i.test(m))          { e.code = 'EHOSTUNREACH'; e.errno = -113; e.syscall = 'connect'; }\n"
"    else if (/Network is unreachable/i.test(m))    { e.code = 'ENETUNREACH';  e.errno = -101; e.syscall = 'connect'; }\n"
"    else if (/Address already in use/i.test(m))    { e.code = 'EADDRINUSE';   e.errno = -98;  e.syscall = 'listen'; }\n"
"    else if (/Broken pipe/i.test(m))               { e.code = 'EPIPE';        e.errno = -32; }\n"
"    else if (/Host (is )?not reachable/i.test(m))  { e.code = 'EHOSTUNREACH'; e.errno = -113; e.syscall = 'connect'; }\n"
"    return e;\n"
"  }\n"
"\n"
"  /* Socket wrapper.  Holds a reference to the underlying rampart-net\n"
"     Socket and translates the Node-shape API to it.  Each call to\n"
"     .on('data', fn) installs a wrapper that hands fn a Buffer. */\n"
"  function NetSocket(opts, _raw) {\n"
"    if (!(this instanceof NetSocket)) return new NetSocket(opts);\n"
"    EventEmitter.call(this);\n"
"    var self = this;\n"
"    self._raw = _raw || new _rnet.Socket(opts || {});\n"
"    self._encoding = null;\n"
"    self._destroyed = false;\n"
"    self._endCalled = false;\n"
"    self._paused = false;\n"
"    /* Server-accepted sockets are already connected when handed in;\n"
"       no 'connect' event will fire to flip _connected.  Client-side\n"
"       sockets start disconnected and flip in raw.on('connect'). */\n"
"    self._connected = !!_raw;\n"
"    self.bytesRead = 0;\n"
"    self.bytesWritten = 0;\n"
"    /* Paused-mode buffering for 'readable'+.read() consumers. */\n"
"    self._readableMode = false;\n"
"    self._readBuf = [];\n"
"    self._readLen = 0;\n"
"    self._installRawListeners();\n"
"    /* Auto-detect paused-mode usage by hooking .on / .once for\n"
"       'readable' listener registrations — first one flips us into\n"
"       paused mode. */\n"
"    var origOn = self.on;\n"
"    self.on = function(ev, fn) {\n"
"      if (ev === 'readable') self._readableMode = true;\n"
"      return origOn.call(self, ev, fn);\n"
"    };\n"
"    self.addListener = self.on;\n"
"    var origOnce = self.once;\n"
"    self.once = function(ev, fn) {\n"
"      if (ev === 'readable') self._readableMode = true;\n"
"      return origOnce.call(self, ev, fn);\n"
"    };\n"
"  }\n"
"  NetSocket.prototype = Object.create(EventEmitter.prototype);\n"
"  NetSocket.prototype.constructor = NetSocket;\n"
"\n"
"  /* Wire rampart-net events through to our EE, converting payloads\n"
"     to node-shaped objects (Buffer for data, etc). */\n"
"  NetSocket.prototype._installRawListeners = function() {\n"
"    var self = this;\n"
"    var raw = self._raw;\n"
"    raw.on('connect', function() {\n"
"      /* rampart-net populates remoteAddress/remotePort on the raw socket\n"
"         after connect for client-initiated sockets; copy through. */\n"
"      if (raw.remoteAddress) self.remoteAddress = raw.remoteAddress;\n"
"      if (raw.remotePort)    self.remotePort    = raw.remotePort;\n"
"      if (raw.localAddress)  self.localAddress  = raw.localAddress;\n"
"      if (raw.localPort)     self.localPort     = raw.localPort;\n"
"      self._connected = true;\n"
"      /* Flush any writes that arrived before the TCP handshake completed.\n"
"         Node's net.Socket buffers pre-connect writes; rampart-net throws\n"
"         'Socket is not open'.  Replay the queue in order. */\n"
"      if (self._writeQueue && self._writeQueue.length) {\n"
"        var q = self._writeQueue;\n"
"        self._writeQueue = null;\n"
"        for (var i = 0; i < q.length; i++) {\n"
"          try { self._raw.write(q[i].buf); if (q[i].cb) q[i].cb(); }\n"
"          catch(e) { if (q[i].cb) q[i].cb(e); self.emit('error', _populateCode(e)); break; }\n"
"        }\n"
"      }\n"
"      self.emit('connect');\n"
"      self.emit('ready');\n"
"    });\n"
"    raw.on('data', function(chunk) {\n"
"      if (self._destroyed) return;\n"
"      var b = _toBuf(chunk);\n"
"      self.bytesRead += b.length;\n"
"      /* If anyone registered for 'readable' (paused-mode consumers like\n"
"         mqtt-packet / aedes), buffer the chunk and fire 'readable'.\n"
"         Otherwise stay in flowing mode and emit 'data'. */\n"
"      if (self._readableMode) {\n"
"        self._readBuf.push(b);\n"
"        self._readLen += b.length;\n"
"        self.emit('readable');\n"
"      } else if (!self._paused) {\n"
"        self.emit('data', self._encoding ? b.toString(self._encoding) : b);\n"
"      } else {\n"
"        /* Flowing but paused — drop to back-buffer until resume(). */\n"
"        self._readBuf.push(b);\n"
"        self._readLen += b.length;\n"
"      }\n"
"    });\n"
"    raw.on('close', function() {\n"
"      if (!self._destroyed) {\n"
"        /* If close arrives before destroy, fire 'end' as well (peer-EOF\n"
"           is the Node semantic that 'end' represents). */\n"
"        self.emit('end');\n"
"      }\n"
"      self._destroyed = true;\n"
"      self.emit('close');\n"
"    });\n"
"    raw.on('error', function(e) { self.emit('error', _populateCode(e)); });\n"
"    raw.on('timeout', function() { self.emit('timeout'); });\n"
"  };\n"
"\n"
"  NetSocket.prototype.connect = function(/* args */) {\n"
"    /* connect(port [, host] [, cb])  OR  connect({port, host, ...} [, cb]) */\n"
"    var args = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof args[args.length - 1] === 'function') ? args.pop() : null;\n"
"    var opts;\n"
"    if (typeof args[0] === 'object' && args[0] !== null) opts = args[0];\n"
"    else opts = { port: args[0], host: args[1] || '127.0.0.1' };\n"
"    if (cb) this.once('connect', cb);\n"
"    /* rampart-net's connect expects (opts, cb) — wire it. */\n"
"    this._raw.connect(opts, function(){});\n"
"    return this;\n"
"  };\n"
"\n"
"  NetSocket.prototype.write = function(data, encoding, cb) {\n"
"    if (typeof encoding === 'function') { cb = encoding; encoding = null; }\n"
"    var b;\n"
"    if (typeof data === 'string') b = Buffer.from(data, encoding || 'utf8');\n"
"    else b = _toBuf(data);\n"
"    this.bytesWritten += b.length;\n"
"    /* Pre-connect write — buffer until 'connect' fires.  Matches Node:\n"
"       socket.write before TCP handshake completes is legal and queues. */\n"
"    if (!this._connected) {\n"
"      if (!this._writeQueue) this._writeQueue = [];\n"
"      this._writeQueue.push({buf: b, cb: cb || null});\n"
"      return true;\n"
"    }\n"
"    try { this._raw.write(b); }\n"
"    catch (e) { if (cb) cb(e); this.emit('error', e); return false; }\n"
"    if (cb) cb();\n"
"    return true;\n"
"  };\n"
"\n"
"  NetSocket.prototype.end = function(data, encoding, cb) {\n"
"    if (typeof data === 'function') { cb = data; data = null; }\n"
"    else if (typeof encoding === 'function') { cb = encoding; encoding = null; }\n"
"    if (data != null) this.write(data, encoding);\n"
"    if (this._endCalled) { if (cb) cb(); return this; }\n"
"    this._endCalled = true;\n"
"    /* rampart-net Socket has destroy() but not a half-close.  Destroying\n"
"       after writing emulates TCP FIN well enough for most consumers. */\n"
"    var self = this;\n"
"    setImmediate(function() { self.destroy(); if (cb) cb(); });\n"
"    return this;\n"
"  };\n"
"\n"
"  NetSocket.prototype.destroy = function(err) {\n"
"    if (this._destroyed) return this;\n"
"    this._destroyed = true;\n"
"    try { this._raw.destroy(); } catch(_){}\n"
"    if (err) this.emit('error', err);\n"
"    return this;\n"
"  };\n"
"\n"
"  NetSocket.prototype.pause   = function() { this._paused = true;  return this; };\n"
"  NetSocket.prototype.resume  = function() { this._paused = false; return this; };\n"
"  NetSocket.prototype.setEncoding = function(enc) { this._encoding = enc; return this; };\n"
"  /* .read([n]) — paused-mode pull.  Returns up to n bytes (or all if\n"
"     n omitted) as a Buffer, or null if no data is available. */\n"
"  NetSocket.prototype.read = function(n) {\n"
"    if (this._readLen === 0) return null;\n"
"    if (n == null || n >= this._readLen) {\n"
"      var all = (this._readBuf.length === 1) ? this._readBuf[0] : Buffer.concat(this._readBuf);\n"
"      this._readBuf = []; this._readLen = 0;\n"
"      return this._encoding ? all.toString(this._encoding) : all;\n"
"    }\n"
"    /* Partial pull — coalesce, slice, push remainder back. */\n"
"    var all2 = (this._readBuf.length === 1) ? this._readBuf[0] : Buffer.concat(this._readBuf);\n"
"    var head = all2.slice(0, n);\n"
"    var tail = all2.slice(n);\n"
"    this._readBuf = (tail.length > 0) ? [tail] : [];\n"
"    this._readLen = tail.length;\n"
"    return this._encoding ? head.toString(this._encoding) : head;\n"
"  };\n"
"  NetSocket.prototype.readable    = true;\n"
"  /* readableLength is what some npm libs probe for unread-byte count. */\n"
"  Object.defineProperty(NetSocket.prototype, 'readableLength', {\n"
"    get: function() { return this._readLen; }\n"
"  });\n"
"  NetSocket.prototype.setTimeout  = function(ms, cb) { try { this._raw.setTimeout(ms); } catch(_){} if (cb) this.once('timeout', cb); return this; };\n"
"  NetSocket.prototype.setKeepAlive = function(en, ms) { try { this._raw.setKeepAlive(en, ms || 0); } catch(_){} return this; };\n"
"  NetSocket.prototype.setNoDelay  = function(en) { try { this._raw.setNoDelay(en !== false); } catch(_){} return this; };\n"
"  NetSocket.prototype.ref    = function() { return this; };\n"
"  NetSocket.prototype.unref  = function() { return this; };\n"
"  NetSocket.prototype.address = function() {\n"
"    return { address: this.localAddress || null, family: 'IPv4', port: this.localPort || 0 };\n"
"  };\n"
"  /* Minimal pipe — wires data/end/error to dest's write/end/emit. */\n"
"  NetSocket.prototype.pipe = function(dest, opts) {\n"
"    opts = opts || {};\n"
"    var self = this, ended = false;\n"
"    self.on('data', function(chunk) {\n"
"      if (dest && typeof dest.write === 'function') {\n"
"        if (dest.write(chunk) === false && typeof self.pause === 'function') self.pause();\n"
"      }\n"
"    });\n"
"    self.on('end', function() {\n"
"      if (ended) return; ended = true;\n"
"      if (opts.end !== false && dest && typeof dest.end === 'function') dest.end();\n"
"    });\n"
"    self.on('error', function(e) {\n"
"      if (dest && typeof dest.emit === 'function') dest.emit('error', e);\n"
"    });\n"
"    if (dest && typeof dest.emit === 'function') dest.emit('pipe', self);\n"
"    return dest;\n"
"  };\n"
"  NetSocket.prototype.unpipe = function() { /* simplified: caller's responsibility */ return this; };\n"
"\n"
"  /* Server wrapper. */\n"
"  function NetServer(opts, connectionListener) {\n"
"    if (typeof opts === 'function') { connectionListener = opts; opts = {}; }\n"
"    if (!(this instanceof NetServer)) return new NetServer(opts, connectionListener);\n"
"    EventEmitter.call(this);\n"
"    var self = this;\n"
"    self._listening = false;\n"
"    self._address = null;\n"
"    self._connectionListener = (typeof connectionListener === 'function') ? connectionListener : null;\n"
"    if (self._connectionListener) self.on('connection', self._connectionListener);\n"
"    /* Build the rampart-net Server with a connection callback that\n"
"       wraps each incoming raw socket in a NetSocket and emits 'connection'. */\n"
"    self._raw = new _rnet.Server(opts || {}, function(rawSock) {\n"
"      var wrapped = new NetSocket({}, rawSock);\n"
"      self.emit('connection', wrapped);\n"
"    });\n"
"    self._raw.on('close', function() { self._listening = false; self.emit('close'); });\n"
"    self._raw.on('error', function(e) { self.emit('error', _populateCode(e)); });\n"
"  }\n"
"  NetServer.prototype = Object.create(EventEmitter.prototype);\n"
"  NetServer.prototype.constructor = NetServer;\n"
"\n"
"  NetServer.prototype.listen = function(/* args */) {\n"
"    var args = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof args[args.length - 1] === 'function') ? args.pop() : null;\n"
"    var opts;\n"
"    if (typeof args[0] === 'object' && args[0] !== null) {\n"
"      opts = { port: args[0].port, host: args[0].host || '0.0.0.0' };\n"
"    } else {\n"
"      opts = { port: args[0], host: (typeof args[1] === 'string') ? args[1] : '0.0.0.0' };\n"
"    }\n"
"    /* port 0 = OS-assigned ephemeral (rampart-net rejects).  Pick a\n"
"       random port in IANA dynamic range; caller reads it via .address(). */\n"
"    if (Number(opts.port) === 0) {\n"
"      opts.port = 49152 + Math.floor(Math.random() * (65535 - 49152));\n"
"    }\n"
"    var self = this;\n"
"    if (cb) self.once('listening', cb);\n"
"    try {\n"
"      this._raw.listen(opts);\n"
"      self._listening = true;\n"
"      self._address = { address: opts.host, family: isIPv6(opts.host) ? 'IPv6' : 'IPv4', port: Number(opts.port) };\n"
"      setImmediate(function() { self.emit('listening'); });\n"
"    } catch (e) {\n"
"      setImmediate(function() { self.emit('error', e); });\n"
"    }\n"
"    return this;\n"
"  };\n"
"\n"
"  NetServer.prototype.close = function(cb) {\n"
"    if (cb) this.once('close', cb);\n"
"    try { this._raw.close(); } catch(_){}\n"
"    return this;\n"
"  };\n"
"  NetServer.prototype.address = function() { return this._address; };\n"
"  NetServer.prototype.getConnections = function(cb) {\n"
"    var n = 0;\n"
"    try { n = this._raw.connectionCount(); } catch(_){}\n"
"    setImmediate(function() { cb(null, n); });\n"
"    return this;\n"
"  };\n"
"  NetServer.prototype.ref   = function() { return this; };\n"
"  NetServer.prototype.unref = function() { return this; };\n"
"  NetServer.prototype.maxConnections = undefined;\n"
"\n"
"  function createServer(opts, cb) { return new NetServer(opts, cb); }\n"
"  function createConnection(/* args */) {\n"
"    var args = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof args[args.length - 1] === 'function') ? args.pop() : null;\n"
"    var opts;\n"
"    if (typeof args[0] === 'object' && args[0] !== null) opts = args[0];\n"
"    else opts = { port: args[0], host: args[1] || '127.0.0.1' };\n"
"    var s = new NetSocket(opts);\n"
"    if (cb) s.once('connect', cb);\n"
"    s.connect(opts);\n"
"    return s;\n"
"  }\n"
"\n"
"  /* BlockList — minimal stub.  rampart-net has no native equivalent;\n"
"     real impl would maintain a list of address ranges.  Return a plain\n"
"     object that satisfies typeof checks. */\n"
"  function BlockList() {\n"
"    if (!(this instanceof BlockList)) return new BlockList();\n"
"    this._rules = [];\n"
"  }\n"
"  BlockList.prototype.addAddress = function() {};\n"
"  BlockList.prototype.addRange   = function() {};\n"
"  BlockList.prototype.addSubnet  = function() {};\n"
"  BlockList.prototype.check      = function() { return false; };\n"
"  BlockList.prototype.rules      = [];\n"
"\n"
"  return {\n"
"    isIP: isIP, isIPv4: isIPv4, isIPv6: isIPv6,\n"
"    createServer:     createServer,\n"
"    createConnection: createConnection,\n"
"    connect:          createConnection,\n"
"    Server:           NetServer,\n"
"    Socket:           NetSocket,\n"
"    BlockList:        BlockList\n"
"  };\n"
"}";
static void nodeshim_init_net(duk_context *ctx)
{
    /* Pass EventEmitter from the partially-built top-level exports object
       (same pattern as http_js, worker_threads_js, stream_js).  Slot order
       in duk_open_module guarantees `events` was set before `net`. */
    duk_push_string(ctx, "rampart-nodeshim.c:net_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, net_js);
    duk_get_prop_string(ctx, -2, "events");
    duk_get_prop_string(ctx, -1, "EventEmitter");
    duk_remove(ctx, -2);
    duk_call(ctx, 1);
}

/* ============================================================
 * tls — wraps rampart-net's already-built-in TLS support.
 *
 * rampart-net.so handles TLS natively: client-side via
 * `socket.connect({tls:true, hostname, insecure})` (or by upgrading an
 * existing socket via `socket.startTls(opts, cb)`), server-side via
 * `new Server({secure:true, sslKeyFile, sslCertFile})`.  This shim
 * exposes that as Node's `tls` module shape: `tls.connect`,
 * `tls.createServer`, `tls.Server`, `tls.TLSSocket`.
 *
 * `tls.TLSSocket` and `tls.Server` are aliases for `net.Socket` and
 * `net.Server`: a TLS socket IS a net.Socket whose `tls`/`secure`
 * option was set.  This means `instanceof` checks like
 * `sock instanceof tls.TLSSocket` return true for any socket created
 * by `tls.connect` (and false for plain `net.connect`, since rampart-
 * net's Socket prototype is shared but the TLS-ness is on the instance).
 *
 * Limitations vs full node tls:
 *  - `key`/`cert`/`ca` must be FILE PATHS (rampart-net uses path-based
 *    OpenSSL APIs).  Buffer/PEM-string forms throw ERR_INVALID_ARG_TYPE.
 *  - `ca` (per-connection CA override) maps to the process-wide
 *    `net.setCaCert()` setting; first call wins.
 *  - No SNI callback selection, no session resumption hooks, no
 *    `tls.connect({path: <unix-socket>})`.
 * ============================================================ */
static const char *tls_js =
"function(EventEmitter) {\n"
"  'use strict';\n"
"  /* Lazy-resolved on first use.  We can't `require('net')` at IIFE\n"
"     time because nodeshim itself is still being initialized when this\n"
"     IIFE runs — net's slot is registered but the umbrella's\n"
"     require cache isn't populated yet, so `require('net')` /\n"
"     `require('rampart-nodeshim').net` returns undefined.  By the time\n"
"     tls.connect / createServer is actually CALLED (user code), both\n"
"     resolve cleanly. */\n"
"  var _net = null, _rnet = null;\n"
"  function _modules() {\n"
"    if (!_net) { _net = require('net'); _rnet = require('rampart-net'); }\n"
"  }\n"
"\n"
"  function _requirePath(opt, name) {\n"
"    if (typeof opt === 'string') return opt;\n"
"    if (opt == null) return null;\n"
"    var e = new Error('tls: option ' + name + ' must be a file path string '\n"
"      + '(rampart-net uses path-based OpenSSL APIs; Buffer/PEM not supported)');\n"
"    e.code = 'ERR_INVALID_ARG_TYPE';\n"
"    throw e;\n"
"  }\n"
"\n"
"  /* tls.connect(port[, host][, opts][, cb])  OR  tls.connect(opts[, cb]) */\n"
"  function connect(/* args */) {\n"
"    var args = Array.prototype.slice.call(arguments);\n"
"    var cb = (typeof args[args.length - 1] === 'function') ? args.pop() : null;\n"
"    var opts;\n"
"    if (typeof args[0] === 'object' && args[0] !== null) opts = args[0];\n"
"    else opts = { port: args[0], host: args[1] || '127.0.0.1' };\n"
"    /* Buffer/PEM cert+key not supported — surface clearly. */\n"
"    if (opts.cert != null) _requirePath(opts.cert, 'cert');\n"
"    if (opts.key  != null) _requirePath(opts.key,  'key');\n"
"    if (opts.ca   != null) {\n"
"      var caFile = _requirePath(opts.ca, 'ca');\n"
"      _modules(); try { _rnet.setCaCert(caFile); } catch(_) {}\n"
"    }\n"
"    /* Build the rampart-net connect opts.  rampart-net's tls path sets\n"
"       up TLS during connect (handshake before the 'connect' event\n"
"       fires for the caller). */\n"
"    var rOpts = {\n"
"      port:     opts.port,\n"
"      host:     opts.host || opts.servername || '127.0.0.1',\n"
"      tls:      true,\n"
"      hostname: opts.servername || opts.host || null,\n"
"      insecure: (opts.rejectUnauthorized === false)\n"
"    };\n"
"    _modules();\n"
"    var s = new _net.Socket(rOpts);\n"
"    if (cb) s.once('secureConnect', cb);\n"
"    /* rampart-net fires 'connect' after the TLS handshake completes\n"
"       when tls:true is set — alias it as Node's 'secureConnect'. */\n"
"    s.on('connect', function() { s.emit('secureConnect'); });\n"
"    s.connect(rOpts);\n"
"    s.authorized = !rOpts.insecure;       /* best-effort; rampart-net does the verify */\n"
"    s.encrypted = true;\n"
"    return s;\n"
"  }\n"
"\n"
"  /* tls.createServer(opts, handler) — wraps net.createServer with the\n"
"     secure/sslKeyFile/sslCertFile options rampart-net understands. */\n"
"  function createServer(opts, handler) {\n"
"    if (typeof opts === 'function') { handler = opts; opts = {}; }\n"
"    opts = opts || {};\n"
"    var keyPath  = _requirePath(opts.key,  'key');\n"
"    var certPath = _requirePath(opts.cert, 'cert');\n"
"    if (opts.ca != null) {\n"
"      var caFile = _requirePath(opts.ca, 'ca');\n"
"      _modules(); try { _rnet.setCaCert(caFile); } catch(_) {}\n"
"    }\n"
"    /* Build net.Server with TLS opts.  Going through the shim's\n"
"       net.createServer preserves Buffer-typed 'data' events and\n"
"       .address() that node code expects. */\n"
"    _modules();\n"
"    var srv = _net.createServer({\n"
"      secure:      true,\n"
"      sslKeyFile:  keyPath,\n"
"      sslCertFile: certPath\n"
"    }, handler);\n"
"    return srv;\n"
"  }\n"
"\n"
"  /* TLSSocket / Server: alias to the shim's net.Socket / net.Server so\n"
"     instanceof checks work for sockets that came out of tls.connect.\n"
"     Lazy-resolved via getters on the returned exports object below\n"
"     (we can't resolve `net` at IIFE time — see _modules() above). */\n"
"\n"
"  /* checkServerIdentity: real verification happens inside rampart-net's\n"
"     TLS handshake via OpenSSL.  The hook is called by some user code\n"
"     as a pre-handshake sanity check; return undefined per node's\n"
"     'no error' contract. */\n"
"  function checkServerIdentity(hostname, cert) { return undefined; }\n"
"\n"
"  /* Constants probed for feature-detection at load time. */\n"
"  var DEFAULT_CIPHERS =\n"
"    'TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:' +\n"
"    'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:' +\n"
"    'ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:HIGH:!aNULL:!eNULL';\n"
"\n"
"  var exp = {\n"
"    connect:                   connect,\n"
"    createServer:              createServer,\n"
"    checkServerIdentity:       checkServerIdentity,\n"
"    getCiphers:                function() { return DEFAULT_CIPHERS.toLowerCase().split(':'); },\n"
"    rootCertificates:          [],\n"
"    DEFAULT_ECDH_CURVE:        'auto',\n"
"    DEFAULT_CIPHERS:           DEFAULT_CIPHERS,\n"
"    DEFAULT_MAX_VERSION:       'TLSv1.3',\n"
"    DEFAULT_MIN_VERSION:       'TLSv1.2',\n"
"    CLIENT_RENEG_LIMIT:        3,\n"
"    CLIENT_RENEG_WINDOW:       600\n"
"  };\n"
"  Object.defineProperty(exp, 'TLSSocket', { configurable: true, enumerable: true,\n"
"    get: function(){ _modules(); return _net.Socket; } });\n"
"  Object.defineProperty(exp, 'Server', { configurable: true, enumerable: true,\n"
"    get: function(){ _modules(); return _net.Server; } });\n"
"  return exp;\n"
"}";
static void nodeshim_init_tls(duk_context *ctx)
{
    /* Pass EventEmitter (same pattern as net_js) so the shim doesn't
       need to depend on require('events') resolving inside its IIFE. */
    duk_push_string(ctx, "rampart-nodeshim.c:tls_js");
    duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, tls_js);
    duk_get_prop_string(ctx, -2, "events");
    duk_get_prop_string(ctx, -1, "EventEmitter");
    duk_remove(ctx, -2);
    duk_call(ctx, 1);
}

#define NODESHIM_SLOT(name, init_fn) do { \
    init_fn(ctx); \
    duk_put_prop_string(ctx, -2, name); \
} while (0)

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);

    /* events must come before process (process inherits EventEmitter).
       util/assert come before others so future submodules can use them. */
    NODESHIM_SLOT("events",      nodeshim_init_events);
    NODESHIM_SLOT("util",        nodeshim_init_util);
    NODESHIM_SLOT("assert",      nodeshim_init_assert);
    NODESHIM_SLOT("path",        nodeshim_init_path);
    NODESHIM_SLOT("fs",          nodeshim_init_fs);
    NODESHIM_SLOT("crypto",      nodeshim_init_crypto);
    NODESHIM_SLOT("os",          nodeshim_init_os);
    NODESHIM_SLOT("process",     nodeshim_init_process);
    NODESHIM_SLOT("buffer",      nodeshim_init_buffer);
    NODESHIM_SLOT("querystring", nodeshim_init_querystring);
    NODESHIM_SLOT("url",         nodeshim_init_url);
    /* Tier 1 additions */
    NODESHIM_SLOT("timers",         nodeshim_init_timers);
    NODESHIM_SLOT("string_decoder", nodeshim_init_string_decoder);
    /* stream — Node stream API adapter on WHATWG Streams.  Goes after
       events (uses EventEmitter as base) and after string_decoder for
       future encoding-aware reads. */
    NODESHIM_SLOT("stream",         nodeshim_init_stream);
    NODESHIM_SLOT("module",         nodeshim_init_module);
    NODESHIM_SLOT("console",        nodeshim_init_console);
    NODESHIM_SLOT("perf_hooks",     nodeshim_init_perf_hooks);
    NODESHIM_SLOT("dns",            nodeshim_init_dns);
    NODESHIM_SLOT("zlib",           nodeshim_init_zlib);
    NODESHIM_SLOT("tty",            nodeshim_init_tty);
    NODESHIM_SLOT("readline",       nodeshim_init_readline);
    NODESHIM_SLOT("child_process",  nodeshim_init_child_process);
    NODESHIM_SLOT("vm",             nodeshim_init_vm);
    NODESHIM_SLOT("repl",           nodeshim_init_repl);
    /* Tier 2: worker_threads */
    NODESHIM_SLOT("worker_threads", nodeshim_init_worker_threads);
    /* http/https client — Phase B of nodeshim-todo.md §8.1.  Server side
       (createServer) is Phase A; throws ERR_NOT_IMPLEMENTED for now. */
    NODESHIM_SLOT("http",           nodeshim_init_http);
    NODESHIM_SLOT("https",          nodeshim_init_https);
    /* net — minimal stub.  Only `isIP`/`isIPv4`/`isIPv6` are wired today,
       which is enough for express/koa to load.  Real TCP socket support
       would wrap rampart-net.c (separate effort). */
    NODESHIM_SLOT("net",            nodeshim_init_net);
    /* tls — minimum-viable stub (loadable; throws ERR_NOT_IMPLEMENTED
       when actual TLS operations are invoked). */
    NODESHIM_SLOT("tls",            nodeshim_init_tls);

    /* α′: Auto-augment globalThis.process with node-style fields
     * (stdin/stdout/stderr with fd 0/1/2, platform, arch, hrtime,
     * memoryUsage, cpuUsage, nextTick, kill, chdir, exitCode, etc.).
     *
     * Loading rampart-nodeshim is the implicit opt-in for node-process
     * behavior — pure rampart scripts that never load nodeshim keep
     * the lean core process object.  Anyone who require()s fs/crypto/
     * etc. (transitively loads nodeshim) gets the augmented version.
     *
     * Overlaps with rampart-core's process (argv, argv0, env, exit,
     * uptime — verified functionally equivalent after the
     * process.uptime semantic fix) are LEFT INTACT so rampart-specific
     * extras like installPath, scriptPath, getCpuInfo, setMaxMem,
     * systemUptime, etc. survive the augmentation.
     *
     * Stack at this point: [exports_object]; nodeshim.process is at
     * exports.process. */
    duk_eval_string(ctx,
        "(function(nsProc){"
        "  var dst = globalThis.process;"
        "  if (!dst || typeof dst !== 'object') return;"
        "  Object.keys(nsProc).forEach(function(k){"
        "    if (k in dst) return;"  /* skip overlaps — preserve rampart-core fields */
        "    Object.defineProperty(dst, k, {"
        "      value: nsProc[k], writable: true, configurable: true, enumerable: true"
        "    });"
        "  });"
        "})");
    /* Stack: [exports, augmenter_fn].  Push exports.process as arg. */
    duk_get_prop_string(ctx, -2, "process");
    duk_call(ctx, 1);
    duk_pop(ctx);  /* discard augmenter return value (undefined) */

    /* (note: tolerant-clearTimeout α′ wrapper removed 2026-05-28 — the
       fix moved to rampart core, cmdline.c duk_rp_clear_either, where
       invalid handles now silently no-op per HTML/WHATWG/Node spec.
       All rampart code benefits, not just nodeshim consumers.) */

    /* V8 stack-trace API shim: Error.captureStackTrace +
       Error.prepareStackTrace + CallSite objects.  Used by depd
       (transitive dep of express, body-parser, etc.) to figure out
       where deprecation calls originated.
       Reuses duktape's existing parseable .stack string; produces
       CallSite-shaped objects with .getFileName/.getLineNumber/etc. */
    duk_eval_string_noresult(ctx,
        "(function(){"
        "  /* Already installed? (worker thread copy may already have it.) */"
        "  if (typeof Error.captureStackTrace === 'function' &&"
        "      Error.captureStackTrace.__rampart_v8shim) return;"
        ""
        "  /* Parse one duktape stack line into a frame descriptor.  Format:"
        "       '    at funcName (filename.js:LINE)' (+ optional modifiers)"
        "     C frames look like '    at [anon] (rampart-utils.c:NNN) internal' */"
        "  var FRAME_RE = /^\\s*at\\s+(.+?)\\s+\\(([^)]+):(\\d+)(?::(\\d+))?\\)(.*)$/;"
        "  function _parseFrames(stackStr) {"
        "    var lines = String(stackStr || '').split('\\n');"
        "    var frames = [];"
        "    for (var i = 1; i < lines.length; i++) {"
        "      var m = FRAME_RE.exec(lines[i]);"
        "      if (!m) continue;"
        "      var fn = m[2];"
        "      frames.push({"
        "        funcName: m[1],"
        "        fileName: fn,"
        "        line:     parseInt(m[3]) || 0,"
        "        col:      m[4] ? parseInt(m[4]) : 0,"
        "        isC:      fn.length >= 2 && fn.charAt(fn.length-2) === '.' && fn.charAt(fn.length-1) === 'c',"
        "        modifiers: m[5] || ''"
        "      });"
        "    }"
        "    return frames;"
        "  }"
        ""
        "  /* Build a CallSite-shaped object per V8's stack-trace API */"
        "  function _makeCallSite(frame) {"
        "    return {"
        "      getThis:         function() { return undefined; },"
        "      getTypeName:     function() { return null; },"
        "      getFunction:     function() { return undefined; },"
        "      getFunctionName: function() { return (frame.funcName === '[anon]') ? null : frame.funcName; },"
        "      getMethodName:   function() { return null; },"
        "      getFileName:     function() { return frame.fileName; },"
        "      getLineNumber:   function() { return frame.line; },"
        "      getColumnNumber: function() { return frame.col; },"
        "      getEvalOrigin:   function() { return null; },"
        "      isToplevel:      function() { return frame.funcName === 'global'; },"
        "      isEval:          function() { return false; },"
        "      isNative:        function() { return frame.isC; },"
        "      isConstructor:   function() { return false; },"
        "      isAsync:         function() { return false; },"
        "      isPromiseAll:    function() { return false; },"
        "      getPromiseIndex: function() { return null; },"
        "      toString:        function() {"
        "        var name = frame.funcName || '<anonymous>';"
        "        return name + ' (' + frame.fileName + ':' + frame.line +"
        "               (frame.col ? ':' + frame.col : '') + ')';"
        "      }"
        "    };"
        "  }"
        ""
        "  /* Default V8-style formatter (also our fallback when user hasn't"
        "     set prepareStackTrace).  Returns a string. */"
        "  function _formatV8Style(target, sites) {"
        "    var head = (target && target.name ? target.name : 'Error') +"
        "               (target && target.message ? ': ' + target.message : '');"
        "    var lines = [head];"
        "    for (var i = 0; i < sites.length; i++) lines.push('    at ' + sites[i].toString());"
        "    return lines.join('\\n');"
        "  }"
        ""
        "  /* captureStackTrace replacement: install a lazy .stack getter on"
        "     the target that dispatches through Error.prepareStackTrace when"
        "     set, otherwise returns a V8-style formatted string. */"
        "  function captureStackTrace(target, ctor) {"
        "    if (!target || (typeof target !== 'object' && typeof target !== 'function')) return;"
        "    /* Grab duktape's native stack at this moment. */"
        "    var native = (new Error()).stack || '';"
        "    var frames = _parseFrames(native);"
        "    /* V8 drops frames at-and-above the named constructor */"
        "    if (ctor && ctor.name) {"
        "      var drop = -1;"
        "      for (var i = 0; i < frames.length; i++) {"
        "        if (frames[i].funcName === ctor.name) { drop = i; break; }"
        "      }"
        "      if (drop >= 0) frames = frames.slice(drop + 1);"
        "      else if (frames.length > 0) frames = frames.slice(1);"
        "    } else if (frames.length > 0) {"
        "      /* Drop the captureStackTrace frame itself */"
        "      frames = frames.slice(1);"
        "    }"
        "    var sites = frames.map(_makeCallSite);"
        "    Object.defineProperty(target, 'stack', {"
        "      configurable: true,"
        "      get: function() {"
        "        var prep = Error.prepareStackTrace;"
        "        if (typeof prep === 'function') {"
        "          try { return prep(target, sites); } catch (_) {}"
        "        }"
        "        return _formatV8Style(target, sites);"
        "      },"
        "      set: function(v) {"
        "        Object.defineProperty(target, 'stack', {"
        "          value: v, writable: true, configurable: true, enumerable: false"
        "        });"
        "      }"
        "    });"
        "  }"
        "  captureStackTrace.__rampart_v8shim = true;"
        "  Object.defineProperty(Error, 'captureStackTrace', {"
        "    value: captureStackTrace, writable: true, configurable: true"
        "  });"
        "  /* stackTraceLimit — V8 attribute; commonly set/read. */"
        "  if (typeof Error.stackTraceLimit !== 'number') Error.stackTraceLimit = 10;"
        "})()");

    return 1;
}
