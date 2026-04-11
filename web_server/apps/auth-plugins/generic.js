/*
    rampart-auth OAuth plugin: Generic OAuth 2.0

    Handles any standard OAuth 2.0 / OpenID Connect provider.
    Supports optional PKCE (required by some providers like Twitter/X).

    Each provider entry in auth-conf.js with plugin:"generic" is handled
    by this single plugin.  Endpoints are created dynamically:
        /oauth/<name>           — starts the flow
        /oauth/<name>/callback  — handles the callback

    Example configuration in auth-conf.js:

        oauth: {
            github: {
                plugin:       "generic",
                authorizeUrl: "https://github.com/login/oauth/authorize",
                tokenUrl:     "https://github.com/login/oauth/access_token",
                userInfoUrl:  "https://api.github.com/user",
                clientId:     "your-client-id",
                clientSecret: "your-client-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/github/callback",
                scope:        "user:email",
                fieldMap: {
                    id:       "id",
                    name:     "name",
                    email:    "email",
                    picture:  "avatar_url"
                }
            },
            twitter: {
                plugin:       "generic",
                authorizeUrl: "https://twitter.com/i/oauth2/authorize",
                tokenUrl:     "https://api.twitter.com/2/oauth2/token",
                userInfoUrl:  "https://api.twitter.com/2/users/me?user.fields=profile_image_url",
                clientId:     "your-client-id",
                clientSecret: "your-client-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/twitter/callback",
                scope:        "users.read tweet.read",
                pkce:         true,
                fieldMap: {
                    id:       "id",
                    name:     "name",
                    email:    "email",
                    picture:  "profile_image_url"
                }
            }
        }
*/

rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");
var curl   = require("rampart-curl");

var authApi   = null;
var providers = {};  /* keyed by provider name from conf.oauth */

/* default field mapping (OIDC standard) */
var defaultFieldMap = {
    id:      "sub",
    name:    "name",
    email:   "email",
    picture: "picture"
};

/* ---- plugin interface ---- */

exports.name = "generic";
exports.endpoints = {};

exports.init = function(conf, api) {
    if (!conf.oauth) return false;

    authApi = api;
    var count = 0;

    for (var key in conf.oauth) {
        var pc = conf.oauth[key];
        if (pc.plugin !== "generic") continue;

        if (!pc.authorizeUrl || !pc.tokenUrl || !pc.clientId || !pc.clientSecret || !pc.callbackUrl) {
            fprintf(stderr, "auth-plugin generic/%s: authorizeUrl, tokenUrl, clientId, clientSecret, and callbackUrl are required\n", key);
            continue;
        }

        providers[key] = {
            authorizeUrl: pc.authorizeUrl,
            tokenUrl:     pc.tokenUrl,
            userInfoUrl:  pc.userInfoUrl || null,
            emailUrl:     pc.emailUrl || null,
            clientId:     pc.clientId,
            clientSecret: pc.clientSecret,
            callbackUrl:  pc.callbackUrl,
            scope:        pc.scope || "openid profile email",
            pkce:         !!pc.pkce,
            fieldMap:     pc.fieldMap || defaultFieldMap
        };

        exports.endpoints["/oauth/" + key] = makeStartHandler(key);
        exports.endpoints["/oauth/" + key + "/callback"] = makeCallbackHandler(key);
        count++;
        fprintf(stderr, "  generic oauth provider: %s (%s)\n", key, pc.pkce ? "with PKCE" : "standard");
    }

    return count > 0;
};

/* ---- PKCE helpers ---- */

function generateCodeVerifier() {
    return sprintf("%-0B", crypto.rand(32));
}

function generateCodeChallenge(verifier) {
    /* S256: base64url(sha256(ascii(code_verifier))) */
    var hash = crypto.sha256(verifier, true);  /* true = return Buffer */
    return sprintf("%-0B", hash);
}

/* ---- response parsing ---- */

/* parse JSON or form-encoded response (some providers return form-encoded) */
function parseResponse(text) {
    if (!text) return null;
    try {
        return JSON.parse(text);
    } catch(e) {
        /* try form-encoded (e.g. older GitHub token responses) */
        var obj = {};
        var pairs = text.split("&");
        for (var i = 0; i < pairs.length; i++) {
            var kv = pairs[i].split("=");
            if (kv.length >= 2)
                obj[decodeURIComponent(kv[0])] = decodeURIComponent(kv.slice(1).join("="));
        }
        return Object.keys(obj).length ? obj : null;
    }
}

/* extract user info from nested response (e.g. Twitter wraps in {data: {...}}) */
function flattenUserInfo(obj) {
    if (obj && obj.data && typeof obj.data === "object" && !Array.isArray(obj.data))
        return obj.data;
    return obj;
}

/* ---- email fetching ---- */

/* fetch email from a separate endpoint if the main profile didn't include one.
   Handles common formats:
     - Array of objects with email/primary/verified fields (GitHub)
     - Array of strings
     - Object with an email field                                        */
