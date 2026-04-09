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

    /* create or update local user */
    var oauthId  = "facebook:" + userInfo.id;
    var username = "fb_" + userInfo.id;
    var existing = authApi.getUser(username);

    if (!existing) {
        var result = authApi.createUser({
            username:      username,
            password:      sprintf("%-0B", crypto.rand(32)),
            name:          userInfo.name || "",
            email:         userInfo.email || "",
            authLevel:     50,
            authMethod:    "facebook",
            oauthProvider: "facebook",
            oauthId:       oauthId,
            picture:       picture,
            emailVerified: true
        });

        if (result.error) {
            fprintf(stderr, "auth-plugin facebook: failed to create user: %s\n", result.error);
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
        }
    } else {
        authApi.updateUser(username, {
            name:    userInfo.name || existing.name,
            email:   userInfo.email || existing.email,
            picture: picture || existing.picture || ""
        });
    }

    /* create session */
    var token = authApi.generateToken();
    var csrfToken = authApi.generateToken();
    var now = Math.floor(Date.now() / 1000);
    var user = authApi.getUser(username);

    var session = {};
    var skipKeys = {passwordHash: 1};
    for (var k in user) {
        if (!(k in skipKeys))
            session[k] = user[k];
    }
    session.csrfToken   = csrfToken;
    session.expires     = now + 86400;
    session.lastRefresh = now;
    session.created     = now;

    var Lmdb2 = require("rampart-lmdb");
    var lmdb2 = new Lmdb2.init(authApi.getDbPath(), false, {conversion: "json"});
    lmdb2.put(null, token, session);

    var cookieName = authApi.getCookieName();
    var cookie = cookieName + "=" + token + "; Path=/; Max-Age=86400; HttpOnly; SameSite=Lax";

    var returnTo = stateRec.returnTo || "/";

    return {
        status: 302,
        headers: {
            "location": returnTo,
            "Set-Cookie": [cookie]
        }
    };
}

/* ---- exported endpoints ---- */

exports.endpoints = {
    "/oauth/facebook":          startAuth,
    "/oauth/facebook/callback": handleCallback
};
