/* Build a single-file bundle of the web server: the rampart executable with
   a zip of the site appended.  Experimental.
   From the command line (run "rampart ide/lib/bundle-build.js --help"):
     rampart ide/lib/bundle-build.js [--name myproject] [--output FILE] ...
   The Mini IDE (File > Build Bundle) passes its choices as JSON instead:
     rampart ide/lib/bundle-build.js '<options json>'
   JSON options:
     name         project name; also used for ~/.rampart/<name>
     output       path of the bundle to write (not inside the web server)
     dataDir      data directory the bundle uses (may start with ~/)
     logsDir      logs directory the bundle uses
     dataInclude  entries of data/ to ship as starting data ([] for none)
     seedPolicy   "first" (only into a new data directory) or "missing"
     modules      rampart modules to include, e.g. ["rampart-sql"]
     macWrapper   (macOS) also write a wrapper that clears the quarantine flag
     id           (IDE) build id; on success the bundle is recorded under it in
                  ide/data/bundle-builds.json so the IDE can offer a download
   Also usable as a module: require(...).info() describes the defaults.     */

var u = rampart.utils;

/* module.path when required by the IDE, process.scriptPath when run directly */
var ideRoot  = u.realPath(((module && module.path) || process.scriptPath) + '/..');
var siteRoot = u.realPath(ideRoot + '/..');
var isMac    = /darwin/i.test(u.exec('uname').stdout);

/* the login system: included only when used (see authUse()) */
var authFiles = ['apps/auth.js', 'apps/auth-plugins', 'apps/priv', 'auth-conf.js', 'html/css/auth.css'];

/* files never shipped: the IDE, run-time files, editor and VCS clutter */
var exclude = ['./ide', './mini-ide-conf.js', './start-mini-ide.sh', './AGENTS.md', './CLAUDE.md',
               './*.pid', './logs/*', './data', '*~', '*.swp', '.DS_Store', '.git'];

function die(msg) {
    u.fprintf(u.stderr, "Error: %s\n", msg);
    process.exit(1);
}

function sh(cmd) {
    var r = u.shell(cmd);
    if (r.exitStatus) die(cmd + "\n" + r.stderr);
    return r.stdout;
}

