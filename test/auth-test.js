/* rampart-auth module test */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");
var Lmdb = require("rampart-lmdb");
var pid;

var tmpdir = process.scriptPath + '/tmp-auth-test';
var dbpath = tmpdir + '/auth-db';
var htmldir = tmpdir + '/html';
var appsdir = tmpdir + '/apps';
var logsdir = tmpdir + '/logs';
var confpath = tmpdir + '/auth-conf.js';
var port = 8095;
var base = 'http://127.0.0.1:' + port;

/* ---- setup ---- */

function kill_server(p) {
    if (!p || !kill(p, 0)) return;
    kill(p, 15);
    sleep(0.5);
    if (!kill(p, 0)) return;
    kill(p, 9);
    sleep(0.5);
    if (!kill(p, 0)) return;
    fprintf(stderr, "WARNING: process %d could not be terminated\n", p);
}

var pid2 = 0;

function cleanup() {
    kill_server(pid);
    kill_server(pid2);
    shell("rm -rf " + tmpdir);
}

var ntest = 0;
var npass = 0;
function testFeature(name, test) {
    var error = false;
    ntest++;
    printf("testing auth - %-53s - ", name);
    fflush(stdout);
    if (typeof test == 'function') {
        try {
            test = test();
        } catch(e) {
            error = e;
            test = false;
        }
    }
    if (test) {
        npass++;
        printf("passed\n");
    } else {
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        cleanup();
        process.exit(1);
    }
}

/* create directory structure */
shell("rm -rf " + tmpdir);
mkdir(tmpdir);
mkdir(dbpath);
mkdir(htmldir);
mkdir(htmldir + '/private');
mkdir(htmldir + '/admin');
mkdir(htmldir + '/admin/reports');
mkdir(htmldir + '/public-area');
mkdir(appsdir);
mkdir(appsdir + '/admin');
mkdir(logsdir);

/* create test html files */
fprintf(htmldir + '/index.html', '%s', '<html><body>public index</body></html>');
fprintf(htmldir + '/private/page.html', '%s', '<html><body>private page</body></html>');
fprintf(htmldir + '/private/image.jpg', '%s', 'fake-jpg-data');
fprintf(htmldir + '/private/style.css', '%s', 'body{color:red}');
fprintf(htmldir + '/private/data.json', '%s', '{"x":1}');
fprintf(htmldir + '/admin/dashboard.html', '%s', '<html><body>admin dashboard</body></html>');
fprintf(htmldir + '/admin/reports/q1.html', '%s', '<html><body>Q1 report</body></html>');
fprintf(htmldir + '/admin/reports/chart.png', '%s', 'fake-png-data');
fprintf(htmldir + '/public-area/info.html', '%s', '<html><body>public info</body></html>');

