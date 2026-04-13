/*
    rampart-auth administration and login module.

    Dual-purpose:
      1) require("apps/auth") — programmatic API for login, logout, user management
      2) Server-mapped app module — admin web interface at /apps/auth/

    User records are stored in the "users" named database in the auth LMDB.
    Session records are in the "default" database (shared with rampart-auth.c).
*/

rampart.globalize(rampart.utils);

var crypto = require("rampart-crypto");
var Lmdb   = require("rampart-lmdb");

/* ---- configuration ---- */

var conf    = {};
var dbPath  = "";
var lmdb    = null;
var hooks   = {};

/* cookie flags from config, with secure defaults */
var cookieFlags = {
    httpOnly: true,
    sameSite: "Lax",
    secure: true,
    path: "/",
    maxAge: 86400
};

var minPasswordLength = 7;

var lockout = {
    attempts: 5,        /* max failed attempts before lockout */
    window:   300,      /* time window in seconds (5 min) */
    duration: 900       /* lockout duration in seconds (15 min) */
};

var emailConf = null;       /* email sending config, null = disabled */
var displayCookieFields = null; /* fields to include in client-readable display cookie */
var allowRegistration = false;
var requireEmailVerification = true;
var siteUrl = "";           /* base URL for links in emails */

function init() {
    if (lmdb) return; /* already initialized */

    /* read config — load the auth-conf.js module */
    if (typeof serverConf !== "undefined" && serverConf.authModConf) {
        try {
            conf = require(serverConf.authModConf);
        } catch(e) {
            /* authModConf might already be the parsed object (e.g., from tests) */
            if (typeof serverConf.authModConf === "object")
                conf = serverConf.authModConf;
        }
    }

    if (conf.dbPath) {
        dbPath = conf.dbPath;
    } else if (typeof serverConf !== "undefined" && serverConf.dataRoot) {
        dbPath = serverConf.dataRoot + "/auth";
    } else {
        /* CLI or standalone: look for data/auth relative to the parent of the
           apps directory (i.e., the web server root) */
        dbPath = process.scriptPath + "/../data/auth";
        try { dbPath = realPath(dbPath); } catch(e) {}
    }

    lmdb = new Lmdb.init(dbPath, true, {conversion: "json"});

    /* ensure named databases exist */
    lmdb.openDb("users", true);
    lmdb.openDb("lockouts", true);
    lmdb.openDb("resets", true);
    lmdb.openDb("verifications", true);

    /* hooks from config */
    hooks = {
        onLogin:                conf.onLogin || null,
        onSessionCreated:       conf.onSessionCreated || null,
        onLogout:               conf.onLogout || null,
        onPasswordResetRequest: conf.onPasswordResetRequest || null
    };

    /* cookie config */
    if (conf.cookieFlags) {
        for (var k in conf.cookieFlags)
            cookieFlags[k] = conf.cookieFlags[k];
    }
    if (conf.sessionExpiry)
        cookieFlags.maxAge = conf.sessionExpiry;

    /* lockout config */
    if (conf.lockoutAttempts !== undefined) lockout.attempts = conf.lockoutAttempts;
    if (conf.lockoutWindow !== undefined)   lockout.window = conf.lockoutWindow;
    if (conf.lockoutDuration !== undefined) lockout.duration = conf.lockoutDuration;

    /* password policy */
    if (conf.minPasswordLength !== undefined) minPasswordLength = conf.minPasswordLength;

    /* email cooldown */
    if (conf.emailCooldown !== undefined) emailCooldown = conf.emailCooldown;

    /* display cookie — client-readable cookie with user info for static pages */
    if (conf.displayCookie)
        displayCookieFields = conf.displayCookie;

    /* email and registration config */
    if (conf.email && typeof conf.email === "object")
        emailConf = conf.email;
    if (conf.allowRegistration !== undefined)
        allowRegistration = conf.allowRegistration;
    if (conf.requireEmailVerification !== undefined)
        requireEmailVerification = conf.requireEmailVerification;
    if (conf.siteUrl)
        siteUrl = conf.siteUrl.replace(/\/+$/, ''); /* strip trailing slash */
}

/* ---- helpers ---- */

function generateToken() {
    /* %-0B = URL-safe base64 (- and _ instead of + and /, no = padding) */
    return sprintf("%-0B", crypto.rand(32));
}

function generateCsrfToken() {
    return sprintf("%-0B", crypto.rand(24));
}

function now() {
    return Math.floor(Date.now() / 1000);
}

/* validate a returnTo URL — must be a local path, not an external redirect */
function safeReturnTo(url) {
    if (!url || typeof url !== "string") return "/";
    url = url.trim();
    /* must start with / and not // (protocol-relative) */
    if (url.charAt(0) !== '/' || url.charAt(1) === '/') return "/";
    /* block javascript: and data: URIs */
    var lower = url.toLowerCase();
    if (lower.indexOf("javascript:") >= 0 || lower.indexOf("data:") >= 0) return "/";
    return url;
}

function buildCookieString(name, value, opts) {
    var parts = [name + "=" + value];
    if (opts.path)     parts.push("Path=" + opts.path);
    if (opts.maxAge)   parts.push("Max-Age=" + opts.maxAge);
    if (opts.httpOnly) parts.push("HttpOnly");
    if (opts.secure)   parts.push("Secure");
    if (opts.sameSite) parts.push("SameSite=" + opts.sameSite);
    return parts.join("; ");
}


/* ---- user CRUD ---- */

function createUser(opts) {
    init();
    if (!opts || !opts.username || !opts.password)
        return {error: "username and password are required"};

    var username = opts.username.toLowerCase().trim();
    if (username.length < 3 || username.length > 64)
        return {error: "username must be 3-64 characters"};

    if (opts.password.length < minPasswordLength)
        return {error: "password must be at least " + minPasswordLength + " characters"};

    /* check if user exists — hash anyway to prevent timing enumeration */
    var existing = lmdb.get("users", username);
    if (existing) {
        crypto.passwd("dummy"); /* constant-time delay */
        return {error: "user already exists"};
    }

    /* hash password */
    var pwHash = crypto.passwd(opts.password);

    var user = {
        username:     username,
        name:         opts.name || username,
        email:        opts.email || "",
        authLevel:    (typeof opts.authLevel === "number") ? opts.authLevel : 50,
        authMethod:   opts.authMethod || "password",
        passwordHash: pwHash.line,
        created:      now(),
        mustResetPassword: opts.mustResetPassword || false,
        emailVerified: (opts.emailVerified !== undefined) ? opts.emailVerified : true
    };

    /* store any extra fields */
    for (var k in opts) {
        if (!(k in user) && k !== "password")
            user[k] = opts[k];
    }

    lmdb.put("users", username, user);
    return {ok: true, username: username};
}

function getUser(username) {
    init();
    if (!username) return null;
    return lmdb.get("users", username.toLowerCase().trim()) || null;
}

function listUsers(maxUsers) {
    init();
    maxUsers = maxUsers || 1000;
    return lmdb.get("users", "", maxUsers);
}

function updateUser(username, updates) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    for (var k in updates) {
        if (k === "username") continue; /* can't change username */
        if (k === "password") {
            user.passwordHash = crypto.passwd(updates.password).line;
            continue;
        }
        user[k] = updates[k];
    }

    lmdb.put("users", username, user);
    return {ok: true, username: username};
}

function deleteUser(username) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    /* delete user record */
    lmdb.del("users", username);

    /* delete all sessions for this user */
    var sessions = lmdb.get(null, "", "*");
    if (sessions) {
        for (var token in sessions) {
            if (sessions[token].username === username)
                lmdb.del(null, token);
        }
    }

    return {ok: true, username: username};
}

