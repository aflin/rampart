/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

/*
 * rampart-auth.c — Session-based authentication module for rampart-server.
 *
 * This module is loaded by the server when authMod is set.  It exports a
 * single function that receives a req-like object (must have req.cookies),
 * looks up the session in LMDB, and attaches req.userAuth if valid.
 *
 * The server stores the parsed authModConf JSON object in a hidden symbol
 * before loading this module, so duk_open_module can read the config
 * (cookieName, dbPath) at load time.
 *
 * Third-party modules (JS or C) can replace this module by exporting a
 * function with the same contract:
 *   - receives req object (at least req.cookies)
 *   - if session is valid, sets req.userAuth = {username, authLevel, ...}
 *   - returns req
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "rampart.h"
#include "../core/module.h"

/* per-thread LMDB instance, cached in a global hidden symbol */
#define AUTH_LMDB_SYM "auth_lmdb_instance"

/* config read at module load time from hidden symbol authModConf */
static char *auth_cookie_name = NULL;
static char *auth_db_path = NULL;

/* protected paths — for file-serving true/false decisions */
typedef struct {
    char *path;
    int   pathlen;
    int   level;
    char *redirect;  /* stored but not used by auth func; server handles file redirects */
} auth_ppath;

static auth_ppath *auth_ppaths = NULL;
static int auth_num_ppaths = 0;

/* CSRF config */
static int auth_csrf_enabled = 1;  /* default: true */
static char **auth_csrf_exempt = NULL;
static int auth_num_csrf_exempt = 0;

/* sliding session expiry config */
static int auth_session_expiry = 86400;     /* total TTL in seconds (default 24h) */
static int auth_session_refresh = 300;      /* routine refresh interval (default 5 min) */
static int auth_session_refresh_urgent = 3600; /* urgent refresh threshold (default 1h remaining) */

/* ----------------------------------------------------------------
   LMDB access via rampart-lmdb duktape C API
   ---------------------------------------------------------------- */

/*
 * Get or create the per-thread lmdb instance.
 * Pushes the lmdb instance onto the stack and returns its index.
 * Returns -1 on error (nothing pushed).
 *
 * Stack on success: [ ... lmdb_instance ]  (+1)
 * Stack on failure: [ ... ]                (+0)
 */
static int get_lmdb_instance(duk_context *ctx)
{
    /* check cache */
    if (duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL(AUTH_LMDB_SYM)))
    {                                           /* stack: [ ... cached ] */
        if (duk_is_object(ctx, -1))
            return duk_get_top_index(ctx);
    }
    duk_pop(ctx);                               /* stack: [ ... ] */

    /* load rampart-lmdb */
    if (duk_rp_resolve(ctx, "rampart-lmdb") != 1)
    {
        fprintf(stderr, "rampart-auth: failed to require rampart-lmdb\n");
        duk_pop(ctx);
        return -1;
    }
    /* stack: [ ... module_obj ] */
    duk_get_prop_string(ctx, -1, "exports");    /* stack: [ ... module_obj Lmdb ] */
    duk_remove(ctx, -2);                        /* stack: [ ... Lmdb ] */

    duk_get_prop_string(ctx, -1, "init");       /* stack: [ ... Lmdb init_ctor ] */
    duk_remove(ctx, -2);                        /* stack: [ ... init_ctor ] */

    /* args: path, create=true, {conversion:"json"} */
    duk_push_string(ctx, auth_db_path);         /* stack: [ ... init_ctor path ] */
    duk_push_boolean(ctx, 1);                   /* stack: [ ... init_ctor path true ] */
    duk_push_object(ctx);                       /* stack: [ ... init_ctor path true {} ] */
    duk_push_string(ctx, "json");
    duk_put_prop_string(ctx, -2, "conversion"); /* stack: [ ... init_ctor path true {conversion:"json"} ] */

    if (duk_pnew(ctx, 3) != 0)                  /* consumes ctor+3args, pushes result */
    {
        fprintf(stderr, "rampart-auth: lmdb init failed: %s\n",
                duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return -1;
    }
    /* stack: [ ... lmdb_instance ] */

    /* cache it */
    duk_dup(ctx, -1);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL(AUTH_LMDB_SYM));

    return duk_get_top_index(ctx);
}

/* ----------------------------------------------------------------
   The exported auth check function
   ---------------------------------------------------------------- */

