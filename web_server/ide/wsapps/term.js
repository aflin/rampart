/* Mini IDE terminal sessions over a websocket.
     /ide/wsapps/term.js?mode=shell[&path=dir] - login shell in the web server root (or dir)
     /ide/wsapps/term.js?mode=repl             - interactive rampart
     /ide/wsapps/term.js?mode=run&path=a/b.js  - rampart a/b.js, run from its directory
     /ide/wsapps/term.js?mode=bundle&opts=JSON - build a bundle (ide/lib/bundle-build.js)
   Text messages are keyboard input; binary messages are JSON commands
   ({"resize":{"cols":n,"rows":n}}).                                        */

var u   = rampart.utils;
var lib = require(module.path + '/../lib/ide-lib.js');

/* rampart parses JSON-looking query values into objects */
function bundleOpts(o) {
    return typeof o == 'object' && o ? JSON.stringify(o) : String(o || '');
}

function startPty(req) {
    var rp  = process.installPathExec;
    var env = {
        TERM:      'xterm-256color',
        COLORTERM: 'truecolor',
        PATH:      process.installPathBin + ':' + (process.env.PATH || '/usr/bin:/bin'),
        SHELL:     process.env.SHELL || '/bin/sh',
        MINI_IDE:  '1'
    };
    var opts = {env: env, appendEnv: true};
    var mode = req.params.mode;

    if (mode == 'repl')
        return u.forkpty('/bin/sh', opts, '-c', 'cd "$1" && exec "$2"', 'sh', lib.siteRoot, rp);

    if (mode == 'run') {
        var p = lib.resolve(req.params.path);
        var dir = p.abs.replace(/\/[^\/]*$/, ''), file = p.abs.replace(/.*\//, '');
        /* args are parsed with shell quoting rules */
        return u.forkpty('/bin/sh', opts, '-c',
            'cd "$1" || exit 1; f=$2; a=$3; eval "set -- $a"; ' +
            'printf "\\033[2m$ rampart %s %s\\033[0m\\r\\n" "$f" "$a"; ' +
            '"$0" "$f" "$@"; s=$?; ' +
            'printf "\\r\\n\\033[2m[%s exited with status %d]\\033[0m\\r\\n" "$f" $s',
            rp, dir, file, String(req.params.args || ''));
    }

    if (mode == 'bundle')
        return u.forkpty('/bin/sh', opts, '-c',
            'cd "$1" || exit 1; "$0" ide/lib/bundle-build.js "$2"; s=$?; ' +
            'printf "\\r\\n\\033[2m[bundle build exited with status %d]\\033[0m\\r\\n" $s',
            rp, lib.siteRoot, bundleOpts(req.params.opts));

    var cwd = req.params.path ? lib.resolve(req.params.path).abs : lib.siteRoot;
    return u.forkpty('/bin/sh', opts, '-c', 'cd "$1" && exec "$SHELL" -l', 'sh', cwd);
}

module.exports = function (req) {

    if (!req.count) {
        if (!lib.originOk(req) || !lib.checkSession(req)) {
            req.wsSend("Not logged in.\r\n");
            req.wsEnd();
            return;
        }
        try {
            req.con = startPty(req);
        } catch (e) {
            req.wsSend("Could not start process: " + e.message + "\r\n");
            req.wsEnd();
            return;
        }
        req.wsOnDisconnect(function () {
            if (req.con.close) req.con.close();
        });
        req.con.on("data", function () {
            if (req.con.read) {
                var d = req.con.read();
                if (d) req.wsSend(d);
            }
        });
        req.con.on("close", function () {
            req.wsEnd();
        });
        var cols = parseInt(req.params.cols) || 80, rows = parseInt(req.params.rows) || 24;
        req.con.resize(cols, rows);
        return;
    }

    if (!req.body.length || !req.con.write)
        return;

    if (!req.wsIsBin) {
        req.con.write(req.body);
        return;
    }

    var cmd;
    try {
        cmd = JSON.parse(u.bufferToString(req.body));
    } catch (e) {
        return;
    }
    if (cmd && cmd.resize && req.con.resize) {
        var r = cmd.resize;
        if (typeof r.cols == 'number' && typeof r.rows == 'number')
            req.con.resize(r.cols, r.rows);
    }
};