function changePassword(username, oldPassword, newPassword) {
    init();
    if (newPassword.length < minPasswordLength)
        return {error: "password must be at least " + minPasswordLength + " characters"};

    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    if (!crypto.passwdCheck(user.passwordHash, oldPassword))
        return {error: "incorrect password"};

    user.passwordHash = crypto.passwd(newPassword).line;
    user.mustResetPassword = false;
    lmdb.put("users", username, user);

    /* revoke all existing sessions — old tokens are no longer valid */
    deleteAllSessions(username);

    return {ok: true};
}

function adminResetPassword(username, newPassword, mustChange) {
    init();
    if (newPassword.length < minPasswordLength)
        return {error: "password must be at least " + minPasswordLength + " characters"};
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    user.passwordHash = crypto.passwd(newPassword).line;
    user.mustResetPassword = (mustChange !== false); /* default true */
    user.emailVerified = true; /* admin-initiated reset implies verified */
    lmdb.put("users", username, user);

    /* revoke all existing sessions — force re-login with new password */
    deleteAllSessions(username);

    return {ok: true, mustResetPassword: user.mustResetPassword};
}

function requestPasswordReset(username) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    /* generate a time-limited reset token */
    var resetToken = generateToken();
    var resetExpiry = now() + 3600; /* 1 hour */

    /* store reset token in a "resets" database */
    lmdb.put("resets", resetToken, {
        username: username,
        expires:  resetExpiry
    });

    var resetUrl = "/apps/auth/do-reset?token=" + sprintf("%U", resetToken);

    /* call hook if configured */
    if (hooks.onPasswordResetRequest)
        hooks.onPasswordResetRequest(user, resetToken, resetUrl);

    return {ok: true, resetToken: resetToken, resetUrl: resetUrl};
}

function completePasswordReset(resetToken, newPassword) {
    init();
    if (newPassword.length < minPasswordLength)
        return {error: "password must be at least " + minPasswordLength + " characters"};
    var reset = lmdb.get("resets", resetToken);
    if (!reset)
        return {error: "invalid or expired reset token"};

    if (reset.expires < now()) {
        lmdb.del("resets", resetToken);
        return {error: "reset token has expired"};
    }

    var user = lmdb.get("users", reset.username);
    if (!user)
        return {error: "user not found"};

    user.passwordHash = crypto.passwd(newPassword).line;
    user.mustResetPassword = false;
    lmdb.put("users", reset.username, user);

    /* revoke all existing sessions */
    deleteAllSessions(reset.username);

    /* consume the reset token */
    lmdb.del("resets", resetToken);

    return {ok: true, username: reset.username};
}

/* ---- session management ---- */

/* ---- account lockout ---- */

function isLockedOut(username) {
    if (lockout.attempts <= 0) return false; /* disabled */
    var rec = lmdb.get("lockouts", username);
    if (!rec) return false;
    var ts = now();
    /* check if in active lockout */
    if (rec.lockedUntil && rec.lockedUntil > ts)
        return true;
    /* check if the window has expired — reset */
    if (rec.firstAttempt && (ts - rec.firstAttempt) > lockout.window) {
        lmdb.del("lockouts", username);
        return false;
    }
    return false;
}

function recordFailedAttempt(username) {
    if (lockout.attempts <= 0) return; /* disabled */
    var ts = now();
    var rec = lmdb.get("lockouts", username);
    if (!rec || (rec.firstAttempt && (ts - rec.firstAttempt) > lockout.window)) {
        /* start new window */
        rec = {attempts: 1, firstAttempt: ts};
    } else {
        rec.attempts++;
    }
    /* lock the account if threshold reached */
    if (rec.attempts >= lockout.attempts) {
        rec.lockedUntil = ts + lockout.duration;
    }
    lmdb.put("lockouts", username, rec);
}

function clearLockout(username) {
    try { lmdb.del("lockouts", username); } catch(e) {}
}

/* ---- email helpers ---- */

function sendAuthEmail(to, subject, htmlBody, textBody) {
    if (!emailConf)
        return {error: "email not configured"};

    var email = require("rampart-email");
    var opts = {
        from:    emailConf.from || "noreply@localhost",
        to:      to,
        subject: subject,
        message: {
            html: htmlBody,
            text: textBody || htmlBody.replace(/<[^>]*>/g, '')
        }
    };

    /* copy method-specific settings */
    if (emailConf.method)         opts.method = emailConf.method;
    if (emailConf.user)           opts.user = emailConf.user;
    if (emailConf.pass)           opts.pass = emailConf.pass;
    if (emailConf.smtpUrl)        opts.smtpUrl = emailConf.smtpUrl;
    if (emailConf.relay)          opts.relay = emailConf.relay;
    if (emailConf.relayPort)      opts.relayPort = emailConf.relayPort;
    if (emailConf.insecure)       opts.insecure = emailConf.insecure;
    if (emailConf.requireSsl)     opts.requireSsl = emailConf.requireSsl;
    if (emailConf.timeout)        opts.timeout = emailConf.timeout;
    if (emailConf.connectTimeout) opts.connectTimeout = emailConf.connectTimeout;

    try {
        var result = email.send(opts);
        if (!result.ok) {
            var errDetail = sprintf("%J", result);
            fprintf(stderr, "auth email error: %s\n", errDetail);
            return {error: "email send failed", detail: errDetail};
        }
        return result;
    } catch(e) {
        fprintf(stderr, "auth email exception: %s\n", e.message || e);
        return {error: "email send failed: " + e.message};
    }
}

/* ---- email verification ---- */

var emailCooldown = 60; /* seconds between emails to same user */

function checkEmailCooldown(key) {
    var rec = lmdb.get("lockouts", "email:" + key);
    if (rec && rec.lastSent && (now() - rec.lastSent) < emailCooldown)
        return false; /* too soon */
    return true;
}

function recordEmailSent(key) {
    lmdb.put("lockouts", "email:" + key, {lastSent: now()});
}

function sendVerificationEmail(username) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};
    if (!user.email)
        return {error: "user has no email address"};
    if (user.emailVerified)
        return {error: "email already verified"};
    if (!checkEmailCooldown(username))
        return {error: "please wait a few minutes before requesting another email"};

    var token = generateToken();
    lmdb.put("verifications", token, {
        username: username,
        email:    user.email,
        expires:  now() + 86400  /* 24 hours */
    });

    var verifyUrl = siteUrl + "/apps/auth/verify-email?token=" + sprintf("%U", token);

    var html = '<p>Please verify your email address by clicking the link below:</p>'
        + '<p><a href="' + sprintf("%H", verifyUrl) + '">Verify Email</a></p>'
        + '<p>This link expires in 24 hours.</p>'
        + '<p>If you did not create this account, you can ignore this email.</p>';

    var result = sendAuthEmail(user.email, "Verify your email address", html);
    if (result.error)
        return result;

    recordEmailSent(username);
    return {ok: true, token: token, verifyUrl: verifyUrl};
}

function verifyEmail(token) {
    init();
    var rec = lmdb.get("verifications", token);
    if (!rec)
        return {error: "invalid or expired verification token"};

    if (rec.expires < now()) {
        lmdb.del("verifications", token);
        return {error: "verification token has expired"};
    }

    var user = lmdb.get("users", rec.username);
    if (!user)
        return {error: "user not found"};

    user.emailVerified = true;
    lmdb.put("users", rec.username, user);

    /* consume the token */
    lmdb.del("verifications", token);

    return {ok: true, username: rec.username};
}