/*
 * Look up session from cookies on the req object.
 * If valid session found, pushes the session object onto the stack.
 *
 * Stack on success: [ ... session ]  (+1, session object pushed)
 * Stack on failure: [ ... ]          (+0, nothing pushed)
 *
 * Returns 1 if valid session found, 0 if not.
 */
static int auth_do_lookup(duk_context *ctx, duk_idx_t req_idx)
{
    duk_idx_t lmdb_idx, top_save;
    const char *token;

    top_save = duk_get_top(ctx);

    /* get req.cookies[cookieName] */
    if (!duk_get_prop_string(ctx, req_idx, "cookies"))
    {                                           /* stack: [ ... undefined ] */
        duk_pop(ctx);                           /* stack: [ ... ] */
        return 0;
    }
    /* stack: [ ... cookies_obj ] */

    if (!auth_cookie_name || !duk_get_prop_string(ctx, -1, auth_cookie_name))
    {                                           /* stack: [ ... cookies_obj undefined ] */
        duk_pop_2(ctx);                         /* stack: [ ... ] */
        return 0;
    }
    /* stack: [ ... cookies_obj token_string ] */

    token = duk_get_string(ctx, -1);
    if (!token || *token == '\0')
    {
        duk_pop_2(ctx);                         /* stack: [ ... ] */
        return 0;
    }
    /* keep token_string on stack for later use, pop cookies */
    duk_remove(ctx, -2);                        /* stack: [ ... token_string ] */

    /* LMDB lookup */
    lmdb_idx = get_lmdb_instance(ctx);
    if (lmdb_idx < 0)
    {
        duk_pop(ctx);                           /* pop token; stack: [ ... ] */
        return 0;
    }
    /* stack: [ ... token_string lmdb ] */

    duk_get_prop_string(ctx, lmdb_idx, "get");  /* stack: [ ... token lmdb get_fn ] */
    duk_dup(ctx, lmdb_idx);                     /* stack: [ ... token lmdb get_fn lmdb ] */
    duk_push_null(ctx);                         /* stack: [ ... token lmdb get_fn lmdb null ] */
    duk_push_string(ctx, token);                /* stack: [ ... token lmdb get_fn lmdb null token ] */

    if (duk_pcall_method(ctx, 2) != 0)
    {                                           /* stack: [ ... token lmdb error ] */
        duk_set_top(ctx, top_save);
        return 0;
    }
    /* stack: [ ... token lmdb result ] */

    if (duk_is_undefined(ctx, -1) || duk_is_null(ctx, -1))
    {
        duk_set_top(ctx, top_save);
        return 0;
    }

    /* check expiry and sliding refresh */
    {
        duk_idx_t session_idx = duk_get_top_index(ctx);
        double expires = 0, last_refresh = 0;
        time_t now = time(NULL);

        if (duk_get_prop_string(ctx, session_idx, "expires"))
        {
            if (duk_is_number(ctx, -1))
                expires = duk_get_number(ctx, -1);
        }
        duk_pop(ctx);                           /* pop expires val */

        if (expires > 0 && expires <= (double)now)
        {
            duk_set_top(ctx, top_save);
            return 0; /* expired */
        }

        /* sliding refresh: check if we should extend the session */
        if (auth_session_refresh > 0 && expires > 0)
        {
            if (duk_get_prop_string(ctx, session_idx, "lastRefresh"))
            {
                if (duk_is_number(ctx, -1))
                    last_refresh = duk_get_number(ctx, -1);
            }
            duk_pop(ctx);                       /* pop lastRefresh val */

            int do_refresh = 0;
            double time_left = expires - (double)now;
            double since_refresh = (double)now - last_refresh;

            /* routine: refresh if interval has elapsed */
            if (since_refresh >= (double)auth_session_refresh)
                do_refresh = 1;

            /* urgent: refresh if close to expiring */
            if (auth_session_refresh_urgent > 0 && time_left < (double)auth_session_refresh_urgent)
                do_refresh = 1;

            if (do_refresh)
            {
                double new_expires = (double)now + (double)auth_session_expiry;

                /* update session object */
                duk_push_number(ctx, new_expires);
                duk_put_prop_string(ctx, session_idx, "expires");
                duk_push_number(ctx, (double)now);
                duk_put_prop_string(ctx, session_idx, "lastRefresh");

                /* write back to LMDB: lmdb.put(null, token, session) */
                /* token string is below lmdb on the stack */
                {
                    duk_idx_t token_idx = lmdb_idx - 1; /* token is right below lmdb */

                    duk_get_prop_string(ctx, lmdb_idx, "put");
                                                /* stack: [ ... token lmdb session put_fn ] */
                    duk_dup(ctx, lmdb_idx);     /* stack: [ ... token lmdb session put_fn lmdb ] */
                    duk_push_null(ctx);         /* stack: [ ... token lmdb session put_fn lmdb null ] */
                    duk_dup(ctx, token_idx);    /* stack: [ ... token lmdb session put_fn lmdb null token ] */
                    duk_dup(ctx, session_idx);  /* stack: [ ... token lmdb session put_fn lmdb null token session ] */

                    if (duk_pcall_method(ctx, 3) != 0)
                    {
                        /* write failed — not fatal, just skip refresh */
                        /* stack: [ ... token lmdb session error ] */
                    }
                    duk_pop(ctx);               /* pop put result or error */
                    /* stack: [ ... token lmdb session ] */
                }
            }
        }
    }

    /* valid session — remove token and lmdb, leave session on stack */
    duk_remove(ctx, lmdb_idx);                  /* stack: [ ... token session ] */
    duk_remove(ctx, -2);                        /* remove token; stack: [ ... session ] */
    return 1;
}