function fetchEmail(emailUrl, accessToken) {
    try {
        var res = curl.fetch(emailUrl, {
            returnText: true,
            headers: [
                "Authorization: Bearer " + accessToken,
                "Accept: application/json"
            ]
        });
        var data = parseResponse(res.text);
        if (!data) return "";

        if (Array.isArray(data)) {
            /* look for primary+verified first, then any verified, then first */
            var primary = null, verified = null;
            for (var i = 0; i < data.length; i++) {
                var entry = data[i];
                if (typeof entry === "string") return entry;
                if (entry.primary && entry.verified) return entry.email;
                if (entry.primary) primary = entry.email;
                if (entry.verified && !verified) verified = entry.email;
            }
            return primary || verified || (data[0] && data[0].email) || "";
        }

        if (data.email) return data.email;
    } catch(e) {
        /* non-fatal */
    }
    return "";
}

/* ---- OAuth flow ---- */

function makeStartHandler(providerName) {
    return function(req) {
        authApi.init();
        var pc = providers[providerName];
        var returnTo = (req.query && req.query.returnTo) ? req.query.returnTo : "/";

        var state = sprintf("%-0B", crypto.rand(16));
        var Lmdb = require("rampart-lmdb");
        var lmdb = new Lmdb.init(authApi.getDbPath(), false, {conversion: "json"});
        lmdb.openDb("oauth_states", true);

        var stateData = {
            returnTo: returnTo,
            provider: providerName,
            expires:  Math.floor(Date.now() / 1000) + 600
        };

        var url;
        if (pc.pkce) {
            var verifier = generateCodeVerifier();
            stateData.codeVerifier = verifier;
            var challenge = generateCodeChallenge(verifier);
            url = sprintf(
                "%s?client_id=%U&redirect_uri=%U&state=%U&response_type=code&scope=%U&code_challenge=%U&code_challenge_method=S256",
                pc.authorizeUrl, pc.clientId, pc.callbackUrl, state, pc.scope, challenge
            );
        } else {
            url = sprintf(
                "%s?client_id=%U&redirect_uri=%U&state=%U&response_type=code&scope=%U",
                pc.authorizeUrl, pc.clientId, pc.callbackUrl, state, pc.scope
            );
        }

        lmdb.put("oauth_states", state, stateData);

        return {
            status: 302,
            headers: {"location": url}
        };
    };
}

function makeCallbackHandler(providerName) {
    return function(req) {
        authApi.init();
        var pc = providers[providerName];
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

        if (!stateRec || stateRec.provider !== providerName)
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

        if (stateRec.expires < Math.floor(Date.now() / 1000))
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

        /* exchange code for tokens */
        var postData = {
            client_id:     pc.clientId,
            redirect_uri:  pc.callbackUrl,
            client_secret: pc.clientSecret,
            code:          code,
            grant_type:    "authorization_code"
        };

        if (pc.pkce && stateRec.codeVerifier)
            postData.code_verifier = stateRec.codeVerifier;

        var tokenRes = curl.fetch(pc.tokenUrl, {
            post:       postData,
            returnText: true,
            headers:    ["Accept: application/json"]
        });

        var tokenData = parseResponse(tokenRes.text);
        if (!tokenData || (!tokenData.access_token && !tokenData.id_token)) {
            fprintf(stderr, "auth-plugin generic/%s: no access_token or id_token in response: %s\n",
                    providerName, tokenRes.text);
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
        }

        /* get user info: try id_token JWT first, then userinfo endpoint */
        var userInfo = null;

        if (tokenData.id_token) {
            try {
                var parts = tokenData.id_token.split('.');
                userInfo = JSON.parse(sprintf('%!B', parts[1]));
            } catch(e) {
                fprintf(stderr, "auth-plugin generic/%s: failed to decode id_token: %s\n",
                        providerName, e.message || e);
            }
        }

        if (!userInfo && pc.userInfoUrl && tokenData.access_token) {
            var uiRes = curl.fetch(pc.userInfoUrl, {
                returnText: true,
                headers:    [
                    "Authorization: Bearer " + tokenData.access_token,
                    "Accept: application/json"
                ]
            });
            userInfo = parseResponse(uiRes.text);
            if (userInfo)
                userInfo = flattenUserInfo(userInfo);
        }

        if (!userInfo) {
            fprintf(stderr, "auth-plugin generic/%s: could not get user info\n", providerName);
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
        }

        /* map fields from provider response to standard userData */
        var fm  = pc.fieldMap;
        var uid = userInfo[fm.id || "sub"];
        if (!uid) {
            fprintf(stderr, "auth-plugin generic/%s: no id field '%s' in user info: %J\n",
                    providerName, fm.id || "sub", userInfo);
            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
        }

        var email = userInfo[fm.email || "email"] || "";

        /* if no email in profile and emailUrl is configured, try fetching it */
        if (!email && pc.emailUrl && tokenData.access_token)
            email = fetchEmail(pc.emailUrl, tokenData.access_token);

        return {
            ok: true,
            returnTo: stateRec.returnTo || "/",
            userData: {
                username:      providerName + "_" + uid,
                name:          userInfo[fm.name    || "name"]    || "",
                email:         email,
                picture:       userInfo[fm.picture || "picture"] || "",
                authMethod:    providerName,
                oauthProvider: providerName,
                oauthId:       providerName + ":" + uid,
                emailVerified: true
            }
        };
    };
}