function register(opts) {
    init();
    if (!allowRegistration)
        return {error: "registration is not enabled"};

    if (!opts || !opts.username || !opts.password || !opts.email)
        return {error: "username, password, and email are required"};

    /* create the user */
    var createOpts = {
        username:  opts.username,
        password:  opts.password,
        email:     opts.email,
        name:      opts.name || opts.username,
        authLevel: opts.authLevel || 50, /* default to lowest privilege */
        emailVerified: !requireEmailVerification
    };

    var result = createUser(createOpts);
    if (result.error)
        return result;

    /* send verification email if required */
    if (requireEmailVerification) {
        var emailResult = sendVerificationEmail(opts.username);
        if (emailResult.error) {
            return {
                ok: true,
                username: result.username,
                emailSent: false,
                emailError: emailResult.error
            };
        }
        return {ok: true, username: result.username, emailSent: true};
    }

    return {ok: true, username: result.username, emailSent: false};
}

/* ---- password reset via email ---- */

function sendPasswordResetEmail(username) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};
    if (!user.email)
        return {error: "user has no email address"};
    if (!checkEmailCooldown(username))
        return {error: "please wait a few minutes before requesting another email"};

    var resetResult = requestPasswordReset(username);
    if (resetResult.error)
        return resetResult;

    var resetUrl = siteUrl + resetResult.resetUrl;

    var html = '<p>A password reset was requested for your account.</p>'
        + '<p><a href="' + sprintf("%H", resetUrl) + '">Reset Password</a></p>'
        + '<p>This link expires in 1 hour.</p>'
        + '<p>If you did not request this, you can ignore this email.</p>';

    var result = sendAuthEmail(user.email, "Password Reset", html);
    if (result.error)
        return result;

    recordEmailSent(username);
    return {ok: true, resetUrl: resetUrl};
}

/* ---- periodic cleanup of expired records ---- */

function cleanupExpired() {
    var ts = now();
    var databases = [
        {name: null, field: "expires"},           /* sessions */
        {name: "resets", field: "expires"},        /* reset tokens */
        {name: "verifications", field: "expires"}, /* verification tokens */
        {name: "lockouts", field: "lockedUntil"}   /* lockouts */
    ];

    var total = 0;
    for (var d = 0; d < databases.length; d++) {
        var db = databases[d];
        var all = lmdb.get(db.name, "", -1);
        if (!all) continue;
        for (var key in all) {
            var rec = all[key];
            /* for lockouts, clean if window expired and not currently locked */
            if (db.name === "lockouts") {
                if (rec.lockedUntil && rec.lockedUntil < ts) {
                    lmdb.del(db.name, key);
                    total++;
                } else if (rec.firstAttempt && (ts - rec.firstAttempt) > lockout.window * 2) {
                    lmdb.del(db.name, key);
                    total++;
                }
            } else if (rec[db.field] && rec[db.field] < ts) {
                lmdb.del(db.name, key);
                total++;
            }
        }
    }
    return total;
}

/* run cleanup roughly once per 20 logins */
function maybeCleanup() {
    if (Math.random() < 0.05)
        cleanupExpired();
}

function login(username, password, req) {
    init();
    username = username.toLowerCase().trim();

    /* check account lockout */
    if (isLockedOut(username))
        return {error: "account temporarily locked"};

    var user = lmdb.get("users", username);
    if (!user) {
        crypto.passwd("dummy"); /* constant-time: match the timing of a real check */
        recordFailedAttempt(username);
        return {error: "invalid credentials"};
    }

    if (user.authMethod !== "password")
        return {error: "this account uses " + user.authMethod + " authentication"};

    if (!crypto.passwdCheck(user.passwordHash, password)) {
        recordFailedAttempt(username);
        return {error: "invalid credentials"};
    }

    /* successful login — clear any lockout record */
    clearLockout(username);

    /* check email verification if required */
    if (requireEmailVerification && !user.emailVerified)
        return {error: "email not verified"};

    /* onLogin hook — can block or redirect (e.g., for 2FA) */
    if (hooks.onLogin) {
        var hookResult = hooks.onLogin(user, req || {});
        if (hookResult === false)
            return {error: "login denied by hook"};
        if (typeof hookResult === "object" && hookResult.redirect)
            return {redirect: hookResult.redirect, user: user};
    }

    /* create session */
    var token     = generateToken();
    var csrfToken = generateCsrfToken();
    /* build session from user record — include all custom properties */
    var session = {};
    var skipKeys = {passwordHash:1};
    for (var k in user) {
        if (!(k in skipKeys))
            session[k] = user[k];
    }
    /* add session-specific fields */
    session.csrfToken    = csrfToken;
    session.expires      = now() + (conf.sessionExpiry || 86400);
    session.lastRefresh  = now();
    session.created      = now();

    lmdb.put(null, token, session);

    /* onSessionCreated hook */
    if (hooks.onSessionCreated)
        hooks.onSessionCreated(user, session, req || {});

    /* periodic cleanup of expired records */
    maybeCleanup();

    var cookieName = conf.cookieName || "rp_session";
    var cookie = buildCookieString(cookieName, token, cookieFlags);

    return {
        ok: true,
        token: token,
        cookie: cookie,
        session: session,
        mustResetPassword: session.mustResetPassword
    };
}

/* build a display cookie from session data — client-readable, NOT HttpOnly */
function buildDisplayCookie(session) {
    if (!displayCookieFields) return null;
    var info = {};
    var fields = displayCookieFields.fields || ["name", "picture"];
    for (var i = 0; i < fields.length; i++) {
        var f = fields[i];
        if (session[f] !== undefined)
            info[f] = session[f];
    }
    var encoded = sprintf("%-0B", JSON.stringify(info));
    var cookieName = displayCookieFields.cookieName || "rp_user";
    return cookieName + "=" + encoded + "; Path=/; Max-Age=" + (cookieFlags.maxAge || 86400)
        + (cookieFlags.secure ? "; Secure" : "") + "; SameSite=Lax";
}

function clearDisplayCookie() {
    if (!displayCookieFields) return null;
    var cookieName = displayCookieFields.cookieName || "rp_user";
    return cookieName + "=; Path=/; Max-Age=0";
}

/* build the Set-Cookie array for a login response */
function buildLoginCookies(token, session) {
    var cookieName = conf.cookieName || "rp_session";
    var cookies = [buildCookieString(cookieName, token, cookieFlags)];
    var dc = buildDisplayCookie(session);
    if (dc) cookies.push(dc);
    return cookies;
}

/* create a session for an OAuth login — called by auth.js on behalf of plugins */
function createOAuthSession(userData) {
    init();
    if (!userData || !userData.username)
        return {error: "username is required"};

    var username = userData.username.toLowerCase().trim();
    var existing = getUser(username);

    if (!existing) {
        /* new user */
        var createOpts = {
            username:      username,
            password:      sprintf("%-0B", crypto.rand(32)),
            authLevel:     userData.authLevel || 50,
            emailVerified: true
        };
        /* copy all userData fields except username and password */
        for (var k in userData) {
            if (k !== "username" && k !== "password")
                createOpts[k] = userData[k];
        }
        var result = createUser(createOpts);
        if (result.error) return result;
    } else {
        /* update existing user with fresh data from provider */
        var updates = {};
        for (var k in userData) {
            if (k !== "username" && k !== "password")
                updates[k] = userData[k];
        }
        updateUser(username, updates);
    }

    /* create session */
    var user = getUser(username);
    var token = generateToken();
    var csrfToken = generateToken();
    var ts = now();

    var session = {};
    var skipKeys = {passwordHash: 1};
    for (var k in user) {
        if (!(k in skipKeys))
            session[k] = user[k];
    }
    session.csrfToken   = csrfToken;
    session.expires     = ts + (conf.sessionExpiry || 86400);
    session.lastRefresh = ts;
    session.created     = ts;

    lmdb.put(null, token, session);

    if (hooks.onSessionCreated)
        hooks.onSessionCreated(user, session, {});

    maybeCleanup();

    return {
        ok: true,
        token: token,
        session: session,
        cookies: buildLoginCookies(token, session)
    };
}

