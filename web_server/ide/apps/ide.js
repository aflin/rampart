/* Mini IDE JSON API.  Served at /ide/apps/ide/<name>.json by ide/mini-ide-conf.js only.
   All calls are POSTs with a JSON body and an "X-Mini-IDE" header.         */

var u   = rampart.utils;
var lib = require(module.path + '/../lib/ide-lib.js');

var maxEditSize = 10 * 1024 * 1024;

function fail(msg, status) {
    return {status: status || 400, json: {error: msg}};
}

function body(req) {
    try {
        var b = JSON.parse(u.bufferToString(req.body));
        return (b && typeof b == 'object') ? b : {};
    } catch (e) {
        return {};
    }
}

function fileInfo(abs, name) {
    var lst = u.lstat(abs);
    if (!lst) return null;
    var st = lst.isSymbolicLink ? u.stat(abs) : lst;   // broken link: describe the link itself
    if (!st) st = lst;
    return {
        name:        name,
        dir:         !!st.isDirectory,
        link:        !!lst.isSymbolicLink,
        size:        st.size,
        mtime:       st.mtime.getTime(),
        permissions: st.permissions,
        owner:       st.owner,
        group:       st.group,
        writable:    !!st.writable,
        executable:  !!st.executable && !st.isDirectory
    };
}

/* NUL, 0xFE and 0xFF never occur in UTF-8 text; checking the first 64k is enough */
function isBinary(buf) {
    var n = buf.length < 65536 ? buf.length : 65536;
    for (var i = 0; i < n; i++)
        if (buf[i] == 0 || buf[i] >= 0xfe) return true;
    return false;
}

/* wrap a handler: origin and CSRF checks, session check, JSON body, error catching */
function api(fn, noAuth) {
    return function (req) {
        if (req.method != 'POST')
            return fail("POST required", 405);
        if (!lib.originOk(req) || !lib.header(req, 'X-Mini-IDE'))
            return fail("Forbidden", 403);
        if (!noAuth && !lib.checkSession(req))
            return fail("Not logged in", 401);
        try {
            return fn(body(req), req);
        } catch (e) {
            return fail(e.message);
        }
    };
}

function status(b, req) {
    var host = u.exec('hostname').stdout.trim();
    if (!lib.checkSession(req))
        return {json: {loggedIn: false, host: host}};
    return {json: {
        loggedIn: true,
        host:     host,
        root:     lib.siteRoot.replace(/.*\//, ''),
        rootPath: lib.siteRoot,
        version:  rampart.version
    }};
}

function login(b) {
    if (!b.password || !lib.checkPassword(b.password)) {
        u.sleep(1);   /* slow down guessing; rateLimit in the config limits it further */
        return fail("Incorrect password", 401);
    }
    return {json: {ok: true}, headers: {"Set-Cookie": lib.loginCookie()}};
}

function logout() {
    lib.rotateSecret();
    return {json: {ok: true}, headers: {"Set-Cookie": lib.logoutCookie()}};
}

function list(b) {
    var p = lib.resolve(b.path);
    var st = u.stat(p.abs);
    if (!st || !st.isDirectory) return fail("Not a directory: /" + p.rel, 404);
    var names = u.readDir(p.abs, !!b.hidden), entries = [];
    for (var i = 0; i < names.length; i++) {
        if (names[i] == '.' || names[i] == '..') continue;
        var info = fileInfo(p.abs + '/' + names[i], names[i]);
        if (info) entries.push(info);
    }
    entries.sort(function (a, c) {
        if (a.dir != c.dir) return a.dir ? -1 : 1;
        var x = a.name.toLowerCase(), y = c.name.toLowerCase();
        return x < y ? -1 : x > y ? 1 : 0;
    });
    return {json: {path: p.rel, entries: entries}};
}

function info(b) {
    var p = lib.resolve(b.path);
    var inf = fileInfo(p.abs, p.rel.replace(/.*\//, ''));
    if (!inf) return fail("Not found: /" + p.rel, 404);
    inf.path = p.rel;
    return {json: inf};
}

function read(b) {
    var p = lib.resolve(b.path);
    var inf = fileInfo(p.abs, p.rel.replace(/.*\//, ''));
    if (!inf || inf.dir) return fail("Not a file: /" + p.rel, 404);
    inf.path = p.rel;
    if (inf.size > maxEditSize) {
        inf.tooLarge = true;
        return {json: inf};
    }
    var buf = u.readFile(p.abs);
    if (isBinary(buf)) {
        inf.binary = true;
        return {json: inf};
    }
    inf.content = u.bufferToString(buf);
    return {json: inf};
}

/* b.mtime - modification time when the file was loaded; a mismatch is a conflict
   b.create - file must not exist yet; missing parent folders are created       */
function write(b) {
    var p = lib.resolve(b.path);
    if (!p.rel) return fail("No file name given");
    if (typeof b.content != 'string') return fail("No content given");
    var st = u.stat(p.abs);
    if (st && st.isDirectory) return fail("Is a directory: /" + p.rel);
    if (b.create && st) return fail("Already exists: /" + p.rel, 409);
    if (st && !b.force && b.mtime && st.mtime.getTime() != b.mtime)
        return {status: 409, json: {error: "File changed on disk since it was opened", conflict: true}};
    var dir = p.abs.replace(/\/[^\/]*$/, '');
    if (b.create && !u.stat(dir))
        u.mkdir(dir);       /* new files may name folders that do not exist yet */
    u.fprintf(p.abs, '%s', b.content);
    var inf = fileInfo(p.abs, p.rel.replace(/.*\//, ''));
    inf.path = p.rel;
    return {json: inf};
}

function mkdir(b) {
    var p = lib.resolve(b.path);
    if (u.stat(p.abs)) return fail("Already exists: /" + p.rel, 409);
    u.mkdir(p.abs);
    return {json: {ok: true, path: p.rel}};
}

function rename(b) {
    var from = lib.resolve(b.from), to = lib.resolve(b.to);
    if (!from.rel || !to.rel) return fail("Cannot rename the root directory");
    if (!u.lstat(from.abs)) return fail("Not found: /" + from.rel, 404);
    if (u.lstat(to.abs)) return fail("Already exists: /" + to.rel, 409);
    u.rename(from.abs, to.abs);
    return {json: {ok: true, path: to.rel}};
}

function del(b) {
    var p = lib.resolve(b.path);
    if (!p.rel) return fail("Cannot delete the root directory");
    var lst = u.lstat(p.abs);
    if (!lst) return fail("Not found: /" + p.rel, 404);
    if (lst.isDirectory)
        u.rm(p.abs, {recursive: true});
    else
        u.rmFile(p.abs);
    return {json: {ok: true}};
}

module.exports = {
    "/status.json": api(status, true),
    "/login.json":  api(login, true),
    "/logout.json": api(logout),
    "/list.json":   api(list),
    "/info.json":   api(info),
    "/read.json":   api(read),
    "/write.json":  api(write),
    "/mkdir.json":  api(mkdir),
    "/rename.json": api(rename),
    "/delete.json": api(del)
};
