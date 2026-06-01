/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
   Copyright (C) 2026 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>
#include <libgen.h>
#include <string.h>
#include "duktape.h"
#include "module.h"
#include "rampart.h"
#include "rp_zip.h"
#include "rp_transpile.h"

/* ====================================================================
 * RAMPART_NODE_COMPAT_RESOLVE
 *
 * When 1: enable node.js-style require resolution behaviors, scoped to
 * callers whose file path contains "/node_modules/" (i.e., code that
 * came out of an `npm install`).  Two behaviors:
 *
 *   1. `node:` prefix stripping — require('node:events') resolves the
 *      same as require('events').  Stripped unconditionally; harmless
 *      for non-npm callers since rampart names never start with "node:".
 *
 *   2. For npm-located callers (caller path under `node_modules/`):
 *      (a) Bare requires (no leading `.` or `/`) try rampart builtins
 *          BEFORE the caller's own directory — fixes the case where
 *          npm packages have an internal file with the same name as a
 *          builtin (e.g. readable-stream/lib/internal/streams/stream.js
 *          doing `require('stream')` and getting itself).
 *      (b) After builtins, walk node_modules chains up from the
 *          caller's directory: try caller_dir/node_modules/X, then
 *          caller_dir/../node_modules/X, etc.  Standard node algorithm.
 *
 * Out-of-node_modules callers (your own scripts, rampart-internal
 * modules, js_modules/X.js re-exports installed in rampart's install
 * dir) hit none of the new logic — their resolution is byte-for-byte
 * identical to the pre-flag behavior.
 *
 * Set to 0 to disable both behaviors and revert to classic rampart
 * resolution everywhere.
 * ==================================================================== */
#define RAMPART_NODE_COMPAT_RESOLVE 1

#if RAMPART_NODE_COMPAT_RESOLVE

/* True if path contains the literal substring "/node_modules/".  Used
   to detect that the calling module is itself an npm package. */
static int is_under_node_modules(const char *path)
{
    return path && strstr(path, "/node_modules/") != NULL;
}

/* Try to resolve `name` via node_modules walk-up from `start_dir`.
   Tries, for each ancestor dir of start_dir:
     <ancestor>/node_modules/<name>             (as direct file w/ ext)
     <ancestor>/node_modules/<name>/index.js    (dir + index.js)
     <ancestor>/node_modules/<name>/<pkg.main>  (dir + package.json main)
   The .js / .so extension is handled by the caller's loop; here we
   just check each candidate as-is and via dir+index.js.  Returns an
   RPPATH on hit (path[0] != '\0'); empty path on miss.
   `ext` is ".js" or ".so" (passed in so we can also try the dir-with-
   index.js fallback under the right loader). */
/* Try to satisfy a directory match via package.json#main.  Returns 1 on
   success (rppath populated with a real file path); 0 if no valid main.
   Naive JSON scan — good enough for well-formed package.json files. */
static int try_pkg_main(const char *dir, RPPATH *out)
{
    char pkgpath[PATH_MAX];
    if (snprintf(pkgpath, sizeof(pkgpath), "%s/package.json", dir) >= (int)sizeof(pkgpath))
        return 0;
    struct stat pkgst;
    if (stat(pkgpath, &pkgst) != 0 || pkgst.st_size <= 0 || pkgst.st_size >= 65536)
        return 0;
    FILE *fp = fopen(pkgpath, "r");
    if (!fp) return 0;
    char buf[65536];
    size_t br = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[br] = '\0';
    const char *m = strstr(buf, "\"main\"");
    if (!m) return 0;
    m += 6;
    while (*m && *m != '"') m++;
    if (*m != '"') return 0;
    m++;
    const char *end = m;
    while (*end && *end != '"') end++;
    if (*end != '"' || (end - m) >= 200) return 0;
    char mainpath[PATH_MAX];
    if (snprintf(mainpath, sizeof(mainpath), "%s/%.*s", dir, (int)(end - m), m)
        >= (int)sizeof(mainpath))
        return 0;
    struct stat mst;
    if (stat(mainpath, &mst) == 0 && !S_ISDIR(mst.st_mode)) {
        out->stat = mst;
        if (!realpath(mainpath, out->path))
            strncpy(out->path, mainpath, sizeof(out->path) - 1);
        return 1;
    }
    /* Common npm shorthand: "main":"lib/foo" (no extension). */
    char mainjs[PATH_MAX];
    if (snprintf(mainjs, sizeof(mainjs), "%s.js", mainpath) >= (int)sizeof(mainjs))
        return 0;
    if (stat(mainjs, &mst) == 0 && !S_ISDIR(mst.st_mode)) {
        out->stat = mst;
        if (!realpath(mainjs, out->path))
            strncpy(out->path, mainjs, sizeof(out->path) - 1);
        return 1;
    }
    return 0;
}

/* NDE.35: resolve a `pkg/subpath` request via the package's
   `exports` field in package.json.
   - Splits request_id at first `/` (or second for `@scope/pkg/subpath`).
   - Walks node_modules chains up from start_dir.
   - For the first <dir>/node_modules/<pkg>/package.json found, parses
     the exports map via duktape's JSON parser and looks up `./subpath`.
   - String value: use directly.
   - Object value: try keys `require` → `node` → `default` (CJS pref),
     recursing into nested conditional exports.
   - Resolved relative path is joined to the package dir; existence-
     checked via stat.
   Returns empty RPPATH if no match. */
static duk_ret_t _safe_json_decode(duk_context *ctx, void *udata)
{
    (void)udata;
    duk_json_decode(ctx, -1);
    return 1;
}

/* On entry: value to resolve is at stack top.  On success: replaces it
   with a string and returns 1.  On no match: pops, returns 0. */
static int _resolve_conditional_export(duk_context *ctx)
{
    if (duk_is_string(ctx, -1))
        return 1;
    if (!duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        return 0;
    }
    /* Prefer require / node / default for CJS context. */
    static const char *keys[] = { "require", "node", "default" };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        if (duk_get_prop_string(ctx, -1, keys[i])) {
            if (_resolve_conditional_export(ctx)) {
                duk_remove(ctx, -2); /* drop the parent object */
                return 1;
            }
        } else {
            duk_pop(ctx); /* the undefined we just got */
        }
    }
    duk_pop(ctx);
    return 0;
}