function logout(token, req) {
    init();
    if (!token) return {error: "no token"};

    var session = lmdb.get(null, token);

    /* onLogout hook */
    if (session && hooks.onLogout)
        hooks.onLogout(session, req || {});

    lmdb.del(null, token);

    var cookieName = conf.cookieName || "rp_session";
    var cookie = cookieName + "=; Path=" + (cookieFlags.path || "/") + "; Max-Age=0; HttpOnly";

    return {ok: true, cookie: cookie};
}

function refreshSessions(username) {
    init();
    username = username.toLowerCase().trim();
    var user = lmdb.get("users", username);
    if (!user)
        return {error: "user not found"};

    var skipKeys = {passwordHash:1};
    var all = lmdb.get(null, "", "*");
    var count = 0;

    if (all) {
        for (var token in all) {
            var s = all[token];
            if (s.username !== username) continue;
            if (s.expires && s.expires < now()) continue;

            /* update session with current user properties */
            for (var k in user) {
                if (!(k in skipKeys))
                    s[k] = user[k];
            }
            lmdb.put(null, token, s);
            count++;
        }
    }
    return {ok: true, updated: count};
}

function listSessions(username) {
    init();
    var all = lmdb.get(null, "", "*");
    var result = {};
    var ts = now();

    if (all) {
        for (var token in all) {
            var s = all[token];
            if (!s || !s.username) continue;  /* skip non-session entries */
            if (username && s.username !== username) continue;
            if (s.expires && s.expires < ts) continue; /* skip expired */
            result[token] = s;
        }
    }
    return result;
}

function deleteSession(token) {
    init();
    lmdb.del(null, token);
    return {ok: true};
}

function deleteAllSessions(username) {
    init();
    var all = lmdb.get(null, "", "*");
    var count = 0;
    if (all) {
        for (var token in all) {
            if (!username || all[token].username === username) {
                lmdb.del(null, token);
                count++;
            }
        }
    }
    return {ok: true, deleted: count};
}

/* ---- admin web interface ---- */

var defaultBeginHtml = '<!DOCTYPE html>\n<html><head><meta charset="utf-8">'
    + '<meta name="viewport" content="width=device-width, initial-scale=1">'
    + '<title>Rampart Auth</title>'
    + '<link rel="stylesheet" href="/css/auth.css">'
    + '</head><body>';
var defaultEndHtml = '</body></html>';

function wrapPage(title, content, extraClass) {
    var beginHtml = (conf.beginHtml !== undefined) ? conf.beginHtml : defaultBeginHtml;
    var endHtml   = (conf.endHtml !== undefined) ? conf.endHtml : defaultEndHtml;
    var cls = extraClass || 'auth-wrap';

    /* replace $title in beginHtml if present */
    if (beginHtml.indexOf('$title') >= 0)
        beginHtml = beginHtml.replace(/\$title/g, title);

    return beginHtml + '<div class="' + cls + '">' + content + '</div>' + endHtml;
}

var adminBase = "/apps/auth/admin";

function adminNav() {
    return '<div class="auth-nav">'
        + '<a href="' + adminBase + '/">Users</a>'
        + '<a href="' + adminBase + '/create-user">Create User</a>'
        + '<a href="' + adminBase + '/sessions">Sessions</a>'
        + '</div>';
}

function csrfField(req) {
    if (req.userAuth && req.userAuth.csrfToken)
        return '<input type="hidden" name="_csrf" value="' + sprintf("%H", req.userAuth.csrfToken) + '">';
    return '';
}

function msgDiv(msg, isError, rawHtml) {
    if (!msg) return '';
    return '<div class="auth-msg ' + (isError ? 'err' : 'ok') + '">'
        + (rawHtml ? msg : sprintf("%H", msg)) + '</div>';
}

function friendlyDate(ts) {
    var d = new Date(ts * 1000);
    var today = new Date();
    var yesterday = new Date(today);
    yesterday.setDate(yesterday.getDate() - 1);

    var h = d.getHours();
    var m = d.getMinutes();
    var ampm = h >= 12 ? 'pm' : 'am';
    h = h % 12;
    if (h === 0) h = 12;
    var timeStr = h + ':' + (m < 10 ? '0' : '') + m + ' ' + ampm;

    if (d.getFullYear() === today.getFullYear()
        && d.getMonth() === today.getMonth()
        && d.getDate() === today.getDate())
        return 'Today at ' + timeStr;

    if (d.getFullYear() === yesterday.getFullYear()
        && d.getMonth() === yesterday.getMonth()
        && d.getDate() === yesterday.getDate())
        return 'Yesterday at ' + timeStr;

    var tomorrow = new Date(today);
    tomorrow.setDate(tomorrow.getDate() + 1);
    if (d.getFullYear() === tomorrow.getFullYear()
        && d.getMonth() === tomorrow.getMonth()
        && d.getDate() === tomorrow.getDate())
        return 'Tomorrow at ' + timeStr;

    var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
                  'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var day = d.getDate();
    var suffix = 'th';
    if (day === 1 || day === 21 || day === 31) suffix = 'st';
    else if (day === 2 || day === 22) suffix = 'nd';
    else if (day === 3 || day === 23) suffix = 'rd';

    var str = months[d.getMonth()] + ' ' + day + suffix + ' at ' + timeStr;
    if (d.getFullYear() !== today.getFullYear())
        str = months[d.getMonth()] + ' ' + day + suffix + ', ' + d.getFullYear() + ' at ' + timeStr;
    return str;
}

/* admin page: user list */
function adminIndex(req) {
    init();
    var users = listUsers();
    var html = adminNav() + '<h2>Users</h2><table>'
        + '<tr><th>Username</th><th>Name</th><th>Email</th><th>Level</th><th>Method</th><th>Actions</th></tr>';

    for (var uname in users) {
        var u = users[uname];
        html += '<tr>'
            + '<td>' + sprintf("%H", u.username) + '</td>'
            + '<td>' + sprintf("%H", u.name || '') + '</td>'
            + '<td>' + sprintf("%H", u.email || '') + '</td>'
            + '<td>' + u.authLevel + '</td>'
            + '<td>' + sprintf("%H", u.authMethod || '') + '</td>'
            + '<td>'
            + '<a href="' + adminBase + '/edit-user?u=' + sprintf("%U", u.username) + '">Edit</a> '
            + '<a href="' + adminBase + '/delete-user?u=' + sprintf("%U", u.username) + '">Delete</a> '
            + (u.authMethod === 'password' || !u.authMethod
                ? '<a style="white-space:nowrap" href="' + adminBase + '/reset-pw?u=' + sprintf("%U", u.username) + '">Reset PW</a>'
                : '')
            + '</td></tr>';
    }
    html += '</table>';
    return {html: wrapPage("Auth Admin - Users", html)};
}

/* admin page: create user form */
function adminCreateUser(req) {
    init();
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        var result = createUser({
            username: p.username || '',
            password: p.password || '',
            name:     p.name || '',
            email:    p.email || '',
            authLevel: parseInt(p.authLevel) || 50
        });
        if (result.error) { msg = result.error; isError = true; }
        else { msg = "User '" + result.username + "' created."; }
    }

    var html = adminNav() + '<h2>Create User</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="' + adminBase + '/create-user">'
        + csrfField(req)
        + '<p>Username: <input type="text" name="username" required></p>'
        + '<p>Password: <input type="password" name="password" required></p>'
        + '<p>Name: <input type="text" name="name"></p>'
        + '<p>Email: <input type="email" name="email"></p>'
        + '<p>Auth Level: <input type="text" name="authLevel" value="50"></p>'
        + '<p><button type="submit">Create</button></p>'
        + '</form>';
    return {html: wrapPage("Auth Admin - Create User", html)};
}

