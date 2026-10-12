#!/usr/bin/env rampart

/*
Configuration for the Mini IDE, a browser-based editor, file navigator and
terminal for the files in this web server directory.  It is normally started
with ./start-mini-ide.sh, which runs the one-time setup if needed.

  usage:
    rampart mini-ide-conf.js [start|stop|restart|status|dump|setup|help]
        start     -- start the IDE server
        stop      -- stop the IDE server
        restart   -- stop and restart the IDE server
        status    -- show status of server processes
        dump      -- dump the config object used for server.start()
        setup     -- read password, bindAll (y/n) and port, one per line, from stdin
        help      -- show this message

The server is the same as one started with web_server_conf.js using its
defaults: process.scriptPath, html/, apps/, wsapps/, data/ and logs/ are the
same, so scripts written in the IDE see the same serverConf and work the same
here and on the normal server.  The IDE itself is added with appendMap at
/ide/, served from ide/html, ide/apps and ide/wsapps.  A server started with
web_server_conf.js does not map ide/, so the IDE is not visible there.

Only serverRoot differs: it is ide/, so this server's pid file does not
replace the normal server's, and the two can run at the same time on
different ports.

Settings made by setup are kept in ide/data/ide-settings.json.  Anyone who
logs in to the IDE can run commands in a terminal as the user running this
server, so leave bindAll off unless the machine is on a trusted network,
and never start it as root.
*/

var u = rampart.utils;

//set working directory to the location of this script, as in web_server_conf.js
var working_directory = process.scriptPath;
var ide_directory = working_directory + '/ide';

if (!u.stat(ide_directory + '/html'))
    die("The Mini IDE is not installed here (no " + ide_directory + " directory).");

var settingsFile = ide_directory + '/data/ide-settings.json';

function die(msg) {
    u.fprintf(u.stderr, "%s\n", msg);
    process.exit(1);
}

/* ---- one time setup: password, bindAll and port from stdin ---- */
if (process.argv[2] == 'setup') {
    var crypto = require("rampart-crypto");
    var inf = u.stdin.readLine(), lines = [], l;
    while (lines.length < 3 && (l = inf.next()))
        lines.push(l.replace(/\r?\n$/, ''));

    var pass = lines[0] || '', ba = (lines[1] || '').toLowerCase(), port = parseInt(lines[2]);
    if (pass.length < 4)
        die("setup: password must be at least 4 characters");
    if (isNaN(port) || port < 1 || port > 65535)
        die("setup: invalid port '" + (lines[2] || '') + "'");

    if (!u.stat(ide_directory + '/data'))
        u.mkdir(ide_directory + '/data');
    u.fprintf(settingsFile, '%3J\n', {
        passwordHash: crypto.passwd(pass).line,
        secret:       u.sprintf('%-0B', crypto.rand(32)),
        bindAll:      (ba == 'y' || ba == 'yes' || ba == 'true'),
        port:         port
    });
    u.chmod(settingsFile, "600");
    u.printf("Settings saved to %s\n", settingsFile);
    process.exit(0);
}

var settings = {};
try {
    settings = JSON.parse(u.readFile(settingsFile, {returnString: true}));
} catch (e) {
    if (process.argv[2] != 'stop' && process.argv[2] != 'status')
        die("No settings found in " + settingsFile + ".  Run ./start-mini-ide.sh to set a password.");
}

if ((!process.argv[2] || /start/.test(process.argv[2])) && u.exec('id', '-u').stdout.trim() == '0')
    die("The Mini IDE gives a shell to whoever logs in.  Do not run it as root.");

/* the IDE's own pages, api and terminals, plus the documentation built by
   ide/apps/docs/rsearch.js (its pages use /docs/ and /apps/docs/rsearch/) */
var ide_map = {
    "/ide/":            ide_directory + '/html',
    "/ide/apps/":       {modulePath: ide_directory + '/apps'},
    "ws://ide/wsapps/": {modulePath: ide_directory + '/wsapps'},
    "/docs/":           ide_directory + '/html/docs',
    "/apps/docs/":      {modulePath: ide_directory + '/apps/docs'}
};

var serverConf = {

    /* bindAll            Bool.   Listen on all interfaces (0.0.0.0 and [::]) instead of localhost only   */
    bindAll:               !!settings.bindAll,

    /* ipAddr, ipv6Addr    String. Addresses to bind when bindAll is false   */
    ipAddr:                '127.0.0.1',
    ipv6Addr:              '[::1]',

    /* port                Number. Set both ipv4 and ipv6 port   */
    port:                  settings.port || 8089,

    /* serverRoot          String. Where the pid file is kept.  ide/ rather than web_server/, so the
                                   IDE and the normal web server can run at the same time.
                                   The roots below are therefore set explicitly to their
                                   web_server_conf.js defaults.                            */
    serverRoot:            ide_directory,

    /* htmlRoot, appsRoot, wsappsRoot, dataRoot, logRoot
                           String. The same as web_server_conf.js   */
    htmlRoot:              working_directory + '/html',
    appsRoot:              working_directory + '/apps',
    wsappsRoot:            working_directory + '/wsapps',
    dataRoot:              working_directory + '/data',
    logRoot:               working_directory + '/logs',

    /* appendMap           Object. The IDE at /ide/, added to the normal map.  Any mappings
                           added in web_server_conf.js should be added here as well.  */
    appendMap:             ide_map,

    /* cacheControl        String. Browsers revalidate every file (a quick 304 when unchanged), so
                                   edits made in the IDE and IDE updates show up on reload.
                                   The default ("max-age=84600, public") lets them use
                                   day-old copies.                                         */
    cacheControl:          "no-cache",

    /* rateLimit           Object. Limit password guessing   */
    rateLimit: {
        "/ide/apps/ide/login.json": {rate: 10, window: 60, key: "ip"}
    },

    /* daemon              Bool.   Detach from the terminal; stop with "stop"   */
    //daemon:              true,

    /* log                 Bool.   Whether to log requests and errors to logRoot   */
    //log:                 true,

    /* developerMode       Bool.   Whether JavaScript errors result in 500 and return a stack trace   */
    //developerMode:       true,

    /* secure, sslKeyFile, sslCertFile, selfSign
                           Serve over https.  Recommended if bindAll is set.
                           See web_server_conf.js for details.   */
    //secure:              false,
    //selfSign:            false,

    /* threads             Number. Server threads (default: number of cores)   */
    //threads:             -1,
}


if (module && module.exports) {

    module.exports = function () {
        serverConf.dumpObj = true;
        var ret = require("rampart-webserver").web_server_conf(serverConf);
        delete ret.dumpObj;
        return ret;
    }

} else {

    require("rampart-webserver").web_server_conf(serverConf);

    var cmd = process.argv[2];
    if (!cmd || cmd == 'start' || cmd == 'restart') {
        var scheme = serverConf.secure || serverConf.selfSign ? 'https' : 'http';
        u.printf("Mini IDE: %s://localhost:%d/ide/ (listening on %s)\n", scheme, serverConf.port,
                 serverConf.bindAll ? "all interfaces, 0.0.0.0 and [::]" : "localhost only");
    }
}
