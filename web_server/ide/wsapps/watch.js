/* Mini IDE directory watcher over a websocket: /ide/wsapps/watch.js
   The client sends {"watch": ["", "apps", "apps/sub", ...]}, the directories
   currently shown in its file tree, whenever that set changes.  A watcher is
   kept for each; when entries are added, removed or renamed the client is
   sent {"changed": "apps"}.  Writes to a file's contents are ignored, so an
   expanded logs/ does not refresh on every request.                         */

var u   = rampart.utils;
var lib = require(module.path + '/../lib/ide-lib.js');

var maxWatched = 64;
var debounceMs = 250;

function notify(req, rel) {
    if (req.pending[rel]) return;
    req.pending[rel] = setTimeout(function () {
        delete req.pending[rel];
        if (req.watchers[rel]) req.wsSend(JSON.stringify({changed: rel}));
    }, debounceMs);
}

function addWatcher(req, rel) {
    var p;
    try { p = lib.resolve(rel); } catch (e) { return; }
    var st = u.stat(p.abs);
    if (!st || !st.isDirectory) return;
    var abs = p.abs;
    try {
        req.watchers[p.rel] = u.watch(abs, function (ev) {
            /* inotify reports the child; content changes are not listing changes.
               The polling backend reports the directory itself.                */
            if (ev.type == 'change' && ev.path != abs) return;
            notify(req, p.rel);
        });
    } catch (e) {
        /* unwatchable (permissions, too many watches): the tree still works */
    }
}

function closeAll(req) {
    for (var rel in req.watchers)
        req.watchers[rel].close();
    req.watchers = {};
    for (var k in req.pending)
        clearTimeout(req.pending[k]);
    req.pending = {};
}

function setWatched(req, list) {
    var want = {}, n = 0, rel;
    for (var i = 0; i < list.length && n < maxWatched; i++) {
        if (typeof list[i] != 'string') continue;
        try { rel = lib.resolve(list[i]).rel; } catch (e) { continue; }
        if (!want[rel]) { want[rel] = true; n++; }
    }
    for (rel in req.watchers) {
        if (!want[rel]) {
            req.watchers[rel].close();
            delete req.watchers[rel];
        }
    }
    for (rel in want)
        if (!req.watchers[rel]) addWatcher(req, rel);
}

module.exports = function (req) {

    if (!req.count) {
        if (!lib.originOk(req) || !lib.checkSession(req)) {
            req.wsSend(JSON.stringify({error: "Not logged in"}));
            req.wsEnd();
            return;
        }
        req.watchers = {};
        req.pending = {};
        req.wsOnDisconnect(function () { closeAll(req); });
        return;
    }

    var msg;
    try {
        msg = JSON.parse(u.bufferToString(req.body));
    } catch (e) {
        return;
    }
    if (msg && Array.isArray(msg.watch))
        setWatched(req, msg.watch);
};