/* admin page: edit user */
function adminEditUser(req) {
    init();
    var username = req.query ? req.query.u : '';
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        username = p.username || username;
        var updates = {};
        if (p.name !== undefined)  updates.name = p.name;
        if (p.email !== undefined) updates.email = p.email;
        if (p.authLevel !== undefined) updates.authLevel = parseInt(p.authLevel);
        var result = updateUser(username, updates);
        if (result.error) { msg = result.error; isError = true; }
        else { msg = "User updated."; }
    }

    var user = getUser(username);
    if (!user)
        return {html: wrapPage("Auth Admin - Edit User", adminNav() + '<p>User not found.</p>')};

    var html = adminNav() + '<h2>Edit User: ' + sprintf("%H", user.username) + '</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="' + adminBase + '/edit-user">'
        + csrfField(req)
        + '<input type="hidden" name="username" value="' + sprintf("%H", user.username) + '">'
        + '<p>Name: <input type="text" name="name" value="' + sprintf("%H", user.name || '') + '"></p>'
        + '<p>Email: <input type="email" name="email" value="' + sprintf("%H", user.email || '') + '"></p>'
        + '<p>Auth Level: <input type="text" name="authLevel" value="' + user.authLevel + '"></p>'
        + '<p><button type="submit">Save</button></p>'
        + '</form>';
    return {html: wrapPage("Auth Admin - Edit User", html)};
}

/* admin page: reset password */
function adminResetPw(req) {
    init();
    var username = req.query ? req.query.u : '';
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        username = p.username || username;
        var mustChange = (p.mustChange === "on" || p.mustChange === "true");
        var result = adminResetPassword(username, p.password, mustChange);
        if (result.error) { msg = result.error; isError = true; }
        else { msg = "Password reset." + (result.mustResetPassword ? " User must change on next login." : ""); }
    }

    var user = getUser(username);
    if (!user)
        return {html: wrapPage("Auth Admin - Reset Password", adminNav() + '<p>User not found.</p>')};

    var html = adminNav() + '<h2>Reset Password: ' + sprintf("%H", user.username) + '</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="' + adminBase + '/reset-pw">'
        + csrfField(req)
        + '<input type="hidden" name="username" value="' + sprintf("%H", user.username) + '">'
        + '<p>New Password: <input type="password" name="password" required></p>'
        + '<p><label><input type="checkbox" name="mustChange" checked> Require change on next login</label></p>'
        + '<p><button type="submit">Reset Password</button></p>'
        + '</form>';
    return {html: wrapPage("Auth Admin - Reset Password", html)};
}

/* admin page: delete user confirmation */
function adminDeleteUser(req) {
    init();
    var username = req.query ? req.query.u : '';
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        username = p.username || username;
        if (p.confirm === "yes") {
            var result = deleteUser(username);
            if (result.error) { msg = result.error; isError = true; }
            else { return {status: 302, headers: {"location": adminBase + "/"}}; }
        }
    }

    var user = getUser(username);
    if (!user)
        return {html: wrapPage("Auth Admin - Delete User", adminNav() + '<p>User not found.</p>')};

    var html = adminNav() + '<h2>Delete User: ' + sprintf("%H", user.username) + '</h2>'
        + msgDiv(msg, isError)
        + '<p>Are you sure you want to delete this user and all their sessions?</p>'
        + '<form method="POST" action="' + adminBase + '/delete-user">'
        + csrfField(req)
        + '<input type="hidden" name="username" value="' + sprintf("%H", user.username) + '">'
        + '<input type="hidden" name="confirm" value="yes">'
        + '<p><button type="submit" style="color:red;">Delete User</button>'
        + ' <a href="' + adminBase + '/">Cancel</a></p>'
        + '</form>';
    return {html: wrapPage("Auth Admin - Delete User", html)};
}

/* admin page: session list */
function adminSessions(req) {
    init();
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        if (p.action === "delete" && p.token) {
            deleteSession(p.token);
            msg = "Session deleted.";
        } else if (p.action === "deleteAll" && p.username) {
            var r = deleteAllSessions(p.username);
            msg = r.deleted + " session(s) deleted for " + p.username + ".";
        }
    }

    var sessions = listSessions();
    var html = adminNav() + '<h2>Active Sessions</h2>'
        + msgDiv(msg, isError);

    html += '<table><tr><th>User</th><th>Created</th><th>Expires</th><th>Method</th><th>Actions</th></tr>';
    for (var token in sessions) {
        var s = sessions[token];
        var created = '<span style="white-space:nowrap">' + (s.created ? friendlyDate(s.created) : '?') + '</span>';
        var expires = '<span style="white-space:nowrap">' + (s.expires ? friendlyDate(s.expires) : '?') + '</span>';
        html += '<tr>'
            + '<td>' + sprintf("%H", s.username || '?') + '</td>'
            + '<td>' + created + '</td>'
            + '<td>' + expires + '</td>'
            + '<td>' + sprintf("%H", s.authMethod || '?') + '</td>'
            + '<td>'
            + '<form method="POST" action="' + adminBase + '/sessions" style="display:inline;">'
            + csrfField(req)
            + '<input type="hidden" name="action" value="delete">'
            + '<input type="hidden" name="token" value="' + sprintf("%H", token) + '">'
            + '<button type="submit">Revoke</button>'
            + '</form>'
            + '</td></tr>';
    }
    html += '</table>';
    return {html: wrapPage("Auth Admin - Sessions", html)};
}

/* login handler (POST) */
/* registration page */
function registerHandler(req) {
    init();
    if (!allowRegistration)
        return {status: 404, html: wrapPage("Not Found", "<h2>Registration is not enabled</h2>")};

    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        var result = register({
            username: p.username || '',
            password: p.password || '',
            email:    p.email || '',
            name:     p.name || ''
        });

        if (result.error) {
            msg = result.error; isError = true;
        } else if (requireEmailVerification) {
            if (result.emailSent)
                msg = 'Account created. Please check your email to verify your address. '
                    + '<a href="/apps/auth/resend-verification">Resend verification email</a>';
            else
                msg = 'Account created but verification email could not be sent. '
                    + '<a href="/apps/auth/resend-verification">Try resending</a>';
            isError = !result.emailSent;
        } else {
            return {status: 302, headers: {"location": "/apps/auth/login"}};
        }
    }

    var hasLink = msg && msg.indexOf('<a ') >= 0;
    var html = '<h2>Create Account</h2>'
        + msgDiv(msg, isError, hasLink)
        + '<form method="POST" action="/apps/auth/register">'
        + '<p>Username: <input type="text" name="username" required></p>'
        + '<p>Email: <input type="email" name="email" required></p>'
        + '<p>Name: <input type="text" name="name"></p>'
        + '<p>Password: <input type="password" name="password" required></p>'
        + '<p><button type="submit">Register</button></p>'
        + '</form>'
        + '<p>Already have an account? <a href="/apps/auth/login">Log in</a></p>';
    return {html: wrapPage("Register", html)};
}

/* email verification endpoint */
function verifyEmailHandler(req) {
    init();
    var token = req.query ? req.query.token : '';

    if (!token)
        return {html: wrapPage("Email Verification", "<h2>Verification</h2><p>No token provided.</p>")};

    var result = verifyEmail(token);

    if (result.error)
        return {html: wrapPage("Email Verification",
            "<h2>Verification Failed</h2><p>" + sprintf("%H", result.error) + "</p>")};

    return {html: wrapPage("Email Verification",
        "<h2>Email Verified</h2>"
        + "<p>Your email has been verified. You can now <a href=\"/apps/auth/login\">log in</a>.</p>")};
}

