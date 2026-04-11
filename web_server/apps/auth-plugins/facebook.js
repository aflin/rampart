/*
    rampart-auth OAuth plugin: Facebook

    Implements Facebook Login using OAuth 2.0 + Graph API.

    Required config in auth-conf.js:

        oauth: {
            facebook: {
                clientId:     "your-app-id",
                clientSecret: "your-app-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/facebook/callback",
                apiVersion:   "v21.0"    // check developers.facebook.com for current version
            }
        }
*/

rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");
var curl   = require("rampart-curl");

var pluginConf = null;
var authApi    = null;
var apiVersion = "v21.0";

/* ---- plugin interface ---- */

exports.name = "facebook";

exports.init = function(conf, api) {
    if (!conf.oauth || !conf.oauth.facebook) return false;

    var fc = conf.oauth.facebook;
    if (!fc.clientId || !fc.clientSecret || !fc.callbackUrl) {
        fprintf(stderr, "auth-plugin facebook: clientId, clientSecret, and callbackUrl are required\n");
        return false;
    }

    pluginConf = fc;
    authApi = api;
    if (fc.apiVersion) apiVersion = fc.apiVersion;
    return true;
};

/* ---- OAuth flow ---- */

function startAuth(req) {
    authApi.init();
    var returnTo = (req.query && req.query.returnTo) ? req.query.returnTo : "/";

    var state = sprintf("%-0B", crypto.rand(16));
    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(authApi.getDbPath(), false, {conversion: "json"});
    lmdb.openDb("oauth_states", true);
    lmdb.put("oauth_states", state, {
        returnTo: returnTo,
        provider: "facebook",
        expires:  Math.floor(Date.now() / 1000) + 600
    });

    var url = sprintf(
        "https://www.facebook.com/%s/dialog/oauth?client_id=%U&redirect_uri=%U&state=%U&scope=email,public_profile",
        apiVersion, pluginConf.clientId, pluginConf.callbackUrl, state
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
    lmdb.del("oauth_states", state);

    if (!stateRec || stateRec.provider !== "facebook")
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

    if (stateRec.expires < Math.floor(Date.now() / 1000))
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

    /* exchange code for access token */
    var tokenUrl = sprintf(
        "https://graph.facebook.com/%s/oauth/access_token?client_id=%U&redirect_uri=%U&client_secret=%U&code=%U",
        apiVersion, pluginConf.clientId, pluginConf.callbackUrl, pluginConf.clientSecret, code
    );

    var tokenRes = curl.fetch(tokenUrl, {returnText: true});
    var tokenData;
    try {
        tokenData = JSON.parse(tokenRes.text);
    } catch(e) {
        fprintf(stderr, "auth-plugin facebook: failed to parse token response: %s\n", tokenRes.text);
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    if (!tokenData.access_token) {
        fprintf(stderr, "auth-plugin facebook: no access_token in response: %s\n", tokenRes.text);
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    /* fetch user profile */
    var profileUrl = sprintf(
        "https://graph.facebook.com/%s/me?fields=id,first_name,last_name,name,email&access_token=%U",
        apiVersion, tokenData.access_token
    );

    var profileRes = curl.fetch(profileUrl, {returnText: true});
    var userInfo;
    try {
        userInfo = JSON.parse(profileRes.text);
    } catch(e) {
        fprintf(stderr, "auth-plugin facebook: failed to parse profile response: %s\n", profileRes.text);
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    if (!userInfo.id) {
        fprintf(stderr, "auth-plugin facebook: no id in profile response: %s\n", profileRes.text);
        return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
    }

    /* fetch profile picture */
    var picture = "";
    try {
        var picUrl = sprintf(
            "https://graph.facebook.com/%s/%s/picture?type=normal&redirect=false&access_token=%U",
            apiVersion, userInfo.id, tokenData.access_token
        );
        var picRes = curl.fetch(picUrl, {returnText: true});
        var picData = JSON.parse(picRes.text);
        if (picData.data && picData.data.url)
            picture = picData.data.url;
    } catch(e) {
        /* non-fatal — continue without picture */
    }

    /* return user data to auth.js — it handles session creation and cookies */
    return {
        ok: true,
        returnTo: stateRec.returnTo || "/",
        userData: {
            username:      "fb_" + userInfo.id,
            name:          userInfo.name || "",
            email:         userInfo.email || "",
            picture:       picture,
            authMethod:    "facebook",
            oauthProvider: "facebook",
            oauthId:       "facebook:" + userInfo.id,
            emailVerified: true
        }
    };
}

/* ---- exported endpoints ---- */

exports.endpoints = {
    "/oauth/facebook":          startAuth,
    "/oauth/facebook/callback": handleCallback
};
