/* Shared helpers for the Mini IDE apps and wsapps:
   settings, single-user session cookies and path resolution.  */

var crypto = require("rampart-crypto");
var u = rampart.utils;

var ideRoot  = u.realPath(module.path + '/..');
var siteRoot = u.realPath(ideRoot + '/..');   // the web server root, the IDE's home directory
var settingsFile = ideRoot + '/data/ide-settings.json';
var sessionHours = 24;

var cache = null, cacheMtime = -1;

/* settings are re-read when the file changes (e.g. password reset via setup) */
function settings() {
    var st = u.stat(settingsFile);
    if (!st) return null;
    var mt = st.mtime.getTime();
    if (!cache || mt != cacheMtime) {
        try {
            cache = JSON.parse(u.readFile(settingsFile, {returnString: true}));
            cacheMtime = mt;
        } catch (e) {
            return null;
        }
    }
    return cache;
}

function saveSettings(s) {
    u.fprintf(settingsFile, '%3J\n', s);
    u.chmod(settingsFile, "600");
    cache = null;
}

function newSecret() {
    return u.sprintf('%-0B', crypto.rand(32));
}

function cookieName(s) {
    return 'mini_ide_' + (s.port || 0);
}

function sign(s, exp) {
    return crypto.hmac(s.secret, 'mini-ide:' + exp);
}

function header(req, name) {
    var h = req.headers || {}, lname = name.toLowerCase();
    for (var k in h)
        if (k.toLowerCase() == lname) return h[k];
    return undefined;
}

/* every value sent for our cookie name.  A browser may hold two (e.g. a stale
   one with another Path), and req.cookies keeps only one of them.            */
function cookieValues(req, name) {
    var raw = header(req, 'Cookie'), vals = [];
    if (raw) {
        var parts = String(raw).split(';');
        for (var i = 0; i < parts.length; i++) {
            var kv = parts[i].replace(/^\s+|\s+$/g, '');
            var eq = kv.indexOf('=');
            if (eq > 0 && kv.substring(0, eq) == name) vals.push(kv.substring(eq + 1));
        }
    }
    if (!vals.length && req.cookies && typeof req.cookies[name] == 'string')
        vals.push(req.cookies[name]);
    return vals;
}

function validToken(s, c) {
    var i = c.indexOf('.');
    if (i < 1) return false;
    var exp = parseInt(c.substring(0, i));
    var mac = c.substring(i + 1);
    if (isNaN(exp) || exp < Date.now()) return false;
    var good = sign(s, exp);
    if (good.length != mac.length) return false;
    return crypto.timingSafeEqual(good, mac);
}

/* returns true if the request carries a valid session cookie */
function checkSession(req) {
    var s = settings();
    if (!s || !s.secret) return false;
    var vals = cookieValues(req, cookieName(s));
    for (var i = 0; i < vals.length; i++)
        if (validToken(s, vals[i])) return true;
    return false;
}

/* Set-Cookie values; each also expires a copy left at Path=/ by older versions */
function loginCookie() {
    var s = settings();
    var exp = Date.now() + sessionHours * 3600000;
    return [
        u.sprintf('%s=%d.%s; Max-Age=%d; Path=/ide/; HttpOnly; SameSite=Strict',
                  cookieName(s), exp, sign(s, exp), sessionHours * 3600),
        u.sprintf('%s=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict', cookieName(s))
    ];
}

function logoutCookie() {
    var s = settings() || {};
    return [
        u.sprintf('%s=; Max-Age=0; Path=/ide/; HttpOnly; SameSite=Strict', cookieName(s)),
        u.sprintf('%s=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict', cookieName(s))
    ];
}

function checkPassword(pass) {
    var s = settings();
    if (!s || !s.passwordHash) return false;
    return crypto.passwdCheck(s.passwordHash, String(pass));
}

/* invalidate every outstanding session cookie */
function rotateSecret() {
    var s = settings();
    if (!s) return;
    s = JSON.parse(JSON.stringify(s));
    s.secret = newSecret();
    saveSettings(s);
}

/* Browser requests from another site must not reach the API or terminals. */
function originOk(req) {
    var origin = header(req, 'Origin');
    if (!origin) return true;
    var host = header(req, 'Host');
    return !!host && origin.replace(/^[a-z]+:\/\//i, '') == host;
}

/* Resolve a path relative to the web server root.  Returns {rel, abs}.
   Throws if the path would climb above the root.  */
function resolve(rel) {
    var parts = String(rel || '').replace(/\\/g, '/').split('/'), out = [];
    for (var i = 0; i < parts.length; i++) {
        var p = parts[i];
        if (!p || p == '.') continue;
        if (p == '..') {
            if (!out.length) throw new Error("Path is outside of the server root");
            out.pop();
        } else
            out.push(p);
    }
    var r = out.join('/');
    return {rel: r, abs: r.length ? siteRoot + '/' + r : siteRoot};
}

module.exports = {
    ideRoot:      ideRoot,
    siteRoot:     siteRoot,
    settings:     settings,
    saveSettings: saveSettings,
    newSecret:    newSecret,
    header:       header,
    checkSession: checkSession,
    loginCookie:  loginCookie,
    logoutCookie: logoutCookie,
    checkPassword:checkPassword,
    rotateSecret: rotateSecret,
    originOk:     originOk,
    resolve:      resolve
};