/* resend verification email */
function resendVerificationHandler(req) {
    init();
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        var input = (p.username || '').trim();
        if (input) {
            /* try as username first, then search by email */
            var user = getUser(input);
            if (!user) {
                /* search users by email */
                var allUsers = listUsers();
                for (var uname in allUsers) {
                    if (allUsers[uname].email && allUsers[uname].email.toLowerCase() === input.toLowerCase()) {
                        user = allUsers[uname];
                        input = uname;
                        break;
                    }
                }
            }
            if (user) {
                var result = sendVerificationEmail(input);
                if (result.error) { msg = result.error; isError = true; }
                else { msg = "Verification email sent. Please check your inbox."; }
            } else {
                msg = "Account not found."; isError = true;
            }
        } else {
            msg = "Username or email is required."; isError = true;
        }
    }

    var html = '<h2>Resend Verification</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="/apps/auth/resend-verification">'
        + '<p>Username or email: <input type="text" name="username" required></p>'
        + '<p><button type="submit">Resend</button></p>'
        + '</form>';
    return {html: wrapPage("Resend Verification", html)};
}

/* request password reset via email */
function requestResetHandler(req) {
    init();
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        var username = p.username || '';
        if (username) {
            var result = sendPasswordResetEmail(username);
            if (result.error) {
                /* don't reveal whether user exists — generic message */
                msg = "If an account with that username exists, a reset email has been sent.";
            } else {
                msg = "If an account with that username exists, a reset email has been sent.";
            }
        } else {
            msg = "Username is required."; isError = true;
        }
    }

    var html = '<h2>Reset Password</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="/apps/auth/request-reset">'
        + '<p>Username: <input type="text" name="username" required></p>'
        + '<p><button type="submit">Send Reset Email</button></p>'
        + '</form>'
        + '<p><a href="/apps/auth/login">Back to login</a></p>';
    return {html: wrapPage("Reset Password", html)};
}

function loginHandler(req) {
    init();
    if (req.method !== "POST" || !req.postData || !req.postData.content)
    {
        /* GET: show login form */
        var error = req.query ? req.query.error : '';
        var returnTo = req.query ? req.query.returnTo : '/';
        var messages = {
            invalid:    'Invalid username or password.',
            locked:     'Account temporarily locked. Try again later.',
            unverified: 'Email not verified. Check your inbox.<br><a href="/apps/auth/resend-verification">Resend verification email</a>'
        };
        var msg = messages[error] || '';

        var html = '<div class="auth-card">'
            + '<h2>Login</h2>'
            + (msg ? msgDiv(msg, true, msg.indexOf('<a ') >= 0) : '')
            + '<form method="POST" action="/apps/auth/login">'
            + '<input type="hidden" name="returnTo" value="' + sprintf("%H", safeReturnTo(returnTo)) + '">'
            + '<label for="username">Username</label>'
            + '<input type="text" name="username" id="username" required autofocus>'
            + '<label for="password">Password</label>'
            + '<input type="password" name="password" id="password" required>'
            + '<p><button type="submit">Log In</button></p>'
            + '</form>'
            + '<div class="auth-links">'
            + '<a href="/apps/auth/request-reset">Forgot password?</a>'
            + (allowRegistration ? '<a href="/apps/auth/register">Create account</a>' : '')
            + '</div>';

        /* add OAuth buttons for loaded plugins */
        if (loginProviders.length > 0) {
            html += '<div class="auth-oauth">'
                + '<p class="auth-oauth-label">Or sign in with:</p>';
            for (var oi = 0; oi < loginProviders.length; oi++) {
                var lp = loginProviders[oi];
                html += '<a href="/apps/auth' + sprintf("%H", lp.startPath)
                    + '?returnTo=' + sprintf("%U", safeReturnTo(returnTo))
                    + '" class="auth-oauth-btn">';
                if (lp.icon)
                    html += '<img src="' + sprintf("%H", lp.icon) + '" alt="">';
                html += sprintf("%H", lp.label) + '</a>';
            }
            html += '</div>';
        }

        html += '</div>';
        return {html: wrapPage("Login", html, "auth-login-wrap")};
    }

    var p = req.postData.content;
    var result = login(p.username || '', p.password || '', req);

    if (result.error) {
        var returnTo = safeReturnTo(p.returnTo);
        var errCode = "invalid";
        if (result.error === "account temporarily locked") errCode = "locked";
        else if (result.error === "email not verified") errCode = "unverified";
        return {status: 302, headers: {"location": "/apps/auth/login?error=" + errCode + "&returnTo=" + sprintf("%U", returnTo)}};
    }

    if (result.redirect) {
        return {
            status: 302,
            headers: {
                "location": result.redirect,
                "Set-Cookie": buildLoginCookies(result.token, result.session)
            }
        };
    }

    var redirectTo = safeReturnTo(p.returnTo);
    if (result.mustResetPassword)
        redirectTo = "/apps/auth/force-reset";

    return {
        status: 302,
        headers: {
            "location": redirectTo,
            "Set-Cookie": buildLoginCookies(result.token, result.session)
        }
    };
}

/* logout handler */
function logoutHandler(req) {
    init();
    var cookieName = conf.cookieName || "rp_session";
    var token = req.cookies ? req.cookies[cookieName] : null;
    var result = logout(token, req);

    var cookies = [result.cookie];
    var dc = clearDisplayCookie();
    if (dc) cookies.push(dc);

    return {
        status: 302,
        headers: {
            "location": "/",
            "Set-Cookie": cookies
        }
    };
}

/* force password reset page */
function forceResetHandler(req) {
    init();
    var cookieName = conf.cookieName || "rp_session";
    var token = req.cookies ? req.cookies[cookieName] : null;
    var msg = '', isError = false;

    if (!token || !req.userAuth)
        return {status: 302, headers: {"location": "/apps/auth/login"}};

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        if (p.newPassword && p.newPassword === p.confirmPassword) {
            var result = changePassword(req.userAuth.username, p.oldPassword, p.newPassword);
            if (result.error) {
                msg = result.error; isError = true;
            } else {
                return {status: 302, headers: {"location": "/"}};
            }
        } else {
            msg = "Passwords do not match."; isError = true;
        }
    }

    var html = '<h2>Password Change Required</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="/apps/auth/force-reset">'
        + csrfField(req)
        + '<p>Current Password: <input type="password" name="oldPassword" required></p>'
        + '<p>New Password: <input type="password" name="newPassword" required></p>'
        + '<p>Confirm: <input type="password" name="confirmPassword" required></p>'
        + '<p><button type="submit">Change Password</button></p>'
        + '</form>';
    return {html: wrapPage("Change Password", html)};
}

/* password reset via token (from email link) */
function doResetHandler(req) {
    init();
    var resetToken = req.query ? req.query.token : '';
    var msg = '', isError = false;

    if (req.method === "POST" && req.postData && req.postData.content) {
        var p = req.postData.content;
        resetToken = p.token || resetToken;
        if (p.newPassword && p.newPassword === p.confirmPassword) {
            var result = completePasswordReset(resetToken, p.newPassword);
            if (result.error) { msg = result.error; isError = true; }
            else {
                return {html: wrapPage("Password Reset",
                    '<h2>Password Reset</h2><p>Your password has been reset. <a href="/apps/auth/login">Log in</a></p>')};
            }
        } else {
            msg = "Passwords do not match."; isError = true;
        }
    }

    /* verify token is valid before showing form */
    var reset = lmdb.get("resets", resetToken);
    if (!reset || reset.expires < now()) {
        return {html: wrapPage("Password Reset",
            '<h2>Password Reset</h2><p>Invalid or expired reset link.</p>')};
    }

    var html = '<h2>Reset Password</h2>'
        + msgDiv(msg, isError)
        + '<form method="POST" action="/apps/auth/do-reset">'
        + '<input type="hidden" name="token" value="' + sprintf("%H", resetToken) + '">'
        + '<p>New Password: <input type="password" name="newPassword" required></p>'
        + '<p>Confirm: <input type="password" name="confirmPassword" required></p>'
        + '<p><button type="submit">Reset Password</button></p>'
        + '</form>';
    return {html: wrapPage("Password Reset", html)};
}

