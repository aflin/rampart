/*
    rampart-auth OAuth plugin: Google

    Implements Google Sign-In using OpenID Connect (OAuth 2.0).

    Required config in auth-conf.js:

        oauth: {
            google: {
                clientId:     "your-client-id.apps.googleusercontent.com",
                clientSecret: "your-client-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/google/callback"
            }
        }
*/

rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");
var curl   = require("rampart-curl");

var pluginConf = null;
var authApi    = null;  /* set by init() — reference to auth.js API */

/* ---- plugin interface ---- */

exports.name = "google";

exports.init = function(conf, api) {
    if (!conf.oauth || !conf.oauth.google) return false;

    var gc = conf.oauth.google;
    if (!gc.clientId || !gc.clientSecret || !gc.callbackUrl) {
        fprintf(stderr, "auth-plugin google: clientId, clientSecret, and callbackUrl are required\n");
        return false;
    }

    pluginConf = gc;
    authApi = api;
    return true;
};

/* ---- OAuth flow ---- */

function startAuth(req) {
    authApi.init();
    var returnTo = (req.query && req.query.returnTo) ? req.query.returnTo : "/";

    /* generate state token and store in LMDB verifications db */
    var state = sprintf("%-0B", crypto.rand(16));
    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(authApi.getDbPath(), false, {conversion: "json"});
    lmdb.openDb("oauth_states", true);
    lmdb.put("oauth_states", state, {
        returnTo: returnTo,
        provider: "google",
        expires:  Math.floor(Date.now() / 1000) + 600  /* 10 minutes */
    });

    var url = sprintf(
        "https://accounts.google.com/o/oauth2/auth?client_id=%U&redirect_uri=%U&state=%U&response_type=code&scope=profile%%20email",
        pluginConf.clientId, pluginConf.callbackUrl, state
    );

    return {
        status: 302,
        headers: {"location": url}
    };
}

function handleCallback(req) {
    authApi.init();
    var code  = req.query ? req.query.code : null;
    var state = req.query ? req.query.state : null;

    if (!code || !state)
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

    /* validate state */
    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(authApi.getDbPath(), false, {conversion: "json"});
    lmdb.openDb("oauth_states", true);
    var stateRec = lmdb.get("oauth_states", state);
    lmdb.del("oauth_states", state);  /* consume immediately */

    if (!stateRec || stateRec.provider !== "google")
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

    if (stateRec.expires < Math.floor(Date.now() / 1000))
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

    /* exchange code for tokens */
    var tokenRes = curl.fetch("https://www.googleapis.com/oauth2/v4/token", {
        post: {
            client_id:     pluginConf.clientId,
            redirect_uri:  pluginConf.callbackUrl,
            client_secret: pluginConf.clientSecret,
            code:          code,
            grant_type:    "authorization_code"
        },
        returnText: true
    });

    var tokenData;
    try {
        tokenData = JSON.parse(tokenRes.text);
    } catch(e) {
        fprintf(stderr, "auth-plugin google: failed to parse token response: %s\n", tokenRes.text);
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    if (!tokenData.id_token) {
        fprintf(stderr, "auth-plugin google: no id_token in response\n");
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    /* decode the JWT id_token (payload is the second base64 segment) */
    var parts = tokenData.id_token.split('.');
    var userInfo;
    try {
        userInfo = JSON.parse(sprintf('%!B', parts[1]));
    } catch(e) {
        fprintf(stderr, "auth-plugin google: failed to decode id_token\n");
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    if (!userInfo.sub) {
        fprintf(stderr, "auth-plugin google: no sub in id_token\n");
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    /* return user data to auth.js — it handles session creation and cookies */
    return {
        ok: true,
        returnTo: stateRec.returnTo || "/",
        userData: {
            username:      "google_" + userInfo.sub,
            name:          userInfo.name || "",
            email:         userInfo.email || "",
            picture:       userInfo.picture || "",
            authMethod:    "google",
            oauthProvider: "google",
            oauthId:       "google:" + userInfo.sub,
            emailVerified: true
        }
    };
}

/* ---- exported endpoints ---- */

exports.endpoints = {
    "/oauth/google":          startAuth,
    "/oauth/google/callback": handleCallback
};