/* check if a path is CSRF-exempt */
static int auth_is_csrf_exempt(const char *request_path)
{
    int i;
    for (i = 0; i < auth_num_csrf_exempt; i++)
    {
        int len = (int)strlen(auth_csrf_exempt[i]);
        if (strncmp(request_path, auth_csrf_exempt[i], len) == 0)
            return 1;
    }
    return 0;
}

/*
 * Check CSRF token for a state-changing request.
 * Looks for token in req.postData.content._csrf or req.headers['X-CSRF-Token'].
 * Compares against req.userAuth.csrfToken.
 *
 * Returns 1 if valid (or check not needed), 0 if CSRF violation.
 *
 * Stack is left clean (same top as entry).
 */
static int auth_check_csrf(duk_context *ctx, duk_idx_t req_idx)
{
    const char *session_token = NULL;
    const char *submitted_token = NULL;
    int valid = 0;

    /* get the expected token from req.userAuth.csrfToken */
    if (duk_get_prop_string(ctx, req_idx, "userAuth"))
    {                                           /* stack: [ ... userAuth ] */
        if (duk_get_prop_string(ctx, -1, "csrfToken"))
        {                                       /* stack: [ ... userAuth csrfToken ] */
            if (duk_is_string(ctx, -1))
                session_token = duk_get_string(ctx, -1);
        }
        duk_pop(ctx);                           /* pop csrfToken; stack: [ ... userAuth ] */
    }
    duk_pop(ctx);                               /* pop userAuth; stack: [ ... ] */

    if (!session_token || *session_token == '\0')
        return 0; /* no CSRF token in session — deny state-changing requests */

    /* check req.postData.content._csrf */
    if (duk_get_prop_string(ctx, req_idx, "postData"))
    {                                           /* stack: [ ... postData ] */
        if (duk_get_prop_string(ctx, -1, "content"))
        {                                       /* stack: [ ... postData content ] */
            if (duk_is_object(ctx, -1))
            {
                if (duk_get_prop_string(ctx, -1, "_csrf"))
                {                               /* stack: [ ... postData content _csrf ] */
                    if (duk_is_string(ctx, -1))
                        submitted_token = duk_get_string(ctx, -1);
                }
                duk_pop(ctx);                   /* pop _csrf */
            }
            else if (duk_is_string(ctx, -1))
            {
                /* JSON post with _csrf field */
                /* content is already the parsed object — handled above */
            }
        }
        duk_pop(ctx);                           /* pop content */
    }
    duk_pop(ctx);                               /* pop postData; stack: [ ... ] */

    /* if not in postData, check X-CSRF-Token header */
    if (!submitted_token)
    {
        if (duk_get_prop_string(ctx, req_idx, "headers"))
        {                                       /* stack: [ ... headers ] */
            if (duk_get_prop_string(ctx, -1, "X-CSRF-Token"))
            {                                   /* stack: [ ... headers val ] */
                if (duk_is_string(ctx, -1))
                    submitted_token = duk_get_string(ctx, -1);
            }
            duk_pop(ctx);                       /* pop val */
        }
        duk_pop(ctx);                           /* pop headers; stack: [ ... ] */
    }

    if (submitted_token && strcmp(submitted_token, session_token) == 0)
        valid = 1;

    return valid;
}