/* ---- module exports ---- */

/* ---- CLI admin tool ---- */

function cliAdmin() {
    var args = process.argv.slice(2);

    function readLine_() {
        var val = readLine(stdin).next();
        if (val === undefined || val === null) {
            printf("\n");
            process.exit(1);
        }
        return trim(val);
    }

    function readPassword(prompt) {
        printf('%s', prompt);
        stdout.fflush();
        var pw = trim(fgets(stdin, {echo: false}, 4096));
        printf('\n');
        return pw;
    }

    function usage() {
        printf("Rampart Auth Administration Tool\n\n");
        printf("Usage: rampart %s <command> [args]\n\n", process.argv[1]);
        printf("Commands:\n");
        printf("  add    <username> <password> [level]   Create a new user (default level: 50)\n");
        printf("  del    <username>                      Delete a user and their sessions\n");
        printf("  list                                   List all users\n");
        printf("  passwd <username> <password>            Change password\n");
        printf("  reset  <username> <password>            Reset password (force change on login)\n");
        printf("  level  <username> <level>               Set auth level\n");
        printf("  sessions [username]                     List active sessions\n");
        printf("  revoke <username>                       Revoke all sessions for user\n");
        printf("  cleanup                                 Remove expired sessions and tokens\n");
        printf("  setup                                   Interactive initial setup\n");
        process.exit(0);
    }

    init();

    /* interactive setup if no users exist or 'setup' command */
    function interactiveSetup() {
        printf("\n");
        printf("===========================================\n");
        printf("   Rampart Auth — Initial Setup\n");
        printf("===========================================\n");
        printf("\nCreate an administrator account.\n\n");

        var username;
        while (true) {
            printf("Admin username: ");
            stdout.fflush();
            username = readLine_().toLowerCase();
            if (!username) {
                printf("Username cannot be empty.\n");
                continue;
            }
            if (username.length < 3 || username.length > 64) {
                printf("Username must be 3-64 characters.\n");
                continue;
            }
            break;
        }

        var password;
        while (true) {
            password = readPassword("Password: ");
            if (password.length < 4) {
                printf("Password must be at least 4 characters.\n");
                continue;
            }
            var confirm = readPassword("Confirm password: ");
            if (password !== confirm) {
                printf("Passwords do not match. Try again.\n");
                continue;
            }
            break;
        }

        printf("Email (optional): ");
        stdout.fflush();
        var email = readLine_();

        var result = createUser({
            username:  username,
            password:  password,
            name:      username,
            email:     email || "",
            authLevel: 0
        });

        if (result.error) {
            printf("Error: %s\n", result.error);
            process.exit(1);
        }

        printf("\nAdmin user '%s' created.\n", username);
        printf("You can now start the web server and log in.\n\n");
    }

    /* check for no users — auto-setup */
    var allUsers = listUsers();
    var hasUsers = allUsers && typeof allUsers === 'object' && Object.keys(allUsers).length > 0;

    if (!hasUsers && (!args[0] || args[0] === 'setup')) {
        interactiveSetup();
        return;
    }

    if (!args[0]) {
        usage();
        return;
    }

    var command = args[0];
    switch (command) {
        case 'setup':
            interactiveSetup();
            break;

        case 'add': {
            var u = args[1], p = args[2], l = (args[3] !== undefined) ? parseInt(args[3]) : 50;
            if (!u || !p) { printf("Usage: add <username> <password> [level]\n"); process.exit(1); }
            var r = createUser({username: u, password: p, authLevel: l});
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("User '%s' created (level %d).\n", r.username, l);
            break;
        }

        case 'del': {
            if (!args[1]) { printf("Usage: del <username>\n"); process.exit(1); }
            var r = deleteUser(args[1]);
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("User '%s' deleted.\n", args[1]);
            break;
        }

        case 'list': {
            var users = listUsers();
            if (!users || Object.keys(users).length === 0) {
                printf("No users found.\n");
                break;
            }
            printf("%-20s %-6s %-30s %-24s\n", "USERNAME", "LEVEL", "EMAIL", "CREATED");
            printf("%-20s %-6s %-30s %-24s\n", "--------", "-----", "-----", "-------");
            for (var uname in users) {
                var u = users[uname];
                var created = u.created ? new Date(u.created * 1000).toISOString().substring(0,19) : 'unknown';
                printf("%-20s %-6d %-30s %-24s\n", u.username, u.authLevel, u.email || '', created);
            }
            printf("\nTotal: %d user(s)\n", Object.keys(users).length);
            break;
        }

        case 'passwd': {
            if (!args[1] || !args[2]) { printf("Usage: passwd <username> <password>\n"); process.exit(1); }
            var u = getUser(args[1]);
            if (!u) { printf("Error: user not found\n"); process.exit(1); }
            var r = updateUser(args[1], {password: args[2], emailVerified: true});
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("Password changed for '%s'.\n", args[1]);
            break;
        }

        case 'reset': {
            if (!args[1] || !args[2]) { printf("Usage: reset <username> <password>\n"); process.exit(1); }
            var r = adminResetPassword(args[1], args[2], true);
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("Password reset for '%s'. Must change on next login.\n", args[1]);
            break;
        }

        case 'level': {
            if (!args[1] || args[2] === undefined) { printf("Usage: level <username> <level>\n"); process.exit(1); }
            var r = updateUser(args[1], {authLevel: parseInt(args[2])});
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("Auth level for '%s' set to %d.\n", args[1], parseInt(args[2]));
            break;
        }

        case 'sessions': {
            var sessions = listSessions(args[1] || null);
            var keys = Object.keys(sessions);
            if (keys.length === 0) {
                printf("No active sessions%s.\n", args[1] ? " for " + args[1] : "");
                break;
            }
            printf("%-20s %-24s %-24s %-10s\n", "USER", "CREATED", "EXPIRES", "METHOD");
            printf("%-20s %-24s %-24s %-10s\n", "----", "-------", "-------", "------");
            for (var i = 0; i < keys.length; i++) {
                var s = sessions[keys[i]];
                var created = s.created ? new Date(s.created * 1000).toISOString().substring(0,19) : '?';
                var expires = s.expires ? new Date(s.expires * 1000).toISOString().substring(0,19) : '?';
                printf("%-20s %-24s %-24s %-10s\n", s.username || '?', created, expires, s.authMethod || '?');
            }
            printf("\nTotal: %d session(s)\n", keys.length);
            break;
        }

        case 'revoke': {
            if (!args[1]) { printf("Usage: revoke <username>\n"); process.exit(1); }
            var r = deleteAllSessions(args[1]);
            if (r.error) { printf("Error: %s\n", r.error); process.exit(1); }
            printf("Revoked %d session(s) for '%s'.\n", r.deleted, args[1]);
            break;
        }

        case 'cleanup': {
            var n = cleanupExpired();
            printf("Removed %d expired record(s).\n", n);
            break;
        }

        default:
            usage();
    }
}

/* ---- plugin helpers (exposed to plugins) ---- */

function getDbPath() { init(); return dbPath; }
function getCookieName() { init(); return conf.cookieName || "rp_session"; }

/* ---- plugin loading ---- */

var loadedPlugins = [];  /* [{name, endpoints}, ...] */