static RPPATH try_node_modules_exports(duk_context *ctx, const char *request_id,
                                       const char *start_dir)
{
    RPPATH ret = {{0}};
    if (!ctx || !request_id || !start_dir || !*request_id || !*start_dir)
        return ret;

    /* Split request_id into <pkg> + <slash>+rest.  Skip leading
       `@scope/` for scoped packages. */
    const char *slash;
    if (request_id[0] == '@') {
        slash = strchr(request_id + 1, '/');
        if (!slash) return ret;
        slash = strchr(slash + 1, '/');
    } else {
        slash = strchr(request_id, '/');
    }
    if (!slash) return ret;

    char pkg[256];
    size_t pkglen = (size_t)(slash - request_id);
    if (pkglen >= sizeof(pkg)) return ret;
    memcpy(pkg, request_id, pkglen);
    pkg[pkglen] = '\0';

    char subpath[256];
    if (snprintf(subpath, sizeof(subpath), ".%s", slash) >= (int)sizeof(subpath))
        return ret;

    char dir[PATH_MAX];
    size_t n = strlen(start_dir);
    if (n >= sizeof(dir)) return ret;
    memcpy(dir, start_dir, n + 1);
    while (n > 1 && dir[n-1] == '/') dir[--n] = '\0';

    while (1) {
        char pkgdir[PATH_MAX];
        char pkgpath[PATH_MAX];
        if (snprintf(pkgdir, sizeof(pkgdir), "%s/node_modules/%s", dir, pkg)
            >= (int)sizeof(pkgdir))
            return ret;
        if (snprintf(pkgpath, sizeof(pkgpath), "%s/package.json", pkgdir)
            >= (int)sizeof(pkgpath))
            goto next_dir;

        struct stat sb;
        if (stat(pkgpath, &sb) == 0 && sb.st_size > 0 && sb.st_size < 1048576) {
            FILE *fp = fopen(pkgpath, "r");
            if (fp) {
                char *buf = malloc((size_t)sb.st_size + 1);
                if (buf) {
                    size_t br = fread(buf, 1, (size_t)sb.st_size, fp);
                    fclose(fp);
                    buf[br] = '\0';

                    duk_idx_t top = duk_get_top(ctx);
                    duk_push_string(ctx, buf);
                    free(buf);
                    if (duk_safe_call(ctx, _safe_json_decode, NULL, 1, 1)
                        == DUK_EXEC_SUCCESS && duk_is_object(ctx, -1)) {
                        if (duk_get_prop_string(ctx, -1, "exports")
                            && duk_is_object(ctx, -1)
                            && duk_get_prop_string(ctx, -1, subpath))
                        {
                            if (_resolve_conditional_export(ctx)) {
                                const char *rel = duk_get_string(ctx, -1);
                                if (rel) {
                                    const char *r = rel;
                                    if (r[0] == '.' && r[1] == '/') r += 2;
                                    char fullpath[PATH_MAX];
                                    if (snprintf(fullpath, sizeof(fullpath),
                                                 "%s/%s", pkgdir, r)
                                        < (int)sizeof(fullpath))
                                    {
                                        struct stat mst;
                                        if (stat(fullpath, &mst) == 0
                                            && !S_ISDIR(mst.st_mode))
                                        {
                                            ret.stat = mst;
                                            if (!realpath(fullpath, ret.path))
                                                strncpy(ret.path, fullpath,
                                                        sizeof(ret.path) - 1);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    duk_set_top(ctx, top);
                    if (strlen(ret.path)) return ret;
                } else {
                    fclose(fp);
                }
            }
        }

    next_dir:;
        char *slashp = strrchr(dir, '/');
        if (!slashp || slashp == dir) break;
        *slashp = '\0';
    }
    return ret;
}

static RPPATH walk_node_modules(const char *name, const char *start_dir, const char *ext)
{
    RPPATH ret = {{0}};
    if (!name || !start_dir || !*name || !*start_dir) return ret;

    char dir[PATH_MAX];
    size_t n = strlen(start_dir);
    if (n >= sizeof(dir)) return ret;
    memcpy(dir, start_dir, n + 1);

    /* Strip trailing slash. */
    while (n > 1 && dir[n-1] == '/') dir[--n] = '\0';

    while (1) {
        char cand[PATH_MAX];
        struct stat sb;

        /* Try <dir>/node_modules/<name> as a file with the extension
           the caller is currently looking for. */
        if (snprintf(cand, sizeof(cand), "%s/node_modules/%s%s", dir, name, ext)
            < (int)sizeof(cand))
        {
            if (stat(cand, &sb) == 0 && !S_ISDIR(sb.st_mode)) {
                ret.stat = sb;
                if (!realpath(cand, ret.path))
                    strncpy(ret.path, cand, sizeof(ret.path) - 1);
                return ret;
            }
        }

        /* Try <dir>/node_modules/<name>/ as a directory.  Caller does
           the index.js fallback for directories, so just return the
           directory path here. */
        if (snprintf(cand, sizeof(cand), "%s/node_modules/%s", dir, name)
            < (int)sizeof(cand))
        {
            if (stat(cand, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                /* If a package.json exists, try its "main" field first.
                   Minimal parser: just look for "main":"<path>" via
                   simple substring scan — good enough for typical
                   packages (json5/comments not supported, intentional).
                   On parse failure, fall through to directory return
                   and let the caller's index.js path apply. */
                char pkgpath[PATH_MAX];
                if (snprintf(pkgpath, sizeof(pkgpath), "%s/package.json", cand)
                    < (int)sizeof(pkgpath))
                {
                    struct stat pkgst;
                    if (stat(pkgpath, &pkgst) == 0 && pkgst.st_size > 0
                        && pkgst.st_size < 65536)
                    {
                        FILE *fp = fopen(pkgpath, "r");
                        if (fp) {
                            char buf[65536];
                            size_t br = fread(buf, 1, sizeof(buf) - 1, fp);
                            fclose(fp);
                            buf[br] = '\0';
                            /* Find "main" key — naive but works for
                               well-formed package.json files. */
                            const char *m = strstr(buf, "\"main\"");
                            if (m) {
                                m += 6;
                                while (*m && *m != '"') m++;
                                if (*m == '"') {
                                    m++;
                                    const char *end = m;
                                    while (*end && *end != '"') end++;
                                    if (*end == '"' && (end - m) < 200) {
                                        char mainpath[PATH_MAX];
                                        if (snprintf(mainpath, sizeof(mainpath),
                                                     "%s/%.*s", cand,
                                                     (int)(end - m), m)
                                            < (int)sizeof(mainpath))
                                        {
                                            struct stat mst;
                                            if (stat(mainpath, &mst) == 0
                                                && !S_ISDIR(mst.st_mode))
                                            {
                                                ret.stat = mst;
                                                if (!realpath(mainpath, ret.path))
                                                    strncpy(ret.path, mainpath,
                                                            sizeof(ret.path) - 1);
                                                return ret;
                                            }
                                            /* main pointed at a missing
                                               file or a directory — try
                                               adding .js (common npm
                                               shorthand: "main":"lib/foo"
                                               with no ext). */
                                            char mainjs[PATH_MAX];
                                            if (snprintf(mainjs, sizeof(mainjs),
                                                         "%s.js", mainpath)
                                                < (int)sizeof(mainjs)
                                                && stat(mainjs, &mst) == 0
                                                && !S_ISDIR(mst.st_mode))
                                            {
                                                ret.stat = mst;
                                                if (!realpath(mainjs, ret.path))
                                                    strncpy(ret.path, mainjs,
                                                            sizeof(ret.path) - 1);
                                                return ret;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                /* No package.json#main hit — return the directory and let
                   the caller's index.js fallback handle it. */
                ret.stat = sb;
                if (!realpath(cand, ret.path))
                    strncpy(ret.path, cand, sizeof(ret.path) - 1);
                return ret;
            }
        }

        /* Walk up one directory.  Stop at filesystem root. */
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) break;
        *slash = '\0';
    }
    return ret;
}

#endif /* RAMPART_NODE_COMPAT_RESOLVE */

duk_ret_t duk_rp_push_current_module(duk_context *ctx)
{
    duk_idx_t top = duk_get_top(ctx);
    const char *id=NULL;

    duk_get_global_string(ctx, "Error");
    duk_push_string(ctx, "test");
    duk_new(ctx, 1);
    duk_get_prop_string(ctx, -1, "fileName");
    id=duk_get_string(ctx, -1);
    duk_pop_2(ctx); //new error, filename

    duk_push_global_stash(ctx);
    if(duk_get_prop_string(ctx, -1, "module_id_map"))
    {
        if(duk_get_prop_string(ctx, -1, id))
        {
            duk_remove(ctx, -2);//module_id_map
            duk_remove(ctx, -2);//global stash
            //id's module object is on top
            return 1;
        }
    }
    
    duk_set_top(ctx, top);
    duk_push_undefined(ctx);
    return 0;
}

static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;

typedef int (*module_load_function) (duk_context *ctx, const char *id, duk_idx_t module_idx, int is_server);

struct module_loader
{
    char *ext;
    module_load_function loader;
};

// push error, throw if not server
#define MOD_THROW(ctx,type,...) do {\
    duk_get_prop_string(ctx, module_idx, "id");\
    const char *id=duk_get_string(ctx, -1);duk_pop(ctx);\
    duk_push_global_stash(ctx);\
    duk_get_prop_string(ctx, -1, "module_id_map");\
    duk_del_prop_string(ctx, -1, id);duk_pop_2(ctx);\
    duk_push_error_object(ctx, type, __VA_ARGS__);\
    if(is_server) return 0;\
    (void) duk_throw(ctx);\
} while(0)


static int load_js_module(duk_context *ctx, const char *file, duk_idx_t module_idx, int is_server)
{
    struct stat sb={0};
    const char *bfn=NULL;
    char *buffer = NULL;
    char *freebuffer = NULL;
    size_t len = 0;
    FILE *f = NULL;

    /* :zip:/<entry> -- module bundled in the SFX-appended zip.  Read its
       bytes via rp_zip_read and synthesize a stat (mtime=1 = stable). */
    if (strncmp(file, ":zip:/", 6) == 0)
    {
        const rp_zip_entry *e = rp_zip_resolve(file + 6);
        if (!e)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not load zip module %s: not in archive\n", file);
        unsigned char *zbuf = NULL;
        size_t zlen = 0;
        if (rp_zip_read(e, &zbuf, &zlen) != 0)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not read zip module %s\n", file);
        buffer = (char *)zbuf;
        freebuffer = buffer;
        len = zlen;
        sb.st_size  = (off_t)zlen;
        sb.st_mtime = 1;
        sb.st_atime = 1;
    }
    else
    {
        if (stat(file, &sb))
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

        f = fopen(file, "r");
        if (!f)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

        buffer = malloc(sb.st_size + 1);
        freebuffer = buffer;

        len = fread(buffer, 1, sb.st_size, f);
        if (sb.st_size != (off_t)len)
        {
            // bug fix: close file and free buffer before throwing on fread failure - 2026-02-27
            fclose(f);
            free(buffer);
            MOD_THROW(ctx, DUK_ERR_ERROR, "Error loading file %s: %s\n", file, strerror(errno));
        }
    }

    duk_push_number(ctx, sb.st_mtime);
    duk_put_prop_string(ctx, module_idx, "mtime");
    duk_push_number(ctx, sb.st_atime);
    duk_put_prop_string(ctx, module_idx, "atime");

    //skip any #! line in case this module doubles as a script
    buffer[sb.st_size]='\0';

    if(buffer[0]=='#' && buffer[1]=='!')
    {
        size_t i=0;
        while(i<len && *buffer!='\n')
        {
            i++;
            buffer++;
        }
        //if(*buffer=='\n')
        //    buffer++;  -- don't mess up line numbering -ajf 8/5/2025
    }

    /* NDE.53: `require` is a wrapper parameter (Node model) so module code
       can rebind it locally (`require = lazyCache(require)`, rewire, etc.)
       without mutating the read-only global — and without affecting other
       modules. */
    duk_push_string(ctx, "(function (module, exports, require) { ");

    /* check for babel and push src to stack */
    if (! (bfn=duk_rp_babelize(ctx, (char *)file, buffer, sb.st_mtime, babel_setting_none, NULL)) )
    {
        /* No babel, normal compile */
        //int err, lineno;
        char *isbabel = strstr(file, "/babel.js");
        /* don't tickify actual babel.js source */

        if ( !(isbabel && isbabel == file + strlen(file) - 9) )
        {
            /*
            char *tickified = tickify(buffer, sb.st_size, &err, &lineno);
            free(freebuffer);
            freebuffer = buffer = tickified;
            if (err)
            {
                MOD_THROW(ctx, DUK_ERR_SYNTAX_ERROR, "%s (line %d)\n    at %s:%d", tickify_err(err), lineno, file, lineno);
            }
            */
            int is_tickified=0;
            RP_ParseRes res = rp_get_transpiled_cached((char *)file, buffer, sb.st_mtime, &is_tickified);

            char *dbug = getenv("RPDEBUG");
            if(res.transpiled)
            {
                if( dbug && !strcasecmp (dbug, "moduleTranspiler") )
                    fprintf(stderr, "BEGIN MODULE %s\n%s\nEND MODULE %s\n", file, res.transpiled, file);
            }

            if (res.err && res.transpiled)
            {
                int err_line = res.line_num;
                duk_push_string(ctx, res.errmsg);
                const char *out = duk_get_string(ctx, -1);
                // bug fix: free parse result, close file, and free buffer before throwing on transpile error - 2026-02-27
                freeParseRes(&res);
                if (f) fclose(f);
                free(freebuffer);
                MOD_THROW(ctx, DUK_ERR_SYNTAX_ERROR, "\n%s\n    at %s:%d", out, file, err_line);
            }

            if(res.transpiled)
            {
                duk_push_string(ctx, res.transpiled);
            }
            else /* no changed in transpile */
                duk_push_string(ctx, buffer);
            freeParseRes(&res);
        }
        else /* its .../babel.js */
            duk_push_string(ctx, buffer);
    }
    // else is babel, babelized source is on top of stack.
    if (f) fclose(f);
    free(freebuffer);

    duk_push_string(ctx, "\n})");
    duk_concat(ctx, 3);

    if(bfn)
    {
    //duk_push_string(ctx, bfn);
        free((char*)bfn);
    } 
    //else - we need the orig filename in error objects for duk_rp_push_current_module above
        duk_push_string(ctx, file);

    /* 
        execute (function(module,exports) {...})(module,exports);
        1) compile and call "function(){...}", which leaves function on stack
        2) call compiled function with args (module,exports)
    */
    if(is_server)
    {
        if( duk_pcompile(ctx, DUK_COMPILE_EVAL) != 0)
            return 0;
        if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
            return 0;
    }
    else
    {
        duk_compile(ctx, DUK_COMPILE_EVAL);
        duk_call(ctx,0);
    }

    duk_dup(ctx, module_idx);
    duk_get_prop_string(ctx, -1, "exports");
    duk_get_global_string(ctx, "require");   /* NDE.53: 3rd arg — per-module require */

    if(is_server)
    {
        if (duk_pcall(ctx, 3) == DUK_EXEC_ERROR)
            return 0;
    }
    else
        duk_call(ctx, 3);

    duk_pop(ctx);
    return 1;
}

void **rp_opened_mods=NULL;
size_t rp_n_opened_mods=0;

#define addhandle_to_close(mod) do{                                     \
    REMALLOC(rp_opened_mods, (1 + rp_n_opened_mods) * sizeof(void *));  \
    rp_opened_mods[rp_n_opened_mods]=mod;                               \
    rp_n_opened_mods++;                                                 \
}while(0)

/* Process-wide cache of dlopen handles for bundled .so's.  Each thread's
   require() of e.g. "rampart-auth" triggers an independent mkstemp +
   dlopen, and dlopen of distinct paths gives distinct mappings -- each
   with its own copy of any static C state.  Cache by zip-entry name so
   all threads share one mapping (matches normal disk-dlopen semantics
   where dlopen of the same path returns the same handle). */
typedef struct zip_so_cache_s {
    char  *zname;     /* zip-entry name (key) */
    void  *handle;    /* dlopen result */
    struct zip_so_cache_s *next;
} zip_so_cache_t;

static zip_so_cache_t *zip_so_cache_head = NULL;
static pthread_mutex_t zip_so_cache_lock = PTHREAD_MUTEX_INITIALIZER;

static void *zip_so_cache_get(const char *zname)
{
    pthread_mutex_lock(&zip_so_cache_lock);
    void *h = NULL;
    for (zip_so_cache_t *c = zip_so_cache_head; c; c = c->next)
    {
        if (strcmp(c->zname, zname) == 0) { h = c->handle; break; }
    }
    pthread_mutex_unlock(&zip_so_cache_lock);
    return h;
}

static void zip_so_cache_put(const char *zname, void *handle)
{
    zip_so_cache_t *c = (zip_so_cache_t *)malloc(sizeof(*c));
    if (!c) return;
    c->zname = strdup(zname);
    c->handle = handle;
    pthread_mutex_lock(&zip_so_cache_lock);
    c->next = zip_so_cache_head;
    zip_so_cache_head = c;
    pthread_mutex_unlock(&zip_so_cache_lock);
}

/* dlopen wrapper that transparently handles :zip:/<entry>.so paths by
   extracting to a tmpfile and unlinking right after dlopen returns.  The
   inode survives the unlink via the dlopen handle, so no on-disk trace
   remains even on a crash mid-load.  Cached process-wide so multiple
   threads' require()s share one mapping (and hence one set of statics). */
static void *zip_aware_dlopen(const char *path, int flags)
{
    if (strncmp(path, ":zip:/", 6) != 0)
        return dlopen(path, flags);

    const char *zname = path + 6;

    /* return cached handle for this zip entry if we already loaded it */
    void *cached = zip_so_cache_get(zname);
    if (cached) return cached;

    const rp_zip_entry *e = rp_zip_resolve(zname);
    if (!e) { errno = ENOENT; return NULL; }

    unsigned char *zbuf = NULL;
    size_t zlen = 0;
    if (rp_zip_read(e, &zbuf, &zlen) != 0) { errno = EIO; return NULL; }

    char tp[256];
    snprintf(tp, sizeof(tp), "/tmp/rampart-zipso-XXXXXX");
    int fd = mkstemp(tp);
    if (fd < 0) { free(zbuf); return NULL; }
    ssize_t w = write(fd, zbuf, zlen);
    free(zbuf);
    close(fd);
    if (w != (ssize_t)zlen) { unlink(tp); errno = EIO; return NULL; }

    void *h = dlopen(tp, flags);
    unlink(tp); /* always unlink; inode lives via handle if dlopen succeeded */

    if (h) zip_so_cache_put(zname, h);
    return h;
}

static int load_so_module(duk_context *ctx, const char *file, duk_idx_t module_idx, int is_server)
{
    time_t mtime_for_cache = 1; /* synthetic for zip; replaced via stat() for disk */
    if (strncmp(file, ":zip:/", 6) != 0)
    {
        struct stat sb;
        if (stat(file, &sb) == 0)
            mtime_for_cache = sb.st_mtime;
    }

    pthread_mutex_lock(&modlock);
    void *lib = zip_aware_dlopen(file, RTLD_GLOBAL|RTLD_NOW); // --RTLD_GLOBAL is necessary for python to load .so modules properly

    if (lib == NULL)
    {
        /* rampart-crypto is required by other modules
           if not found in install path, try manually
           loading it from RAMPART_PATH                 */
        const char *dl_err = dlerror();
#ifdef __CYGWIN__
        /* On Cygwin, dlerror() returns a generic "No such file or directory"
           without naming the missing dependency, so always try the crypto
           fallback when any module fails to load. */
        int try_crypto = 1;
#else
        int try_crypto = (dl_err && strstr(dl_err, "rampart-crypto.so") != NULL);
#endif
        if(try_crypto)
        {
            RPPATH rp = {{0}};

            if (rp_has_zip_payload)
                rp = rp_find_zip_path("rampart-crypto.so", "modules/", "lib/rampart_modules/");
            if (!strlen(rp.path))
                rp = rp_find_path("rampart-crypto.so", "modules/", "lib/rampart_modules/");

            if(!strlen(rp.path))
            {
                pthread_mutex_unlock(&modlock);
                RP_THROW(ctx, "Error loading: %s\n%s\n%s\n",
                    dl_err,
                    "Try setting the environment variable RAMPART_PATH to the location of the rampart directory",
                    "(the directory containing the 'bin' and 'modules' directories)");
            }
            else
            {
                void *lib2 = zip_aware_dlopen(rp.path, RTLD_GLOBAL|RTLD_NOW);
                if (lib2)
                {
                    addhandle_to_close(lib2);
                    lib = zip_aware_dlopen(file, RTLD_NOW);
                    if (lib)
                        goto libload_success;
                    else
                        dl_err = dlerror();
                }
            }
        }
        pthread_mutex_unlock(&modlock);
        RP_THROW(ctx, "Error loading: %s", dl_err);

        // bug fix: removed duplicate pthread_mutex_unlock at libload_success label - 2026-02-27
        libload_success: ;
    }

    /* Set mtime so the require() cache check at module.c's _duk_resolve
       finds a value and short-circuits to "module already loaded".
       Without this, every repeated require("foo.so") re-runs this loader. */
    duk_push_number(ctx, (double)mtime_for_cache);
    duk_put_prop_string(ctx, module_idx, "mtime");

    addhandle_to_close(lib);

    duk_c_function init = (duk_c_function)dlsym(lib, "duk_open_module");
    pthread_mutex_unlock(&modlock);
    if (init != NULL)
    {
        duk_push_c_function(ctx, init, 0);
        if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
        {
            RP_THROW(ctx, "Error loading module '%s' - %s", file, duk_to_string(ctx, -1));
            return 0;
        }
        duk_put_prop_string(ctx, module_idx, "exports");
    }
    return 1;
}

/* Load a `.json` module: read the file, JSON.parse it, and set the
   result as module.exports — matching Node's built-in `.json` loader.
   Without this, require()ing a JSON file compiled it as JavaScript and
   threw a parse error.  Honours :zip:/ entries like load_js_module. */
static int load_json_module(duk_context *ctx, const char *file, duk_idx_t module_idx, int is_server)
{
    struct stat sb={0};
    char *buffer = NULL;
    char *freebuffer = NULL;
    size_t len = 0;
    FILE *f = NULL;

    if (strncmp(file, ":zip:/", 6) == 0)
    {
        const rp_zip_entry *e = rp_zip_resolve(file + 6);
        if (!e)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not load zip module %s: not in archive\n", file);
        unsigned char *zbuf = NULL;
        size_t zlen = 0;
        if (rp_zip_read(e, &zbuf, &zlen) != 0)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not read zip module %s\n", file);
        buffer = (char *)zbuf;
        freebuffer = buffer;
        len = zlen;
        sb.st_size  = (off_t)zlen;
        sb.st_mtime = 1;
        sb.st_atime = 1;
    }
    else
    {
        if (stat(file, &sb))
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

        f = fopen(file, "r");
        if (!f)
            MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

        buffer = malloc(sb.st_size + 1);
        freebuffer = buffer;

        len = fread(buffer, 1, sb.st_size, f);
        if (sb.st_size != (off_t)len)
        {
            fclose(f);
            free(buffer);
            MOD_THROW(ctx, DUK_ERR_ERROR, "Error loading file %s: %s\n", file, strerror(errno));
        }
    }
    buffer[sb.st_size] = '\0';

    duk_push_number(ctx, sb.st_mtime);
    duk_put_prop_string(ctx, module_idx, "mtime");
    duk_push_number(ctx, sb.st_atime);
    duk_put_prop_string(ctx, module_idx, "atime");

    /* Copy into the duktape heap, free our buffer, then parse. */
    duk_push_lstring(ctx, buffer, len);
    if (f) fclose(f);
    free(freebuffer);

    if (duk_safe_call(ctx, _safe_json_decode, NULL, 1, 1) != 0)
    {
        char emcopy[256];
        snprintf(emcopy, sizeof(emcopy), "%s", duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        MOD_THROW(ctx, DUK_ERR_SYNTAX_ERROR, "Could not parse JSON module %s: %s\n", file, emcopy);
    }

    /* Parsed value is on top — make it module.exports. */
    duk_put_prop_string(ctx, module_idx, "exports");
    return 1;
}

struct module_loader module_loaders[] = {
    {".js",   &load_js_module},
    /* Node resolves extensionless requires as .js, then .json, then
       .node (.so here); keep that precedence order. */
    {".json", &load_json_module},
    {".so",   &load_so_module},
    // if not known file extension assume javascript
    {"",      &load_js_module}
};

/* Pick the module_loaders[] index whose extension matches the tail of
   `path`.  Used after directory resolution settles on a concrete file
   (index.js/json/so, or a package.json#main target) so the right loader
   runs.  Falls back to the JS loader (the empty-ext entry) when nothing
   matches — mirroring "unknown extension => javascript". */
static int loader_idx_for_path(const char *path)
{
    int js_idx = 0;
    size_t plen = strlen(path);
    for (int i = 0; i < (int)(sizeof(module_loaders) / sizeof(struct module_loader)); i++)
    {
        size_t elen = strlen(module_loaders[i].ext);
        if (elen == 0) { js_idx = i; continue; }   /* the "" fallback */
        if (plen >= elen && strcmp(path + plen - elen, module_loaders[i].ext) == 0)
            return i;
    }
    return js_idx;
}

/* ===== Module dependency tracking =====
   Tracks which files a module depends on (via require()) so that
   when a submodule changes on disk, the parent is also reloaded.
*/

/* Check if all dependencies in a module's "deps" object are still current.
   Returns 1 if all deps are up to date (or no deps), 0 if any dep is stale. */
static int check_deps_current(duk_context *ctx, duk_idx_t module_obj_idx)
{
    struct stat sb;

    module_obj_idx = duk_normalize_index(ctx, module_obj_idx);

    if (!duk_get_prop_string(ctx, module_obj_idx, "deps")) {
        duk_pop(ctx);
        return 1;  /* no deps property */
    }

    if (!duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        return 1;
    }

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        const char *dep_path = duk_get_string(ctx, -2);
        time_t dep_mtime = (time_t)duk_get_number(ctx, -1);
        duk_pop_2(ctx);  /* key, value */

        if (!dep_path)
            continue;

        if (stat(dep_path, &sb) == -1) {
            /* dep file gone, treat as stale */
            duk_pop_2(ctx);  /* enum, deps */
            return 0;
        }

        if (sb.st_mtime > dep_mtime) {
            /* dep has been modified */
            duk_pop_2(ctx);  /* enum, deps */
            return 0;
        }
    }
    duk_pop_2(ctx);  /* enum, deps */
    return 1;
}

/* Push an empty deps object onto the dep_stack in global stash */
static void dep_stack_push(duk_context *ctx)
{
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "dep_stack");
    duk_push_object(ctx);
    duk_put_prop_index(ctx, -2, (duk_uarridx_t)duk_get_length(ctx, -2));
    duk_pop_2(ctx);  /* dep_stack, stash */
}

/* Pop the top deps object from dep_stack and store as "deps" on
   the module at module_idx. */
static void dep_stack_pop(duk_context *ctx, duk_idx_t module_idx)
{
    duk_uarridx_t len;

    module_idx = duk_normalize_index(ctx, module_idx);

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "dep_stack");
    len = (duk_uarridx_t)duk_get_length(ctx, -1);
    if (len > 0) {
        duk_get_prop_index(ctx, -1, len - 1);
        duk_put_prop_string(ctx, module_idx, "deps");
        /* shrink array */
        duk_push_uint(ctx, len - 1);
        duk_put_prop_string(ctx, -2, "length");
    }
    duk_pop_2(ctx);  /* dep_stack, stash */
}

/* Register a resolved module as a dependency of the current parent
   on the dep_stack. Also merges the module's own deps (transitive). */
static void dep_stack_register(duk_context *ctx, const char *id,
                               time_t mtime, duk_idx_t module_obj_idx)
{
    duk_uarridx_t len;
    duk_idx_t parent_deps_idx;

    module_obj_idx = duk_normalize_index(ctx, module_obj_idx);

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "dep_stack");
    len = (duk_uarridx_t)duk_get_length(ctx, -1);
    if (len == 0) {
        duk_pop_2(ctx);  /* dep_stack, stash */
        return;
    }

    duk_get_prop_index(ctx, -1, len - 1);  /* parent's deps object */
    parent_deps_idx = duk_normalize_index(ctx, -1);

    /* Add this module: parent_deps[id] = mtime */
    duk_push_number(ctx, (double)mtime);
    duk_put_prop_string(ctx, parent_deps_idx, id);

    /* Merge this module's deps into parent (transitive propagation) */
    if (duk_get_prop_string(ctx, module_obj_idx, "deps"))
    {
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            /* stack: ... parent_deps module_deps enum key value */
            duk_put_prop(ctx, parent_deps_idx);  /* parent_deps[key] = value; pops key+value */
        }
        duk_pop(ctx); /* enum */
    }
    duk_pop(ctx); /* module_deps or undefined */

    duk_pop_3(ctx);  /* parent_deps, dep_stack, stash */
}

/* Exported: check if a module's dependencies are all current.
   Looks up the module by id in module_id_map and checks its deps.
   Returns 1 if current, 0 if stale. */
int duk_rp_check_module_deps(duk_context *ctx, const char *module_id)
{
    int ret = 1;

    duk_push_global_stash(ctx);
    if (!duk_get_prop_string(ctx, -1, "module_id_map")) {
        duk_pop_2(ctx);
        return 1;
    }

    if (!duk_get_prop_string(ctx, -1, module_id)) {
        duk_pop_3(ctx);  /* undefined, module_id_map, stash */
        return 1;
    }

    ret = check_deps_current(ctx, -1);
    duk_pop_3(ctx);  /* module, module_id_map, stash */
    return ret;
}

static RPPATH resolve_id(duk_context *ctx, const char *request_id)
{
    char *id = NULL;
    int module_loader_idx;
    RPPATH rppath={0};
    size_t extlen=0;
    const char *modpath=NULL;

    if(!request_id)
        return rppath;

#if RAMPART_NODE_COMPAT_RESOLVE
    /* node: builtin prefix — Node 16+ canonical spelling.
       Strip it so the request resolves through the same path
       as the bare name (e.g. "node:events" → "events" → js_modules/events.js). */
    if (request_id[0] == 'n' && request_id[1] == 'o' && request_id[2] == 'd' &&
        request_id[3] == 'e' && request_id[4] == ':')
        request_id += 5;
#endif

    if(duk_rp_push_current_module(ctx))
    {
        duk_get_prop_string(ctx, -1, "path");
        modpath=duk_get_string(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);

#if RAMPART_NODE_COMPAT_RESOLVE
    /* Node-compat resolution scoped to npm-located callers.
       When the calling module's file lives under a node_modules/ tree:
         (a) for bare names (no leading `.` or `/`), try rampart's
             builtin/standard locations BEFORE the caller's own
             directory.  Fixes self-shadowing internal files (e.g.
             readable-stream/lib/internal/streams/stream.js calling
             require('stream') and getting itself).
         (b) if still not found, walk node_modules chains up from the
             caller's directory (standard Node algorithm) with
             package.json#main support.
       Out-of-node_modules callers fall through to the existing
       resolution path unchanged. */
    if (modpath && is_under_node_modules(modpath) &&
        request_id[0] != '.' && request_id[0] != '/')
    {
        /* Phase (a): try builtins/standard locations WITHOUT the
           caller's modpath.  rp_find_path/_zip with the same args
           but NULL modpath skips the caller-dir check that would
           otherwise grab the sibling file. */
        for (module_loader_idx = 0;
             module_loader_idx < (int)(sizeof(module_loaders) / sizeof(struct module_loader));
             module_loader_idx++)
        {
            const char *ext = module_loaders[module_loader_idx].ext;
            extlen = strlen(ext);
            size_t rlen = strlen(request_id);
            /* Guard the suffix compare: a request shorter than the
               extension can't end with it, and `rlen - extlen` would
               underflow (size_t) into a wild pointer. */
            const char *fext = (rlen >= extlen) ? request_id + (rlen - extlen) : request_id;

            if (extlen && rlen >= extlen && !strcmp(fext, ext)) {
                if (rp_has_zip_payload)
                    rppath = rp_find_zip_path((char*)request_id, "modules/", "lib/rampart_modules/");
                if (!strlen(rppath.path))
                    rppath = rp_find_path((char*)request_id, "modules/", "lib/rampart_modules/");
            } else {
                duk_push_string(ctx, request_id);
                duk_push_string(ctx, ext);
                duk_concat(ctx, 2);
                if (rp_has_zip_payload)
                    rppath = rp_find_zip_path((char*)duk_get_string(ctx, -1), "modules/", "lib/rampart_modules/");
                if (!strlen(rppath.path))
                    rppath = rp_find_path((char*)duk_get_string(ctx, -1), "modules/", "lib/rampart_modules/");
                duk_pop(ctx);
            }
            if (strlen(rppath.path)) { id = rppath.path; break; }
        }

        /* Phase (b): walk node_modules chains.  Try each loader's
           extension in turn so .js and .so both work. */
        if (!id) {
            /* Get caller directory from modpath (modpath is the file). */
            char dir[PATH_MAX];
            size_t dlen = strlen(modpath);
            if (dlen < sizeof(dir)) {
                memcpy(dir, modpath, dlen + 1);
                char *slash = strrchr(dir, '/');
                if (slash && slash != dir) {
                    *slash = '\0';
                    /* NDE.35: for `pkg/subpath` requests, first try the
                       package's `exports` map.  Falls through to the
                       extension walk below on miss. */
                    rppath = try_node_modules_exports(ctx, request_id, dir);
                    if (strlen(rppath.path)) {
                        /* Use 0 as the loader index (js).  .so via
                           exports is not handled here. */
                        module_loader_idx = 0;
                        id = rppath.path;
                    }
                    if (!id) {
                    for (module_loader_idx = 0;
                         module_loader_idx < (int)(sizeof(module_loaders) / sizeof(struct module_loader));
                         module_loader_idx++)
                    {
                        const char *ext = module_loaders[module_loader_idx].ext;
                        rppath = walk_node_modules(request_id, dir, ext);
                        if (strlen(rppath.path)) {
                            /* If walk returned a directory, the existing
                               post-loop index.js fallback below will
                               handle it; just record the loader. */
                            id = rppath.path;
                            break;
                        }
                    }
                    }
                }
            }
        }

        /* If found via either phase, skip the legacy resolution loop. */
        if (id) goto resolved;
    }
#endif /* RAMPART_NODE_COMPAT_RESOLVE */

    for (module_loader_idx = 0; module_loader_idx < sizeof(module_loaders) / sizeof(struct module_loader); module_loader_idx++)
    {
        const char *fext;
        size_t rlen;

        extlen=strlen(module_loaders[module_loader_idx].ext);
        rlen=strlen(request_id);
        /* Guard against size_t underflow when the request is shorter
           than the extension (a short id can't end with it anyway). */
        fext = (rlen >= extlen) ? request_id + (rlen - extlen) : request_id;

        // we have a ".so" or a '.js'
        if( extlen && rlen >= extlen && !strcmp(fext,module_loaders[module_loader_idx].ext) )
        {
            if (rp_has_zip_payload)
                rppath = rp_find_zip_path((char*)request_id, "modules/", "lib/rampart_modules/", modpath);
            if (!strlen(rppath.path))
                rppath = rp_find_path((char*)request_id, "modules/", "lib/rampart_modules/", modpath);
            id = (strlen(rppath.path))?rppath.path:NULL;
        }
        else
        {
            // try adding '.so' or '.js'
            duk_push_string(ctx, request_id);
            duk_push_string(ctx, module_loaders[module_loader_idx].ext);
            duk_concat(ctx, 2);
            if (rp_has_zip_payload)
                rppath = rp_find_zip_path((char *)duk_get_string(ctx,-1), "modules/", "lib/rampart_modules/", modpath);
            if (!strlen(rppath.path))
                rppath = rp_find_path((char *)duk_get_string(ctx,-1), "modules/", "lib/rampart_modules/", modpath);
            id = (strlen(rppath.path))?rppath.path:NULL;
            duk_pop(ctx);
        }
        if (id != NULL)
        {
            break;
        }
    }

#if RAMPART_NODE_COMPAT_RESOLVE
    /* NDE.52: conservative node fallback.  Only when rampart's own search
       above found NOTHING and the id is a bare name (not `.`/`/`/`:`):
       walk node_modules up from the calling module's directory (the
       standard Node algorithm).  Purely additive — anything rampart
       already resolved is untouched; this only rescues ids that would
       otherwise be "Could not resolve".  Because it keys off the calling
       module value (via modpath) rather than the call-site syntax, it
       fixes bare-name requires reached through any indirection (aliased /
       `.bind` / passed-through `require`), and regardless of whether the
       caller is itself under node_modules.  The gated phase (a)/(b) block
       above is left untouched, so self-shadow handling for npm-resident
       callers is unchanged. */
    if (id == NULL &&
        request_id[0] != '.' && request_id[0] != '/' && request_id[0] != ':')
    {
        /* Base dir to walk up from: the calling module's directory
           (module.path) when there is one, else the main script's
           directory (the top-level script isn't in module_id_map, so
           `modpath` is NULL there). */
        const char *base = modpath ? modpath : RP_script_path;
        for (module_loader_idx = 0;
             base &&
             module_loader_idx < (int)(sizeof(module_loaders) / sizeof(struct module_loader));
             module_loader_idx++)
        {
            rppath = walk_node_modules(request_id, base,
                                       module_loaders[module_loader_idx].ext);
            if (strlen(rppath.path)) { id = rppath.path; break; }
        }
        /* Pick the loader from the resolved file's extension (a directory
           result yields the JS loader, which is what the index.{js,json,so}
           fallback below expects). */
        if (id)
            module_loader_idx = loader_idx_for_path(rppath.path);
    }
#endif

    if (id == NULL)
    {
        return rppath;
    }

#if RAMPART_NODE_COMPAT_RESOLVE
resolved:
#endif

    /* Directory → package.json#main, then index.{js,json,so} fallback
       for the JS loader path only.  CommonJS proper doesn't require
       this, but Node's resolution algorithm does — and most npm
       packages (xtend → "main":"immutable", various scoped pkgs, etc.)
       use package.json#main with no index.js at all.  Restricted to the
       JS loaders (extension ".js" or empty); the .so loader is left
       untouched.  Node's directory index order is index.js, index.json,
       index.node — we mirror that as index.js, index.json, index.so. */
    if (S_ISDIR(rppath.stat.st_mode) &&
        module_loaders[module_loader_idx].loader == &load_js_module)
    {
        RPPATH viamain = {{0}};
        int resolved_dir = 0;
        if (try_pkg_main(rppath.path, &viamain)) {
            rppath = viamain;
            id = rppath.path;
            resolved_dir = 1;
        }
        else
        {
            static const char *idx_suffixes[] = {
                "/index.js", "/index.json", "/index.so"
            };
            for (size_t ci = 0; ci < sizeof(idx_suffixes) / sizeof(idx_suffixes[0]); ci++)
            {
                char idxpath[PATH_MAX];
                int n = snprintf(idxpath, sizeof(idxpath), "%s%s",
                                 rppath.path, idx_suffixes[ci]);
                struct stat sb;
                if (n > 0 && n < (int)sizeof(idxpath) &&
                    stat(idxpath, &sb) == 0 && !S_ISDIR(sb.st_mode))
                {
                    strncpy(rppath.path, idxpath, sizeof(rppath.path) - 1);
                    rppath.path[sizeof(rppath.path) - 1] = '\0';
                    rppath.stat = sb;
                    id = rppath.path;
                    resolved_dir = 1;
                    break;
                }
            }
        }
        if (!resolved_dir)
        {
            /* Directory resolved but no package.json#main and no
               index.{js,json,so} inside — fail resolution so the
               caller's error message is the usual "Could not resolve
               module id" rather than the misleading "Is a directory"
               from fread. */
            rppath.path[0] = '\0';
            return rppath;
        }
        /* Re-derive the loader from the resolved file's extension so an
           index.json/index.so (or a package.json#main pointing at one)
           is handled by the right loader, not always the JS loader. */
        module_loader_idx = loader_idx_for_path(rppath.path);
    }

    duk_push_string(ctx, id);
    duk_push_int(ctx, module_loader_idx);

    return rppath;
}

duk_ret_t duk_require(duk_context *ctx)
{
    duk_resolve(ctx);
    duk_get_prop_string(ctx, -1, "exports");
    return 1;
}

// If name is not null, we are calling from rampart-server.
// In that case, always force reload, and use pcall
static duk_ret_t _duk_resolve(duk_context *ctx, const char *name)
{
    int force_reload=1, is_babel=0;
    int module_loader_idx;
    duk_idx_t global_stash_idx, module_idx, module_id_map_idx;
    const char *id, *p;
    const char *fn;
    RPPATH rppath;

    duk_push_global_stash(ctx);
    global_stash_idx = duk_get_top_index(ctx);
    duk_get_prop_string(ctx, global_stash_idx, "module_id_map");
    module_id_map_idx = duk_get_top_index(ctx);

    if(!name)
    {
        force_reload = duk_get_boolean_default(ctx, 1, 0);
        fn = duk_get_string(ctx,0);
    }
    else
        fn = name;
    errno=0;

    //no need to keep checking babel src over and over
    if(fn && strcmp(fn,"@babel")==0)
    {
        if(duk_get_prop_string(ctx, module_id_map_idx, "@babel"))
            return 1;

        fn++;
        is_babel=1;
    }

    rppath = resolve_id(ctx, fn);//pushes id and module_loader_idx onto stack
    if(!strlen(rppath.path))
    {
        if(!name)
            RP_THROW(ctx, "Could not resolve module id %s: %s\n", duk_get_string(ctx, 0), errno? strerror(errno):"");
        else
            return 0;
    }

    module_loader_idx = duk_get_int(ctx, -1);
    id = duk_get_string(ctx, -2);

    if(force_reload)
    {
        duk_del_prop_string(ctx, -1, id);
    }
    // if found the module in the module_id_map
    if (duk_get_prop_string(ctx, module_id_map_idx, id))
    {
        time_t old_mtime;

        if(!duk_get_prop_string(ctx, -1, "mtime")) {
            // this should never happen
            duk_pop(ctx); // mtime
            duk_del_prop_string(ctx, -1, id);
        }
        else
        {
            old_mtime = (time_t) duk_get_number_default(ctx, -1, 0);
            duk_pop(ctx); //mtime
            if(!old_mtime)
            {
                //again, should never happen.
                duk_del_prop_string(ctx, -1, id);
            }
            else
            {
                if (rppath.stat.st_mtime > old_mtime)
                    duk_del_prop_string(ctx, -1, id); //its newer, reload
                else if (check_deps_current(ctx, -1))
                {
                    dep_stack_register(ctx, id, old_mtime, -1);
                    return 1; // module and all deps up to date
                }
                // else a dependency changed, fall through to reload
            }
        }
    }
    // module
    module_idx = duk_push_object(ctx);

    // set prototype to be the global module
    duk_get_global_string(ctx, "module");
    duk_set_prototype(ctx, module_idx);

    // module.id
    duk_push_string(ctx, id);
    duk_rp_put_prop_string_ro(ctx, module_idx, "id");

    // module.path
    p = strrchr(id,'/');
    if(p)
        duk_push_sprintf(ctx, "%.*s", (int)(p-id), id);
    else
        duk_push_string(ctx, "");        

    duk_rp_put_prop_string_ro(ctx, module_idx, "path");

    // module.exports
    duk_push_object(ctx);
    duk_put_prop_string(ctx, module_idx, "exports");

    // store 'module' in 'module_id_map'
    duk_dup(ctx, module_idx);
    duk_put_prop_string(ctx, module_id_map_idx, id);

    // if @babel store under @babel also
    if(is_babel)
    {
        duk_dup(ctx, module_idx);
        duk_put_prop_string(ctx, module_id_map_idx, "@babel");
    }

    // call appropriate module loader
    dep_stack_push(ctx);
    if(! (module_loaders[module_loader_idx].loader)(ctx, id, module_idx, (name)?1:0 ) )
    {
        dep_stack_pop(ctx, module_idx);
        duk_del_prop_string(ctx, module_id_map_idx, id);
        return -1;
    }
    dep_stack_pop(ctx, module_idx);
    dep_stack_register(ctx, id, rppath.stat.st_mtime, module_idx);
    // return module
    duk_pull(ctx, module_idx);
    return 1;
}

duk_ret_t duk_resolve(duk_context *ctx)
{
    return _duk_resolve(ctx, NULL);
}

int duk_rp_resolve(duk_context *ctx, const char *name)
{
    duk_idx_t idx=duk_get_top(ctx);// i.e. duk_get_top_index(ctx) + 1;
    int ret = (int)_duk_resolve(ctx, name);

    /* always return start stack size + 1
       for consistency in stack size
    */

    duk_insert(ctx, idx);
 
    duk_set_top(ctx, idx+1);
    return ret;   
}

void duk_module_init(duk_context *ctx)
{
    // put module_id_map in global stash
    duk_push_global_stash(ctx);                      // [... stash]
    duk_push_object(ctx);                            // [... stash obj]
    duk_dup(ctx, -1); // a copy for require.cache    // [... stash obj dup]
    duk_put_prop_string(ctx, -3, "module_id_map");   // [... stash obj]
    duk_push_array(ctx);                             // [... stash obj arr]
    duk_put_prop_string(ctx, -3, "dep_stack");       // [... stash obj]
    duk_remove(ctx, -2);  //global stash             // [... obj]

    // put require as global so code with require can eval
    duk_push_global_object(ctx);                     // [... obj global]
    duk_push_string(ctx, "require");                 // [... obj global "require"]
    duk_push_c_function(ctx, duk_require, 1);        // [... obj global "require" duk_require]
    duk_pull(ctx, -4);                               // [... global "require" duk_require obj]
    duk_put_prop_string(ctx, -2, "cache");           // [... global "require" duk_require]
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);
                                                     // [... global]
    // module.resolve
    duk_push_string(ctx, "module");                  // [... global, "module"]
    duk_push_object(ctx);                            // [... global, "module" obj]
    duk_push_c_function(ctx, duk_resolve, 2);        // [... global, "module" obj, duk_resolve]
    duk_put_prop_string(ctx, -2, "resolve");         // [... global, "module" obj]
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);
                                                     // [... global]

    duk_pop(ctx);                                    // [...]
}