function q(s) {
    return "'" + String(s).replace(/'/g, "'\\''") + "'";
}

/* source with comments removed, so commented-out require()s are not counted
   (a "//" right after ':' or a quote is kept: it is probably in a URL)   */
function stripComments(src) {
    return src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|[^:"'\\])\/\/.*$/gm, '$1');
}

/* rampart modules required by the given files, following required modules
   that are JavaScript.  Returns {module: ["apps/x.js", "via rampart-email", ...]} */
function findModules(files) {
    var found = {}, queue = files.map(function (f) { return {file: f, by: f.replace(siteRoot + '/', '')}; });
    while (queue.length) {
        var it = queue.shift(), src;
        try { src = stripComments(u.readFile(it.file, {returnString: true})); } catch (e) { continue; }
        var re = /require\s*\(\s*["'](rampart-[A-Za-z0-9_-]+)(?:\.js|\.so)?["']\s*\)/g, m;
        while ((m = re.exec(src))) {
            var name = m[1], refs = found[name] || (found[name] = []);
            if (refs.indexOf(it.by) == -1) refs.push(it.by);
            /* rampart-webserver loads crypto and curl only for self-signed
               certificates and the monitor; info() adds those when used */
            var js = process.modulesPath + '/' + name + '.js';
            if (refs.length == 1 && name != 'rampart-webserver' && u.stat(js))
                queue.push({file: js, by: 'via ' + name});
        }
    }
    for (var n in found)
        if (!moduleFiles(n).length) delete found[n];
    return found;
}

/* whether the site uses the login system, and why: authMod is on, or the
   site's own code requires apps/auth.js.  {needed, reason}               */
function authUse(defaults, js) {
    if (defaults.authMod) return {needed: true, reason: 'authMod is on in web_server_conf.js'};
    var re = /require\s*\(\s*["'](?:\.\.?\/)*(?:apps\/)?auth(?:\.js)?["']\s*\)/;
    for (var i = 0; i < js.site.length; i++) {
        var src;
        try { src = stripComments(u.readFile(js.site[i], {returnString: true})); } catch (e) { continue; }
        if (re.test(src))
            return {needed: true, reason: 'required by ' + js.site[i].replace(siteRoot + '/', '')};
    }
    return {needed: false, reason: 'authMod is off and nothing requires apps/auth.js'};
}

/* modules rampart-webserver itself needs with these settings */
function withServerNeeds(found, d) {
    function add(m, why) { (found[m] || (found[m] = [])).push(why); }
    add('rampart-server', 'the web server');
    add('rampart-webserver', 'the web server');
    if (d.selfSign) add('rampart-crypto', 'selfSign in web_server_conf.js');
    if (d.monitor) add('rampart-curl', 'monitor in web_server_conf.js');
    return found;
}

/* rampart modules a native module is linked against (its NEEDED entries),
   e.g. rampart-server.so needs rampart-crypto.so.  Modules only loaded on
   demand (dlopen) are not counted.                                        */
var linkCache = {};
function linkDeps(name) {
    if (linkCache[name]) return linkCache[name];
    var so = process.modulesPath + '/' + name + '.so', out = [];
    if (u.stat(so)) {
        var r = u.exec('/bin/sh', {args: ['-c',
            'if command -v readelf >/dev/null; then readelf -d "$1" | grep NEEDED; ' +
            'elif command -v otool >/dev/null; then otool -L "$1"; ' +
            'else grep -ao "rampart-crypto\\.so" "$1" | head -1; fi', 'sh', so]});
        var re = /(rampart-[A-Za-z0-9_-]+)\.so/g, m;
        while ((m = re.exec(r.stdout)))
            if (m[1] != name && out.indexOf(m[1]) == -1 && moduleFiles(m[1]).length) out.push(m[1]);
    }
    return (linkCache[name] = out);
}

/* add the modules that the found native modules are linked against */
function withLinkDeps(found) {
    var changed = true;
    while (changed) {
        changed = false;
        for (var n in found) {
            linkDeps(n).forEach(function (d) {
                var refs = found[d] || (found[d] = []);
                if (refs.indexOf('via ' + n) == -1) { refs.push('via ' + n); changed = true; }
            });
        }
    }
    return found;
}

function isAuthFile(rel) {
    return authFiles.some(function (a) { return rel == a || rel.indexOf(a + '/') == 0; });
}

function moduleFiles(name) {
    return ['.so', '.js'].map(function (x) { return process.modulesPath + '/' + name + x; })
                         .filter(function (f) { return u.stat(f); });
}

function jsFiles(dir) {
    var out = [];
    if (!u.stat(dir)) return out;
    (function walk(d) {
        var names = u.readDir(d);
        for (var i = 0; i < names.length; i++) {
            var p = d + '/' + names[i], st = u.stat(p);
            if (!st) continue;
            if (st.isDirectory) {
                if (!/\/(data|node_modules)$/.test(p)) walk(p);
            } else if (/\.js$/.test(p)) out.push(p);
        }
    })(dir);
    return out;
}

/* the site's JavaScript, split into the login system and everything else */
function siteJs() {
    var all = jsFiles(siteRoot + '/apps').concat(jsFiles(siteRoot + '/wsapps'),
        u.readDir(siteRoot).filter(function (n) { return /\.js$/.test(n) && n != 'mini-ide-conf.js'; })
                           .map(function (n) { return siteRoot + '/' + n; }));
    var rel = function (f) { return f.replace(siteRoot + '/', ''); };
    return {
        site: all.filter(function (f) { return !isAuthFile(rel(f)); }),
        auth: all.filter(function (f) { return isAuthFile(rel(f)); })
    };
}

/* settings from web_server_conf.js, as the bundle will start.  The config is
   loaded as a module from the site directory, as web_server_conf.js expects. */
function siteDefaults() {
    var js = 'var c = require(process.scriptPath + "/web_server_conf.js")();' +
             'rampart.utils.printf("%J", {bindAll: c.bindAll, ipAddr: c.ipAddr, ipv6Addr: c.ipv6Addr,' +
             ' bind: c.bind, port: c.port, ipPort: c.ipPort, secure: c.secure, selfSign: c.selfSign,' +
             ' letsencrypt: c.letsencrypt, redirPort: c.redirPort, authMod: c.authMod,' +
             ' monitor: c.monitor || (c.log && c.rotateLogs), error: c.error});';
    var r = u.exec('/bin/sh', {args: ['-c', 'cd "$1" && exec "$2" -c "$3"', 'sh', siteRoot, process.installPathExec, js]});
    var d;
    try { d = JSON.parse(r.stdout); } catch (e) { return {error: (r.stderr || r.stdout || 'could not load it').trim()}; }
    if (d.error) return {error: String(d.error)};
    /* the computed bind list, e.g. ["127.0.0.1:8088", "[::1]:8088"], has the real addresses */
    var v4 = '', v6 = '', port = 0;
    (d.bind || []).forEach(function (b) {
        var m = /^(\[.*\]|[^:]+):(\d+)$/.exec(String(b));
        if (!m) return;
        port = port || parseInt(m[2]);
        if (m[1].charAt(0) == '[') v6 = v6 || m[1]; else v4 = v4 || m[1];
    });
    return {
        bindAll: !!d.bindAll || v4 == '0.0.0.0', ipAddr: v4, ipv6Addr: v6,
        port: port || d.ipPort || d.port, secure: !!d.secure, selfSign: !!d.selfSign,
        letsencrypt: d.letsencrypt || '', redirPort: d.redirPort > 0 ? d.redirPort : 0,
        authMod: !!d.authMod, monitor: !!d.monitor
    };
}

function platform() {
    return String(rampart.buildPlatform || u.exec('uname', '-sm').stdout).replace(/;.*/, '').trim();
}

function writableDir(d) {
    var st = u.stat(d);
    return !!(st && st.isDirectory && st.writable && st.executable);
}

/* defaults and choices for the Build Bundle dialog */
function info() {
    var parent = u.realPath(siteRoot + '/..');
    var home = process.env.HOME || '/tmp';
    var data = [];
    if (u.stat(siteRoot + '/data')) {
        u.readDir(siteRoot + '/data', true).forEach(function (n) {
            if (n == '.' || n == '..') return;
            var st = u.stat(siteRoot + '/data/' + n);
            if (st) data.push({name: n, dir: !!st.isDirectory});
        });
    }
    var defaults = siteDefaults(), js = siteJs();
    var available = u.readDir(process.modulesPath).filter(function (n) { return /^rampart-.*\.(so|js)$/.test(n); })
                     .map(function (n) { return n.replace(/\.(so|js)$/, ''); })
                     .filter(function (n, i, a) { return a.indexOf(n) == i; }).sort();
    return {
        siteRoot: siteRoot,
        outputDir: writableDir(parent) ? parent : home,
        parentWritable: writableDir(parent),
        home: home,
        platform: platform(),
        isMac: isMac,
        defaults: defaults,
        modules: withLinkDeps(withServerNeeds(findModules(js.site), defaults)),  /* {module: [what needs it]} */
        authModules: withLinkDeps(findModules(js.auth)),
        authFiles: authFiles.filter(function (f) { return u.stat(siteRoot + '/' + f); }),
        auth: authUse(defaults, js),
        available: available,
        data: data,
        zip: !!u.exec('sh', '-c', 'command -v zip').stdout.trim()
    };
}

/* problems with an output path, or "" */
function checkOutput(output) {
    if (!output || output.charAt(0) != '/') return "the output must be an absolute path";
    var dir = output.replace(/\/[^\/]*$/, '') || '/';
    var st0 = u.stat(dir);
    if (!st0 || !st0.isDirectory) return "folder " + dir + " does not exist";
    var rdir = u.realPath(dir);
    if (!writableDir(rdir)) return "folder " + rdir + " is not writable";
    if (rdir == siteRoot || rdir.indexOf(siteRoot + '/') == 0)
        return "the bundle cannot be written inside the web server directory";
    var st = u.stat(output);
    if (st && st.isDirectory) return output + " is a folder";
    return "";
}

function build(o) {
    var stage, payload;
    if (!o.name || !/^[A-Za-z0-9._-]+$/.test(o.name)) die("project name may use letters, digits, '.', '_' and '-' only");
    var err = checkOutput(o.output);
    if (err) die(err);
    if (!u.exec('sh', '-c', 'command -v zip').stdout.trim()) die("the 'zip' program is needed to build a bundle");
    if (!/bundleSetup/.test(u.readFile(siteRoot + '/web_server_conf.js', {returnString: true})))
        die("web_server_conf.js does not call bundleSetup(); copy that part from rampart's web_server/web_server_conf.js");

    var wsjs = process.modulesPath + '/rampart-webserver.js';
    if (!u.stat(wsjs) || !/function bundleSetup/.test(u.readFile(wsjs, {returnString: true})))
        die("the rampart running the IDE (" + process.installPathExec + ") is too old to build bundles;" +
            " its rampart-webserver.js has no bundleSetup()");

    var defaults = siteDefaults();
    if (defaults.error) die("web_server_conf.js: " + defaults.error);
    var auth = authUse(defaults, siteJs());
    var includeAuth = auth.needed;

    stage = '/tmp/rampart-bundle-' + process.getpid();
    sh('rm -rf ' + q(stage) + ' && mkdir -p ' + q(stage));
    try {
        u.printf("Staging %s\n", siteRoot);
        var ex = exclude.concat(includeAuth ? [] : authFiles.map(function (f) { return './' + f; }))
                        .map(function (e) { return '--exclude=' + q(e); }).join(' ');
        u.printf("Login system: %s (%s)\n", includeAuth ? "included" : "left out", auth.reason);
        sh('cd ' + q(siteRoot) + ' && tar cf - ' + ex + ' . | (cd ' + q(stage) + ' && tar xpf -)');
        u.mkdir(stage + '/logs');

        /* starting data */
        u.mkdir(stage + '/data');
        (o.dataInclude || []).forEach(function (n) {
            if (n.indexOf('/') != -1 || n == '..' || n == '.') return;
            if (u.stat(siteRoot + '/data/' + n)) {
                sh('cp -pR ' + q(siteRoot + '/data/' + n) + ' ' + q(stage + '/data/'));
                u.printf("  data/%s\n", n);
            }
        });

        /* modules */
        var mods = (o.modules || []).slice();
        ['rampart-server', 'rampart-webserver'].concat(includeAuth ? ['rampart-auth'] : [])
            .forEach(function (m) { if (mods.indexOf(m) == -1) mods.push(m); });
        /* a native module cannot load without the modules it is linked against */
        for (var li = 0; li < mods.length; li++) {
            linkDeps(mods[li]).forEach(function (d) {
                if (mods.indexOf(d) != -1) return;
                mods.push(d);
                u.printf("  adding %s (%s is linked against it)\n", d, mods[li]);
            });
        }
        mods.forEach(function (m) {
            var files = moduleFiles(m);
            if (!files.length) { u.printf("  warning: module %s not found in %s\n", m, process.modulesPath); return; }
            files.forEach(function (f) { sh('cp -p ' + q(f) + ' ' + q(stage + '/')); });
        });
        u.printf("Modules: %s\n", mods.join(', '));
        if (mods.indexOf('rampart-sql') != -1) {
            var tl = process.installPathBin + '/texislockd';
            if (u.stat(tl)) sh('cp -p ' + q(tl) + ' ' + q(stage + '/'));
            else u.printf("  warning: texislockd not found in %s\n", process.installPathBin);
        }

        u.fprintf(stage + '/bundle.json', '%3J\n', {
            name: o.name,
            dataDir: o.dataDir,
            logsDir: o.logsDir,
            seedPolicy: o.seedPolicy == 'missing' ? 'missing' : 'first',
            built: new Date().toISOString(),
            platform: platform(),
            defaults: defaults
        });

        u.printf("Zipping\n");
        payload = stage + '.zip';
        sh('cd ' + q(stage) + ' && zip -qr ' + q(payload) + ' .');

        /* the bare rampart: if this rampart is itself a bundle, drop its payload */
        var exe = u.readFile(process.installPathExec);
        if (u.payloadOffset) exe = exe.subarray(0, u.payloadOffset());
        var raw = o.macWrapper && isMac ? o.output + '.bin' : o.output;
        u.fwrite(raw, exe);
        sh('cat ' + q(payload) + ' >> ' + q(raw) + ' && chmod 755 ' + q(raw));

        if (o.macWrapper && isMac) macWrapper(o.output, raw);
        if (o.id) recordBuild(o.id, o.output);

        var size = u.stat(o.output).size;
        u.printf("\nBuilt %s (%.1f MB) for %s\n", o.output, size / 1048576, platform());
        u.printf("Run it with --help for its options.  Data: %s  Logs: %s\n", o.dataDir, o.logsDir);
    } finally {
        u.shell('rm -rf ' + q(stage) + ' ' + q(stage + '.zip'));
    }
}

/* the bundles the IDE may offer for download: {id: {path, size, mtime, built}}.
   Only the latest few are kept.                                          */
var buildsFile = ideRoot + '/data/bundle-builds.json';

function recordBuild(id, path) {
    if (!/^[A-Za-z0-9]{16,64}$/.test(String(id))) return;
    var builds = {};
    try { builds = JSON.parse(u.readFile(buildsFile, {returnString: true})); } catch (e) {}
    var st = u.stat(path);
    builds[id] = {path: path, size: st.size, mtime: st.mtime.getTime(), built: Date.now()};
    var ids = Object.keys(builds).sort(function (a, b) { return builds[b].built - builds[a].built; });
    ids.slice(10).forEach(function (k) { delete builds[k]; });
    u.fprintf(buildsFile, '%3J\n', builds);
    u.chmod(buildsFile, "600");
}

/* the bundle recorded under id, if it is still the same file, or null */
function builtBundle(id) {
    var builds, b;
    try { builds = JSON.parse(u.readFile(buildsFile, {returnString: true})); } catch (e) { return null; }
    b = builds[String(id)];
    if (!b) return null;
    var st = u.stat(b.path);
    if (!st || !st.isFile || st.size != b.size || st.mtime.getTime() != b.mtime) return null;
    return {path: b.path, name: b.path.replace(/.*\//, ''), size: b.size};
}

/* macOS: a shell wrapper that, on first run, writes <output>.bin beside itself
   without the quarantine flag, truncates itself, and runs it.            */
function macWrapper(out, bin) {
    var marker = '### __RAMPART_BUNDLE_PAYLOAD_FOLLOWS__ ###';
    var script =
'#!/bin/sh\n' +
'# Self-extracting wrapper for a rampart bundle.  The first run writes\n' +
'# <this file>.bin, clears the macOS quarantine flag on it and runs it;\n' +
'# later runs run <this file>.bin directly.\n' +
'set -e\n' +
'SELF="$0"\n' +
'case "$SELF" in /*) ;; *) SELF="$PWD/$SELF" ;; esac\n' +
'SELF="$(cd "$(dirname "$SELF")" && pwd)/$(basename "$SELF")"\n' +
'BIN="$SELF.bin"\n' +
'if [ -x "$BIN" ]; then exec "$BIN" "$@"; fi\n' +
'MARKER="### __RAMPART_BUNDLE_PAYLOAD"\n' +
'MARKER="${MARKER}_FOLLOWS__ ###"\n' +
'OFF="$(grep -aboF -m1 -- "$MARKER" "$SELF" | head -1 | cut -d: -f1)"\n' +
'[ -n "$OFF" ] || { echo "$SELF: bundle is damaged" >&2; exit 1; }\n' +
'HDR=$(( OFF + ${#MARKER} + 1 ))\n' +
'tail -c +"$(( HDR + 1 ))" "$SELF" > "$BIN.tmp.$$"\n' +
'chmod +x "$BIN.tmp.$$"\n' +
'mv "$BIN.tmp.$$" "$BIN"\n' +
'xattr -d com.apple.quarantine "$BIN" 2>/dev/null || true\n' +
'head -c "$HDR" "$SELF" > "$SELF.tmp.$$" && mv "$SELF.tmp.$$" "$SELF" && chmod +x "$SELF"\n' +
'exec "$BIN" "$@"\n' +
marker + '\n';
    var tmp = out + '.wrapper.' + process.getpid();
    u.fprintf(tmp, '%s', script);
    sh('cat ' + q(bin) + ' >> ' + q(tmp) + ' && mv ' + q(tmp) + ' ' + q(out) + ' && chmod 755 ' + q(out) +
       ' && rm -f ' + q(bin));
}

/* modules to include: those something being bundled needs, plus --add-module,
   minus --drop-module.  A "via rampart-x" need only counts while rampart-x is
   in, so dropping a module also drops the ones only it needed.            */
function pickModules(inf, add, drop) {
    var needs = {}, on = {}, changed = true, m;
    function merge(src) { for (var k in src) needs[k] = (needs[k] || []).concat(src[k]); }
    merge(inf.modules);
    if (inf.auth.needed) { merge(inf.authModules); merge({'rampart-auth': ['the login system']}); }
    var names = Object.keys(needs).concat(add).filter(function (n, i, a) { return a.indexOf(n) == i; });
    while (changed) {
        changed = false;
        names.forEach(function (n) {
            var v = add.indexOf(n) != -1 ? true : drop.indexOf(n) != -1 ? false :
                (needs[n] || []).some(function (r) { return !/^via /.test(r) || on[r.substring(4)]; });
            if (!!on[n] != v) { on[n] = v; changed = true; }
        });
    }
    return {list: names.filter(function (n) { return on[n]; }).sort(), needs: needs};
}

function usage() {
    u.printf(
"Build a single-file bundle of this web server (experimental).\n\n" +
"The bundle is one executable holding rampart and everything the site does now:\n" +
"html/, apps/, wsapps/, its configuration and the rampart modules they need.\n" +
"The Mini IDE (ide/, mini-ide-conf.js, start-mini-ide.sh) is not included.\n\n" +
"Usage: rampart ide/lib/bundle-build.js [options]\n\n" +
"  --name NAME            project name (default myproject); names the output file\n" +
"                         and the default data and logs directories\n" +
"  --output FILE          bundle to write (default: next to the web server folder,\n" +
"                         or in your home folder if that is not writable)\n" +
"  --data DIR             data directory the bundle uses (default ~/.rampart/NAME/data)\n" +
"  --logs DIR             logs directory the bundle uses (default ~/.rampart/NAME/logs)\n" +
"  --data-include A,B     entries of data/ to ship as starting data (default: all,\n" +
"                         except auth/ when the login system is not used)\n" +
"  --no-data              ship no starting data\n" +
"  --add-missing-data     on later starts, also copy shipped files missing from the\n" +
"                         data directory (default: only into a new data directory)\n" +
"  --add-module NAME      include a module the scan does not find (repeatable)\n" +
"  --drop-module NAME     leave out a module the scan found (repeatable)\n" +
"  --mac-wrapper          (macOS) add a wrapper that clears the quarantine flag\n" +
"  --force                replace FILE if it exists\n" +
"  --dry-run              show what would be built, and stop\n" +
"  -h, --help             this message\n\n" +
"Modules are found from require() calls in the bundled files.  The login system\n" +
"(apps/auth.js and friends) is included only if authMod is on in\n" +
"web_server_conf.js or the site requires apps/auth.js.  Run the bundle with\n" +
"--help to see how to start it (--port, --host, --tls, ...).\n");
}

/* command line options -> the options build() takes */
function cliOptions(argv) {
    var a = {add: [], drop: []}, i, k, v;
    var takesValue = {'--name': 1, '--output': 1, '--data': 1, '--logs': 1, '--data-include': 1,
                      '--add-module': 1, '--drop-module': 1};
    for (i = 0; i < argv.length; i++) {
        var m = /^(--[a-z-]+)=(.*)$/.exec(argv[i]);
        k = m ? m[1] : argv[i];
        if (k == '-h' || k == '--help') { usage(); process.exit(0); }
        if (takesValue[k]) {
            v = m ? m[2] : argv[++i];
            if (v === undefined) die(k + " needs a value");
        } else if (m) die(k + " does not take a value");
        switch (k) {
            case '--name':          a.name = v; break;
            case '--output':        a.output = v; break;
            case '--data':          a.data = v; break;
            case '--logs':          a.logs = v; break;
            case '--data-include':  a.dataInclude = v.split(',').filter(Boolean); break;
            case '--add-module':    a.add.push(/^rampart-/.test(v) ? v : 'rampart-' + v); break;
            case '--drop-module':   a.drop.push(/^rampart-/.test(v) ? v : 'rampart-' + v); break;
            case '--no-data':       a.dataInclude = []; break;
            case '--add-missing-data': a.missing = true; break;
            case '--mac-wrapper':   a.mac = true; break;
            case '--force':         a.force = true; break;
            case '--dry-run':       a.dry = true; break;
            default: die("unknown option '" + argv[i] + "' (see --help)");
        }
    }

    var inf = info();
    if (inf.defaults.error) die("web_server_conf.js: " + inf.defaults.error);
    var name = a.name || 'myproject';
    var out = a.output ? (a.output.charAt(0) == '/' ? a.output : u.getcwd() + '/' + a.output)
                       : inf.outputDir.replace(/\/$/, '') + '/' + name;
    a.add.concat(a.drop).forEach(function (n) {
        if (inf.available.indexOf(n) == -1) die("module " + n + " is not in " + process.modulesPath);
    });
    var mods = pickModules(inf, a.add, a.drop);
    var dataNames = inf.data.map(function (d) { return d.name; });
    var dataInclude = a.dataInclude ||
        dataNames.filter(function (n) { return n != 'auth' || inf.auth.needed; });
    dataInclude.forEach(function (n) {
        if (dataNames.indexOf(n) == -1) die("data/" + n + " does not exist");
    });

    var o = {
        name: name,
        output: out,
        dataDir: a.data || '~/.rampart/' + name + '/data',
        logsDir: a.logs || '~/.rampart/' + name + '/logs',
        dataInclude: dataInclude,
        seedPolicy: a.missing ? 'missing' : 'first',
        modules: mods.list,
        macWrapper: !!a.mac
    };
    var err = checkOutput(o.output);
    if (err) die(err);
    if (u.stat(o.output) && !a.force && !a.dry) die(o.output + " exists; use --force to replace it");
    if (a.mac && !isMac) u.printf("note: --mac-wrapper only applies when building on macOS\n");

    u.printf("Bundle:        %s  (for %s)\n", o.output, inf.platform);
    u.printf("Data, logs:    %s, %s\n", o.dataDir, o.logsDir);
    u.printf("Starting data: %s%s\n", dataInclude.length ? dataInclude.join(', ') : '(none)',
             dataInclude.length ? (a.missing ? ' (also added to an existing data directory)' : ' (into a new data directory only)') : '');
    u.printf("Login system:  %s (%s)\n", inf.auth.needed ? 'included' : 'left out', inf.auth.reason);
    u.printf("Modules:\n");
    mods.list.forEach(function (n) {
        var why = (mods.needs[n] || ['--add-module']).filter(function (x, i, l) { return l.indexOf(x) == i; });
        u.printf("  %-22s %s\n", n, why.join(', '));
    });
    var d = inf.defaults;
    u.printf("Starts on:     %s, port %s, %s (override with --host, --port, --tls when running it)\n",
             d.bindAll ? 'all interfaces' : [d.ipAddr, d.ipv6Addr].filter(Boolean).join(' and '),
             d.port, d.secure ? 'https' : 'http');
    if (a.dry) process.exit(0);
    u.printf("\n");
    return o;
}

if (module && module.exports) {
    module.exports = {info: info, checkOutput: checkOutput, builtBundle: builtBundle};
} else {
    var arg = process.argv[2] || '';
    var opts;
    if (arg.charAt(0) == '{') {
        try { opts = JSON.parse(arg); } catch (e) { die("bad options JSON: " + e.message); }
    } else {
        opts = cliOptions(process.argv.slice(2));
    }
    build(opts);
}