function loadPlugins() {
    var pluginDir = process.scriptPath + "/apps/auth-plugins";
    var files;

    try {
        files = readdir(pluginDir);
    } catch(e) {
        /* no plugin directory — that's fine */
        return;
    }

    /* the API object passed to plugins */
    var api = {
        init:             init,
        createUser:       createUser,
        getUser:          getUser,
        updateUser:       updateUser,
        listUsers:        listUsers,
        deleteUser:       deleteUser,
        login:            login,
        logout:           logout,
        generateToken:    generateToken,
        getDbPath:        getDbPath,
        getCookieName:    getCookieName,
        refreshSessions:  refreshSessions,
        createOAuthSession: createOAuthSession
    };

    for (var i = 0; i < files.length; i++) {
        var f = files[i];
        if (!/\.js$/.test(f)) continue;

        var pluginPath = pluginDir + "/" + f;
        try {
            var plugin = require(pluginPath);

            if (!plugin.name || !plugin.endpoints || !plugin.init) {
                fprintf(stderr, "auth-plugin %s: missing name, endpoints, or init — skipped\n", f);
                continue;
            }

            if (typeof plugin.init !== "function") {
                fprintf(stderr, "auth-plugin %s: init is not a function — skipped\n", f);
                continue;
            }

            if (typeof plugin.endpoints !== "object") {
                fprintf(stderr, "auth-plugin %s: endpoints is not an object — skipped\n", f);
                continue;
            }

            /* initialize plugin with config and API */
            init(); /* ensure conf is loaded */
            var ok = plugin.init(conf, api);

            if (!ok) {
                /* plugin chose not to activate (e.g., config not present) */
                continue;
            }

            loadedPlugins.push({
                name: plugin.name,
                endpoints: plugin.endpoints
            });

            fprintf(stderr, "auth-plugin loaded: %s (%d endpoints)\n",
                    plugin.name, Object.keys(plugin.endpoints).length);

        } catch(e) {
            fprintf(stderr, "auth-plugin %s: failed to load — %s\n", f, e.message || e);
        }
    }
}

/* load plugins at module load time (outside callbacks) */
loadPlugins();

/* ---- build login provider list for the login page ---- */

var loginProviders = [];

function buildLoginProviders() {
    if (!conf || !conf.oauth) return;

    var knownIcons = {
        google:    "https://www.google.com/favicon.ico",
        facebook:  "https://www.facebook.com/favicon.ico",
        github:    "https://github.com/favicon.ico",
        twitter:   "https://abs.twimg.com/favicons/twitter.3.ico",
        microsoft: "https://www.microsoft.com/favicon.ico",
        linkedin:  "https://www.linkedin.com/favicon.ico",
        discord:   "https://discord.com/favicon.ico",
        gitlab:    "https://gitlab.com/favicon.ico",
        apple:     "https://www.apple.com/favicon.ico",
        slack:     "https://slack.com/favicon.ico"
    };

    var knownLabels = {
        google: "Google", facebook: "Facebook", github: "GitHub",
        twitter: "X", microsoft: "Microsoft", linkedin: "LinkedIn",
        discord: "Discord", gitlab: "GitLab", apple: "Apple",
        slack: "Slack"
    };

    for (var pi = 0; pi < loadedPlugins.length; pi++) {
        var ep = loadedPlugins[pi].endpoints;
        for (var path in ep) {
            if (path.indexOf("callback") >= 0) continue;
            if (path.indexOf("/oauth/") !== 0) continue;

            var provName = path.replace("/oauth/", "");
            var provConf = conf.oauth[provName] || {};

            /* determine icon URL: config > known > favicon from authorizeUrl */
            var icon = provConf.icon || null;
            if (!icon && knownIcons[provName])
                icon = knownIcons[provName];
            if (!icon && provConf.authorizeUrl) {
                var match = provConf.authorizeUrl.match(/^https?:\/\/([^\/]+)/);
                if (match)
                    icon = "https://" + match[1] + "/favicon.ico";
            }

            var label = provConf.label || knownLabels[provName]
                || (provName.charAt(0).toUpperCase() + provName.slice(1));

            loginProviders.push({
                name:      provName,
                label:     label,
                icon:      icon,
                startPath: path
            });
        }
    }
}

buildLoginProviders();

/* ---- module detection: server or CLI ---- */

if (module && module.exports) {
    /* server mode — export HTTP endpoints and programmatic API */
    var exports_obj = {
        /* HTTP endpoints (server path-mapped) */

        /* public endpoints (no auth required) */
        "/login":                   loginHandler,
        "/logout":                  logoutHandler,
        "/register":                registerHandler,
        "/verify-email":            verifyEmailHandler,
        "/resend-verification":     resendVerificationHandler,
        "/request-reset":           requestResetHandler,
        "/do-reset":                doResetHandler,

        /* requires authenticated session */
        "/force-reset":             forceResetHandler,

        /* admin endpoints (protected at level 0 via auth-conf.js protectedPaths) */
        "/admin/":                  adminIndex,
        "/admin/index.html":        adminIndex,
        "/admin/create-user":       adminCreateUser,
        "/admin/edit-user":         adminEditUser,
        "/admin/reset-pw":          adminResetPw,
        "/admin/delete-user":       adminDeleteUser,
        "/admin/sessions":          adminSessions,

        /* Programmatic API (used via require()) */
        init:                   init,
        login:                  login,
        logout:                 logout,
        createUser:             createUser,
        getUser:                getUser,
        listUsers:              listUsers,
        updateUser:             updateUser,
        deleteUser:             deleteUser,
        changePassword:         changePassword,
        adminResetPassword:     adminResetPassword,
        requestPasswordReset:   requestPasswordReset,
        completePasswordReset:  completePasswordReset,
        listSessions:           listSessions,
        deleteSession:          deleteSession,
        deleteAllSessions:      deleteAllSessions,
        refreshSessions:        refreshSessions,
        register:               register,
        sendVerificationEmail:  sendVerificationEmail,
        verifyEmail:            verifyEmail,
        sendPasswordResetEmail: sendPasswordResetEmail,
        sendAuthEmail:          sendAuthEmail,
        cleanupExpired:         cleanupExpired,
        generateToken:          generateToken,
        createOAuthSession:     createOAuthSession,
        getDbPath:              getDbPath,
        getCookieName:          getCookieName,
        loadedPlugins:          loadedPlugins
    };

    /* merge plugin endpoints into exports.
       Callback endpoints (containing "callback") are wrapped: the plugin
       returns {ok, username, returnTo, userData} and auth.js creates
       the session and handles the redirect with all cookies. */
    for (var pi = 0; pi < loadedPlugins.length; pi++) {
        var ep = loadedPlugins[pi].endpoints;
        for (var path in ep) {
            if (path.indexOf("callback") >= 0) {
                /* wrap callback endpoint */
                exports_obj[path] = (function(handler) {
                    return function(req) {
                        var result = handler(req);

                        /* if plugin returned an HTTP response (error/redirect), pass through */
                        if (result && result.status)
                            return result;

                        /* plugin returned auth data — create session */
                        if (!result || !result.ok)
                            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};

                        var sess = createOAuthSession(result.userData);
                        if (sess.error) {
                            fprintf(stderr, "auth plugin session error: %s\n", sess.error);
                            return {status: 302, headers: {"location": "/apps/auth/login?error=invalid"}};
                        }

                        var returnTo = safeReturnTo(result.returnTo || "/");
                        return {
                            status: 302,
                            headers: {
                                "location": returnTo,
                                "Set-Cookie": sess.cookies
                            }
                        };
                    };
                })(ep[path]);
            } else {
                /* start endpoints pass through directly */
                exports_obj[path] = ep[path];
            }
        }
    }

    module.exports = exports_obj;
} else {
    /* CLI mode — run admin tool */
    cliAdmin();
}