/* find the most specific protected path match */
static auth_ppath *auth_find_ppath(const char *request_path)
{
    auth_ppath *best = NULL;
    int best_len = 0, i;
    for (i = 0; i < auth_num_ppaths; i++)
    {
        auth_ppath *pp = &auth_ppaths[i];
        if (strncmp(request_path, pp->path, pp->pathlen) == 0 && pp->pathlen > best_len)
        {
            best = pp;
            best_len = pp->pathlen;
        }
    }
    return best;
}

/*
 * Auth check function called by the server.
 *
 * Contract:
 *   For files (mini-req with cookies + path, no method):
 *     return true  → serve the file
 *     return false → server sends 403 (or redirect based on config)
 *
 *   For app modules (full req with method, headers, etc.):
 *     modify req (set req.userAuth), return req → pass to endpoint
 *     return false → server sends 403
 *     return {redirect: "/path"} → server sends 302
 *
 * Stack contract:
 *   Entry:  [ req ]  (1 argument)
 *   Exit:   [ retval ]  (1 return value)
 */
static duk_ret_t auth_check_func(duk_context *ctx)
{
    duk_idx_t req_idx = 0;
    int is_app_module;

    /* detect file vs app module: app modules have req.method */
    is_app_module = (duk_get_prop_string(ctx, req_idx, "method") && duk_is_string(ctx, -1));
    duk_pop(ctx);                               /* pop method or undefined; stack: [ req ] */

    if (!is_app_module)
    {
        /* ---- FILE SERVING: return true or false ---- */
        const char *request_path = NULL;
        auth_ppath *pp;
        int user_level;

        /* get the path */
        if (duk_get_prop_string(ctx, req_idx, "path"))
        {
            if (duk_is_object(ctx, -1))
            {
                if (duk_get_prop_string(ctx, -1, "path"))
                    request_path = duk_get_string(ctx, -1);
                duk_pop(ctx); /* pop path string */
            }
        }
        duk_pop(ctx);                           /* pop path obj; stack: [ req ] */

        if (!request_path)
        {
            duk_push_boolean(ctx, 0);
            return 1; /* no path info — deny */
        }

        /* check if path is protected */
        pp = auth_find_ppath(request_path);
        if (!pp)
        {
            duk_push_boolean(ctx, 1);
            return 1; /* not protected — allow */
        }

        /* protected — need valid session with sufficient level */
        if (!auth_do_lookup(ctx, req_idx))
        {                                       /* stack: [ req ] */
            duk_push_boolean(ctx, 0);
            return 1; /* no valid session — deny */
        }
        /* stack: [ req session ] */

        /* check authLevel */
        user_level = -1;
        if (duk_get_prop_string(ctx, -1, "authLevel"))
        {
            if (duk_is_number(ctx, -1))
                user_level = duk_get_int(ctx, -1);
        }
        duk_pop(ctx);                           /* pop authLevel; stack: [ req session ] */
        duk_pop(ctx);                           /* pop session; stack: [ req ] */

        duk_push_boolean(ctx, (user_level >= 0 && user_level <= pp->level) ? 1 : 0);
        return 1;
    }
    else
    {
        /* ---- APP MODULE: modify req, return req/false/{redirect} ---- */
        const char *request_path = NULL;
        auth_ppath *pp = NULL;
        int has_session = 0;

        /* get the request path */
        if (duk_get_prop_string(ctx, req_idx, "path"))
        {                                       /* stack: [ req path_obj ] */
            if (duk_is_object(ctx, -1))
            {
                if (duk_get_prop_string(ctx, -1, "path"))
                    request_path = duk_get_string(ctx, -1);
                duk_pop(ctx);                   /* pop path string */
            }
        }
        duk_pop(ctx);                           /* pop path obj; stack: [ req ] */

        /* try to look up session and set userAuth */
        if (auth_do_lookup(ctx, req_idx))
        {                                       /* stack: [ req session ] */
            duk_put_prop_string(ctx, req_idx, "userAuth");
                                                /* stack: [ req ] */
            has_session = 1;
        }

        /* check if path is protected */
        if (request_path)
            pp = auth_find_ppath(request_path);

        if (pp)
        {
            /* protected path — check privilege level */
            /* helper: push {redirect: url} with $origin substituted */
            #define PUSH_REDIRECT_OR_FALSE(pp, path) do { \
                if ((pp)->redirect && (path)) { \
                    /* substitute $origin in redirect template */ \
                    const char *tmpl = (pp)->redirect; \
                    const char *origin = (path); \
                    char buf[2048]; \
                    const char *t = tmpl; \
                    int pos = 0; \
                    while (*t && pos < (int)sizeof(buf) - 4) { \
                        if (strncmp(t, "$origin", 7) == 0) { \
                            const char *s = origin; \
                            while (*s && pos < (int)sizeof(buf) - 4) { \
                                unsigned char c = (unsigned char)*s; \
                                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || \
                                    (c >= '0' && c <= '9') || c == '/' || c == '-' || \
                                    c == '_' || c == '.' || c == '~') \
                                    buf[pos++] = c; \
                                else { snprintf(&buf[pos], 4, "%%%02X", c); pos += 3; } \
                                s++; \
                            } \
                            t += 7; \
                        } else buf[pos++] = *t++; \
                    } \
                    buf[pos] = '\0'; \
                    duk_push_object(ctx); \
                    duk_push_string(ctx, buf); \
                    duk_put_prop_string(ctx, -2, "redirect"); \
                    return 1; \
                } \
                duk_push_boolean(ctx, 0); \
                return 1; \
            } while(0)

            if (!has_session)
            {
                PUSH_REDIRECT_OR_FALSE(pp, request_path);
            }
            else
            {
                /* has session — check authLevel */
                int user_level = -1;

                if (duk_get_prop_string(ctx, req_idx, "userAuth"))
                {                               /* stack: [ req userAuth ] */
                    if (duk_get_prop_string(ctx, -1, "authLevel"))
                    {
                        if (duk_is_number(ctx, -1))
                            user_level = duk_get_int(ctx, -1);
                    }
                    duk_pop(ctx);               /* pop authLevel */
                }
                duk_pop(ctx);                   /* pop userAuth; stack: [ req ] */

                if (user_level < 0 || user_level > pp->level)
                {
                    duk_del_prop_string(ctx, req_idx, "userAuth");
                    PUSH_REDIRECT_OR_FALSE(pp, request_path);
                }
            }
            #undef PUSH_REDIRECT_OR_FALSE
        }

        /* CSRF check for state-changing methods */
        if (auth_csrf_enabled && has_session)
        {
            const char *method = NULL;
            int needs_csrf = 0;

            if (duk_get_prop_string(ctx, req_idx, "method"))
                method = duk_get_string(ctx, -1);
            duk_pop(ctx);                       /* pop method; stack: [ req ] */

            if (method && (strcmp(method, "POST") == 0 ||
                           strcmp(method, "PUT") == 0 ||
                           strcmp(method, "DELETE") == 0 ||
                           strcmp(method, "PATCH") == 0))
            {
                needs_csrf = 1;
            }

            if (needs_csrf && request_path && !auth_is_csrf_exempt(request_path))
            {
                if (!auth_check_csrf(ctx, req_idx))
                {
                    /* CSRF violation — deny */
                    duk_push_boolean(ctx, 0);
                    return 1; /* return false → 403 */
                }
            }
        }

        /* allowed — return req (with userAuth if session was valid) */
        /* stack: [ req ] */
        return 1;
    }
}