/* create test app module that echoes req.userAuth */
fprintf(appsdir + '/admin/whoami.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'module.exports = function(req) {\n' +
    '    if (req.userAuth) {\n' +
    '        return {json: {authenticated: true, user: req.userAuth}};\n' +
    '    } else {\n' +
    '        return {json: {authenticated: false}};\n' +
    '    }\n' +
    '};\n'
);

/* create a public app module that also checks req.userAuth */
fprintf(appsdir + '/public-check.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'module.exports = function(req) {\n' +
    '    return {json: {hasAuth: !!req.userAuth, user: req.userAuth || null}};\n' +
    '};\n'
);

/* create an app module that tests the JS require("rampart-auth") interface.
   The module now exports a single function (the auth check function). */
fprintf(appsdir + '/auth-jsapi.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'var authFunc = require("rampart-auth");\n' +
    'module.exports = function(req) {\n' +
    '    var isFunc = (typeof authFunc === "function");\n' +
    '    /* call the auth function with a fake app-module req to test it works */\n' +
    '    var fakeReq = {cookies: {}, method: "GET", path: {path: "/public"}};\n' +
    '    var result = authFunc(fakeReq);\n' +
    '    var returned = (typeof result === "object");\n' +
    '    return {json: {isFunc: isFunc, returned: returned}};\n' +
    '};\n'
);

/* create app module that accepts POST and echoes what it got */
fprintf(appsdir + '/posthandler.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'module.exports = function(req) {\n' +
    '    return {json: {\n' +
    '        method: req.method,\n' +
    '        hasAuth: !!req.userAuth,\n' +
    '        csrf: req.userAuth ? req.userAuth.csrfToken : null\n' +
    '    }};\n' +
    '};\n'
);

/* create app module under /apps/admin/ that accepts POST */
fprintf(appsdir + '/admin/action.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'module.exports = function(req) {\n' +
    '    return {json: {ok: true, method: req.method}};\n' +
    '};\n'
);

/* create app module under exempt path */
fprintf(appsdir + '/webhook.js', '%s',
    'rampart.globalize(rampart.utils);\n' +
    'module.exports = function(req) {\n' +
    '    return {json: {ok: true, method: req.method}};\n' +
    '};\n'
);

/* write auth config as JS module */
fprintf(confpath, '%s',
    'module.exports = ' + JSON.stringify({
        cookieName: "test_session",
        dbPath: dbpath,
        redirectExtensions: ["", ".html", ".htm"],
        csrfExemptPaths: ["/apps/webhook"],
        sessionExpiry: 86400,
        sessionRefresh: 1,
        sessionRefreshUrgent: 3600,
        protectedPaths: {
            "/private/": {
                level: 1,
                redirect: "/login.html?returnTo=$origin"
            },
            "/admin/": {
                level: 0,
                redirect: "/login.html?returnTo=$origin"
            },
            "/apps/admin/": {
                level: 0,
                redirect: "/login.html?returnTo=$origin"
            }
        }
    }, null, 2) + ';\n'
);

/* seed LMDB with test sessions */
var lmdb = new Lmdb.init(dbpath, true, {conversion: "json"});

var now = Math.floor(Date.now() / 1000);

lmdb.put(null, "tok-superadmin", {
    username: "superadmin", name: "Super Admin",
    email: "super@test.com", authLevel: 0,
    authMethod: "password", csrfToken: "csrf-admin-secret",
    expires: now + 86400
});

lmdb.put(null, "tok-editor", {
    username: "editor", name: "Editor User",
    email: "editor@test.com", authLevel: 1,
    authMethod: "password", csrfToken: "csrf-editor-secret",
    expires: now + 86400
});

lmdb.put(null, "tok-viewer", {
    username: "viewer", name: "Viewer User",
    email: "viewer@test.com", authLevel: 50,
    authMethod: "password", csrfToken: "csrf-viewer-secret",
    expires: now + 86400
});

/* session with old lastRefresh — should get refreshed */
lmdb.put(null, "tok-refresh-test", {
    username: "refreshuser", name: "Refresh User",
    email: "refresh@test.com", authLevel: 1,
    authMethod: "password", csrfToken: "csrf-refresh",
    expires: now + 600,          /* expires in 10 minutes */
    lastRefresh: now - 120       /* last refreshed 2 minutes ago */
});

lmdb.put(null, "tok-expired", {
    username: "ghost", name: "Expired User",
    email: "ghost@test.com", authLevel: 0,
    expires: now - 3600
});

lmdb.put(null, "tok-oauth", {
    username: "googleuser", name: "Google User",
    email: "guser@gmail.com", authLevel: 1,
    authMethod: "google", oauthProvider: "google",
    oauthId: "12345", expires: now + 86400
});

/* ---- start server ---- */

pid = server.start({
    bind: "127.0.0.1:" + port,
    daemon: true,
    log: true,
    user: "nobody",
    accessLog: logsdir + '/access.log',
    errorLog: logsdir + '/error.log',
    developerMode: true,
    authMod: true,
    authModConf: confpath,
    map: {
        "/":              htmldir,
        "/apps/":         {modulePath: appsdir}
    }
});
sleep(0.5);

/* ---- helper ---- */
function GET(path, cookie) {
    var opts = {maxTime:2};
    if (cookie) opts.headers = ["Cookie: test_session=" + cookie];
    return curl.fetch(opts, base + path);
}

function GET_noredir(path, cookie) {
    var opts = {location: false, maxTime:2};
    if (cookie) opts.headers = ["Cookie: test_session=" + cookie];
    return curl.fetch(opts, base + path);
}

/* ================================================================
   TESTS
   ================================================================ */

testFeature("server is running", kill(pid, 0));

/* -- unprotected paths -- */

testFeature("public, no cookie: 200", function() {
    var res = GET("/index.html");
    return res.status == 200 && res.text.indexOf("public index") > -1;
});

testFeature("public, with cookie: 200", function() {
    return GET("/index.html", "tok-superadmin").status == 200;
});

testFeature("public-area: 200", function() {
    var res = GET("/public-area/info.html");
    return res.status == 200 && res.text.indexOf("public info") > -1;
});

/* -- protected, no cookie -- */

testFeature("no cookie, html: 302", function() {
    return GET_noredir("/private/page.html").status == 302;
});

testFeature("redirect Location correct", function() {
    var res = GET_noredir("/private/page.html");
    var loc = "";
    if (typeof res.headers == 'object') {
        for (var k in res.headers)
            if (k.toLowerCase() == 'location') loc = res.headers[k];
    }
    return loc == "/login.html?returnTo=/private/page.html";
});

testFeature("no cookie, jpg: 403", function() {
    return GET("/private/image.jpg").status == 403;
});

testFeature("no cookie, css: 403", function() {
    return GET("/private/style.css").status == 403;
});

testFeature("no cookie, no ext: 302", function() {
    return GET_noredir("/admin/").status == 302;
});

testFeature("no cookie, app module: 302", function() {
    return GET_noredir("/apps/admin/whoami.html").status == 302;
});

/* -- bad/expired cookies -- */

testFeature("bogus cookie, html: 302", function() {
    return GET_noredir("/private/page.html", "bogus").status == 302;
});

testFeature("expired cookie, html: 302", function() {
    return GET_noredir("/private/page.html", "tok-expired").status == 302;
});

testFeature("expired cookie, jpg: 403", function() {
    return GET("/private/image.jpg", "tok-expired").status == 403;
});

/* -- sufficient privilege -- */

testFeature("admin(0) => admin/(0): 200", function() {
    var res = GET("/admin/dashboard.html", "tok-superadmin");
    return res.status == 200 && res.text.indexOf("admin dashboard") > -1;
});

testFeature("admin(0) => private/(1): 200", function() {
    var res = GET("/private/page.html", "tok-superadmin");
    return res.status == 200 && res.text.indexOf("private page") > -1;
});

testFeature("editor(1) => private/(1): 200", function() {
    var res = GET("/private/page.html", "tok-editor");
    return res.status == 200 && res.text.indexOf("private page") > -1;
});

testFeature("admin => nested report: 200", function() {
    var res = GET("/admin/reports/q1.html", "tok-superadmin");
    return res.status == 200 && res.text.indexOf("Q1 report") > -1;
});

testFeature("admin => protected jpg: 200", function() {
    var res = GET("/private/image.jpg", "tok-superadmin");
    return res.status == 200 && res.text == "fake-jpg-data";
});

testFeature("admin => protected css: 200", function() {
    return GET("/private/style.css", "tok-superadmin").status == 200;
});

/* -- insufficient privilege -- */

testFeature("editor(1) => admin/(0): 302", function() {
    return GET_noredir("/admin/dashboard.html", "tok-editor").status == 302;
});

testFeature("viewer(50) => private/(1): 302", function() {
    return GET_noredir("/private/page.html", "tok-viewer").status == 302;
});

testFeature("viewer(50) => admin/(0): 302", function() {
    return GET_noredir("/admin/dashboard.html", "tok-viewer").status == 302;
});

testFeature("viewer => private jpg: 403", function() {
    return GET("/private/image.jpg", "tok-viewer").status == 403;
});

testFeature("editor => admin png: 403", function() {
    return GET("/admin/reports/chart.png", "tok-editor").status == 403;
});

/* -- path inheritance -- */

testFeature("nested inherits admin/(0)", function() {
    return GET_noredir("/admin/reports/q1.html", "tok-editor").status == 302;
});

testFeature("nested, admin access: 200", function() {
    return GET("/admin/reports/q1.html", "tok-superadmin").status == 200;
});

/* -- req.userAuth on app modules -- */

testFeature("app: admin sees userAuth", function() {
    var res = GET("/apps/admin/whoami.html", "tok-superadmin");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.authenticated && b.user.username === "superadmin" &&
           b.user.authLevel === 0 && b.user.email === "super@test.com";
});

testFeature("app: editor denied admin/: 302", function() {
    return GET_noredir("/apps/admin/whoami.html", "tok-editor").status == 302;
});

testFeature("app: public, cookie => userAuth", function() {
    var res = GET("/apps/public-check.html", "tok-viewer");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.hasAuth && b.user.username === "viewer" && b.user.authLevel === 50;
});

testFeature("app: public, no cookie => no userAuth", function() {
    var res = GET("/apps/public-check.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return !b.hasAuth && b.user === null;
});

/* -- oauth user -- */

testFeature("oauth(1) => private/(1): 200", function() {
    return GET("/private/page.html", "tok-oauth").status == 200;
});

testFeature("oauth fields preserved", function() {
    var res = GET("/apps/public-check.html", "tok-oauth");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.user.authMethod === "google" &&
           b.user.oauthProvider === "google" && b.user.oauthId === "12345";
});

/* -- cookie edge cases -- */

testFeature("wrong cookie name: denied", function() {
    return GET_noredir("/private/page.html", "").status == 302 ||
           curl.fetch({location:false, headers:["Cookie: wrong=tok-superadmin"]},
                      base + "/private/page.html").status == 302;
});

testFeature("cookie among many: works", function() {
    var res = curl.fetch(
        {headers: ["Cookie: a=1; test_session=tok-superadmin; b=2"]},
        base + "/private/page.html");
    return res.status == 200;
});

testFeature("empty cookie value: denied", function() {
    return curl.fetch({location:false, headers:["Cookie: test_session="]},
                      base + "/private/page.html").status == 302;
});

/* -- $origin encoding -- */

testFeature("$origin in redirect URL", function() {
    return GET_noredir("/private/page.html").status == 302;
});

/* -- CSRF protection -- */

testFeature("csrf: GET is exempt", function() {
    var res = GET("/apps/posthandler.html", "tok-superadmin");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.method === "GET" && b.hasAuth === true;
});

testFeature("csrf: token in req.userAuth", function() {
    var res = GET("/apps/posthandler.html", "tok-superadmin");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.csrf === "csrf-admin-secret";
});

testFeature("csrf: POST no token => 403", function() {
    var res = curl.fetch({
        post: "action=delete",
        headers: ["Cookie: test_session=tok-superadmin"]
    }, base + "/apps/posthandler.html");
    return res.status == 403;
});

testFeature("csrf: POST wrong token => 403", function() {
    var res = curl.fetch({
        post: "_csrf=wrong-token&action=delete",
        headers: ["Cookie: test_session=tok-superadmin"]
    }, base + "/apps/posthandler.html");
    return res.status == 403;
});

testFeature("csrf: POST correct token => 200", function() {
    var res = curl.fetch({
        post: "_csrf=csrf-admin-secret&action=delete",
        headers: ["Cookie: test_session=tok-superadmin"]
    }, base + "/apps/posthandler.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.method === "POST" && b.hasAuth === true;
});

testFeature("csrf: X-CSRF-Token header => 200", function() {
    var res = curl.fetch({
        postJSON: {action: "delete"},
        headers: ["Cookie: test_session=tok-superadmin",
                  "X-CSRF-Token: csrf-admin-secret"]
    }, base + "/apps/posthandler.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.method === "POST" && b.hasAuth === true;
});

testFeature("csrf: exempt path, no token => 200", function() {
    var res = curl.fetch({
        post: "data=test",
        headers: ["Cookie: test_session=tok-superadmin"]
    }, base + "/apps/webhook.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.ok === true && b.method === "POST";
});

testFeature("csrf: POST no session => no check", function() {
    var res = curl.fetch({
        post: "action=test"
    }, base + "/apps/posthandler.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.hasAuth === false;
});

/* -- sliding session expiry -- */

testFeature("refresh: session extended", function() {
    /* read the session before request */
    var before = lmdb.get(null, "tok-refresh-test");
    var old_expires = before.expires;

    /* make a request — lastRefresh is 2 min old, refresh interval is 1s */
    sleep(1.1); /* ensure > 1 second since lastRefresh */
    var res = GET("/apps/public-check.html", "tok-refresh-test");
    if (res.status != 200) return false;

    /* read the session after request */
    var after = lmdb.get(null, "tok-refresh-test");

    /* expires should have been extended (now + 86400 > old now + 600) */
    return after.expires > old_expires &&
           after.lastRefresh > before.lastRefresh;
});

testFeature("refresh: not refreshed too soon", function() {
    /* make another request immediately — lastRefresh was just set */
    var before = lmdb.get(null, "tok-refresh-test");
    var res = GET("/apps/public-check.html", "tok-refresh-test");
    if (res.status != 200) return false;
    var after = lmdb.get(null, "tok-refresh-test");

    /* should NOT have been refreshed (< 1 second since last) */
    return after.lastRefresh === before.lastRefresh;
});

/* -- JS API from server module -- */

testFeature("JS API: exports a function", function() {
    var res = GET("/apps/auth-jsapi.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.isFunc === true && b.returned === true;
});

/* ================================================================
   TEST: Third-party inline auth function
   Start a second server with authMod: function(req){...}
   ================================================================ */

/* stop the first server */
kill_server(pid);
pid = 0;

/* set up a second server with a custom inline auth function */
var port2 = port + 1;
var base2 = 'http://127.0.0.1:' + port2;
var confpath2 = tmpdir + '/auth2-conf.js';

/* config: only protectedPaths needed (no cookieName/dbPath — custom func handles it) */
var authConf2 = {
    protectedPaths: {
        "/private/": {
            level: 0,
            redirect: "/login?r=$origin"
        }
    },
    redirectExtensions: ["", ".html"]
};
fprintf(confpath2, '%s', 'module.exports = ' + JSON.stringify(authConf2) + ';\n');

pid2 = server.start({
    bind: "127.0.0.1:" + port2,
    daemon: true,
    log: true,
    user: "nobody",
    accessLog: logsdir + '/access2.log',
    errorLog: logsdir + '/error2.log',
    developerMode: true,
    authModConf: confpath2,
    authMod: function(req) {
        /* custom auth: check for a header instead of a cookie */
        var token = null;

        if (req.headers && req.headers['X-Auth-Token'])
            token = req.headers['X-Auth-Token'];

        if (req.method) {
            /* app module path — modify req in place, return true (not req) */
            if (token === "secret123") {
                req.userAuth = {username: "custom-user", role: "admin"};
            }
            return true;
        } else {
            /* file path: return true/false */
            return (token === "secret123");
        }
    },
    map: {
        "/":              htmldir,
        "/apps/":         {modulePath: appsdir}
    }
});

sleep(0.5);

function GET2(path, token) {
    var opts = {};
    if (token) opts.headers = ["X-Auth-Token: " + token];
    return curl.fetch(opts, base2 + path);
}

function GET2_noredir(path, token) {
    var opts = {location: false};
    if (token) opts.headers = ["X-Auth-Token: " + token];
    return curl.fetch(opts, base2 + path);
}

testFeature("3p: server running", kill(pid2, 0));

testFeature("3p: public file, no token: 200", function() {
    return GET2("/index.html").status == 200;
});

testFeature("3p: protected file, no token: 403", function() {
    return GET2("/private/image.jpg").status == 403;
});

testFeature("3p: protected html, no token: 302", function() {
    return GET2_noredir("/private/page.html").status == 302;
});

testFeature("3p: protected file, valid token: 200", function() {
    var res = GET2("/private/page.html", "secret123");
    return res.status == 200 && res.text.indexOf("private") > -1;
});

testFeature("3p: protected file, bad token: 403", function() {
    return GET2("/private/image.jpg", "wrong").status == 403;
});

testFeature("3p: app, valid token => userAuth", function() {
    var res = GET2("/apps/public-check.html", "secret123");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.hasAuth === true &&
           b.user.username === "custom-user" &&
           b.user.role === "admin";
});

testFeature("3p: app, no token => no userAuth", function() {
    var res = GET2("/apps/public-check.html");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.hasAuth === false;
});

testFeature("3p: return true, req.userAuth set", function() {
    /* auth func modifies req in place and returns true (not req).
       verify that req.userAuth is still visible to the endpoint */
    var res = GET2("/apps/public-check.html", "secret123");
    if (res.status != 200) return false;
    var b = JSON.parse(res.text);
    return b.hasAuth === true &&
           b.user.username === "custom-user" &&
           b.user.role === "admin";
});

kill_server(pid2);

/* ---- done ---- */
printf("\n%d/%d tests passed.\n", npass, ntest);
cleanup();