/* ================================================================
   duk_open_module — called when require("rampart-auth") or when
   the server loads the module.

   Reads config from global serverConf.authModConf (set by server),
   initializes LMDB, and returns the auth check function.
   ================================================================ */

duk_ret_t duk_open_module(duk_context *ctx)
{
    /* read config from hidden symbol set by rampart-server before loading us */
    if (duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("authModConf")))
    {                                           /* stack: [ authModConf ] */
        if (duk_is_object(ctx, -1))
        {
            /* cookieName */
            if (auth_cookie_name) { free(auth_cookie_name); auth_cookie_name = NULL; }
            if (duk_get_prop_string(ctx, -1, "cookieName"))
                auth_cookie_name = strdup(duk_get_string(ctx, -1));
            else
                auth_cookie_name = strdup("rp_session");
            duk_pop(ctx);                       /* pop cookieName value */

            /* dbPath */
            if (auth_db_path) { free(auth_db_path); auth_db_path = NULL; }
            if (duk_get_prop_string(ctx, -1, "dbPath"))
                auth_db_path = strdup(duk_get_string(ctx, -1));
            else
                auth_db_path = strdup("data/auth");
            duk_pop(ctx);                       /* pop dbPath value */

            /* protectedPaths */
            if (auth_ppaths) { free(auth_ppaths); auth_ppaths = NULL; auth_num_ppaths = 0; }
            if (duk_get_prop_string(ctx, -1, "protectedPaths") && duk_is_object(ctx, -1))
            {
                int count = 0, j = 0;
                duk_enum(ctx, -1, 0);
                while (duk_next(ctx, -1, 0)) { count++; duk_pop(ctx); }
                duk_pop(ctx); /* pop enum */

                auth_ppaths = calloc(count, sizeof(auth_ppath));
                auth_num_ppaths = count;

                duk_enum(ctx, -1, 0);
                while (duk_next(ctx, -1, 1))
                {
                    auth_ppaths[j].path = strdup(duk_get_string(ctx, -2));
                    auth_ppaths[j].pathlen = (int)strlen(auth_ppaths[j].path);
                    if (duk_get_prop_string(ctx, -1, "level"))
                        auth_ppaths[j].level = duk_get_int_default(ctx, -1, 0);
                    duk_pop(ctx);
                    if (duk_get_prop_string(ctx, -1, "redirect"))
                        auth_ppaths[j].redirect = strdup(duk_get_string(ctx, -1));
                    else
                        auth_ppaths[j].redirect = NULL;
                    duk_pop(ctx);
                    duk_pop_2(ctx); /* key and value */
                    j++;
                }
                duk_pop(ctx); /* pop enum */
            }
            duk_pop(ctx);                       /* pop protectedPaths or undefined */

            /* csrf (default true) */
            auth_csrf_enabled = 1;
            if (duk_get_prop_string(ctx, -1, "csrf"))
            {
                if (duk_is_boolean(ctx, -1))
                    auth_csrf_enabled = duk_get_boolean(ctx, -1);
            }
            duk_pop(ctx);                       /* pop csrf value */

            /* csrfExemptPaths */
            if (auth_csrf_exempt)
            {
                int j;
                for (j = 0; j < auth_num_csrf_exempt; j++)
                    free(auth_csrf_exempt[j]);
                free(auth_csrf_exempt);
                auth_csrf_exempt = NULL;
                auth_num_csrf_exempt = 0;
            }
            if (duk_get_prop_string(ctx, -1, "csrfExemptPaths") && duk_is_array(ctx, -1))
            {
                int n = (int)duk_get_length(ctx, -1), j;
                auth_csrf_exempt = calloc(n, sizeof(char *));
                auth_num_csrf_exempt = n;
                for (j = 0; j < n; j++)
                {
                    duk_get_prop_index(ctx, -1, (duk_uarridx_t)j);
                    auth_csrf_exempt[j] = strdup(duk_safe_to_string(ctx, -1));
                    duk_pop(ctx);
                }
            }
            duk_pop(ctx);                       /* pop csrfExemptPaths or undefined */

            /* session expiry config */
            if (duk_get_prop_string(ctx, -1, "sessionExpiry"))
            {
                if (duk_is_number(ctx, -1))
                    auth_session_expiry = duk_get_int(ctx, -1);
            }
            duk_pop(ctx);                       /* pop sessionExpiry */

            if (duk_get_prop_string(ctx, -1, "sessionRefresh"))
            {
                if (duk_is_number(ctx, -1))
                    auth_session_refresh = duk_get_int(ctx, -1);
            }
            duk_pop(ctx);                       /* pop sessionRefresh */

            if (duk_get_prop_string(ctx, -1, "sessionRefreshUrgent"))
            {
                if (duk_is_number(ctx, -1))
                    auth_session_refresh_urgent = duk_get_int(ctx, -1);
            }
            duk_pop(ctx);                       /* pop sessionRefreshUrgent */

            /* ensure db directory exists */
            {
                struct stat sb;
                if (stat(auth_db_path, &sb) == -1)
                    rp_mkdir_parent(auth_db_path, 0755);
            }

            /* pre-warm: create lmdb instance on this context */
            {
                int idx = get_lmdb_instance(ctx);
                if (idx >= 0)
                    duk_pop(ctx);               /* pop lmdb instance */
                else
                    fprintf(stderr, "rampart-auth: warning: lmdb pre-warm failed\n");
            }

            fprintf(stderr, "rampart-auth: initialized (cookie=%s, db=%s, %d protected paths, csrf=%s)\n",
                    auth_cookie_name, auth_db_path, auth_num_ppaths,
                    auth_csrf_enabled ? "on" : "off");
        }
    }
    duk_pop(ctx);                               /* pop authModConf or undefined */

    /* return the auth check function */
    duk_push_c_function(ctx, auth_check_func, 1);
    return 1;
}
