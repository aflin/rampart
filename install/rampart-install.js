/*
 * install/rampart-install-pkg.js  --  prototype of `rampart --install`.
 *
 * Eventually this logic moves into the rampart binary itself (gated on
 * rampart.buildPlatform carrying an official NAME;... prefix).  For now
 * it runs as a standalone JS so we can simulate the install lifecycle
 * against locally-produced package artifacts.
 *
 * Usage:
 *   rampart rampart-install-pkg.js <pkg> [<pkg> ...] \
 *           [--prefix <prefix>] \
 *           [--from <local-pkg-dir-or-URL>] \
 *           [--force]
 *
 *   <pkg>     one or more package names (e.g. rampart-redis).
 *   --prefix  install destination; default $HOME/.rampart, or
 *             /usr/local/rampart if running as root.
 *   --from    local directory (file:// style) or http URL whose
 *             /packages/ subtree holds the artifacts.  Default:
 *             https://rampart.dev/downloads/rampart-<ver>/<plat>/
 *             (but if --from is omitted we'll print the URL we'd fetch
 *             and stop, since this prototype doesn't do HTTPS itself).
 *   --force   reinstall even if installed.json records a matching sha1.
 *
 * Lifecycle for one package:
 *   1. Look up the package in install/packages.js.
 *   2. Refuse if rampart.buildPlatform lacks the NAME;... prefix
 *      (unofficial build -- packages may be incompatible).
 *   3. Compute the URL/path for the artifact based on kind/arch.
 *   4. Fetch the .sha1; if installed.json[name].sha1 matches AND
 *      --force was not passed, skip the install.
 *   5. Fetch the artifact, sha1-verify against the fetched .sha1.
 *   6. Single-file kind -> drop at <prefix>/modules/<filename>.
 *      tar.gz kind     -> extract under <prefix>/.
 *   7. Update <prefix>/installed.json: append a package entry that
 *      now includes the artifact's sha1 for future skip checks.
 */

rampart.globalize(rampart.utils);

/* ---------- helpers ---------- */

function _trim(s) {
    if (s == null || s === false) return "";
    return (""+s).replace(/^\s+|\s+$/g, "");
}

function info(msg) { printf("%s\n", msg); }
function warn(msg) { fprintf(stderr, "WARN: %s\n", msg); }
function fail(msg) { fprintf(stderr, "ERROR: %s\n", msg); process.exit(1); }

function fileExists(p) {
    try { return !!stat(p); } catch (e) { return false; }
}

function run(cmd, args) {
    var r = exec.apply(null, [cmd].concat(args || []));
    if (!r || r.exitStatus !== 0) {
        fail(cmd + " " + (args || []).join(" ") + " -> exit " +
             (r ? r.exitStatus : "?") + "\n" + (r ? r.stderr : ""));
    }
    return r;
}

function basename(p) { var i = p.lastIndexOf("/"); return i < 0 ? p : p.substring(i+1); }

/* ---------- arg parse ---------- */

var argv = process.argv;
var TARGETS = [];
var PREFIX  = null;
var FROM    = null;
var FORCE   = false;
var LIST    = false;

for (var i = 2; i < argv.length; i++) {
    var a = argv[i];
    if (a === "--prefix")    PREFIX = argv[++i];
    else if (a === "--from") FROM   = argv[++i];
    else if (a === "--force") FORCE = true;
    else if (a === "--list" || a === "-l") LIST = true;
    else if (a.charAt(0) === "-")
        fail("unknown flag: " + a);
    else TARGETS.push(a);
}

if (!LIST && !TARGETS.length)
    fail("Usage: rampart --install <pkg> [<pkg> ...] [--prefix PATH] [--from DIR|URL] [--force]\n" +
         "       rampart --install all\n" +
         "       rampart --install --list");

/* default prefix: install into the same tree this rampart binary lives
   in.  process.installPath is the binary's directory minus the trailing
   /bin (set in src/duktape/globals/rampart-utils.c).  This means
   `rampart --install pkg` always lands packages alongside the rampart
   that's running it -- whether that's /usr/local/rampart (root or a
   user who owns the prefix), $HOME/.rampart (user install),
   /opt/rampart, or anywhere else.  Fall back to the old uid-based
   default if installPath isn't usable (e.g. running from a checkout
   build/src/ with no /bin in the path). */
if (PREFIX == null) {
    if (process.installPath && stat(process.installPath + "/bin/rampart")) {
        PREFIX = process.installPath;
    } else {
        var uid = parseInt(_trim((exec("id","-u") || {stdout:"-1"}).stdout), 10);
        PREFIX = (uid === 0) ? "/usr/local/rampart"
                             : (process.env.HOME + "/.rampart");
    }
}

/* ---------- official-build gate ---------- */

var platStr = (rampart.buildPlatform || "").split(";")[0];
platStr = _trim(platStr);
/* Match raw `uname -s` output (e.g. "FreeBSD 14.3-RELEASE ...") --
   the OS word followed by whitespace or end of string.  Official
   platform names like "FreeBSD-14" or "raspberry_pi_os-bullseye-..."
   share the leading word but have a dash or letter next, so they
   pass the gate. */
if (platStr === "" || /^(Linux|Darwin|FreeBSD)(\s|$)/.test(platStr)) {
    fail("rampart was not built for distribution (rampart.buildPlatform" +
         " is '" + rampart.buildPlatform + "').  rampart --install only" +
         " works on official builds whose platform string is hand-coded.");
}
var PLAT = platStr;

/* ---------- defaults ---------- */
/* FROM is a build-time placeholder substituted by mkrp at bundle time:
 *   production bundle -> https://rampart.dev/downloads/rampart-<ver>/<plat>/packages
 *   testing bundle    -> https://rampart.dev/downloads/testing/<plat>/packages
 * Override per-invocation with --from <url-or-dir>.
 */

if (FROM == null) {
    FROM = "@@FROM_URL@@";
}

/* ---------- load manifest ---------- */

var manifest = require(process.scriptPath + "/packages.js");

/* ---------- alias / variant resolution ----------
 * On debian12-x86_64 the langtools tarball ships in two flavours
 * (rampart-langtools = cpu, rampart-langtools-cuda = cuda).  Users
 * who type 'rampart --install rampart-llamacpp' (or rampart-faiss)
 * don't necessarily know which one they want; ask.  On other
 * platforms there's only one variant, so just remap silently.
 */
function resolveLangtoolsAlias(name) {
    var langAliases = { "rampart-llamacpp": 1, "rampart-faiss": 1, "rampart-sentencepiece": 1 };
    if (!langAliases[name]) return name;

    if (PLAT === "debian12-x86_64") {
        printf("\n%s is part of the langtools bundle.  Which variant?\n", name);
        printf("  1) cpu  (rampart-langtools)\n");
        printf("  2) cuda (rampart-langtools-cuda)\n");
        printf("[1] > ");
        stdout.fflush();
        var c;
        try { c = stdin.getchar(1); } catch (e) { c = null; }
        if (c === null || c === undefined || c === false || c === "") {
            printf("\nCancelled.\n"); process.exit(0);
        }
        printf("\n");
        if (c === "2") return "rampart-langtools-cuda";
        return "rampart-langtools";   /* default / "1" / Enter */
    }
    /* single-variant platforms: just install rampart-langtools */
    return "rampart-langtools";
}

/* ---------- artifact-name helpers ---------- */

function artifactName(name, entry) {
    var archDep = (entry.arch === "dep");
    if (entry.kind === "so")
        return name + "-" + rampart.version + (archDep ? "-" + PLAT : "") + ".so";
    if (entry.kind === "js")
        return name + "-" + rampart.version + (archDep ? "-" + PLAT : "") + ".js";
    if (entry.kind === "tar.gz")
        return name + "-" + rampart.version + (archDep ? "-" + PLAT : "") + ".tar.gz";
    fail("unknown kind for " + name + ": " + entry.kind);
}

/* ---------- fetch helper (http or local) ---------- */

function isURL(s) { return /^https?:\/\//.test(s); }

/* Fetch src into dst on disk.  src can be a local path or an http(s) URL. */
function fetchTo(src, dst) {
    if (!isURL(src)) {
        if (!fileExists(src)) fail("artifact not found at " + src);
        run("cp", ["-p", src, dst]);
        return;
    }
    var curl = require("rampart-curl");
    var r;
    try { r = curl.fetch(src); }
    catch (e) { fail("download failed: " + src + " (" + e.message + ")"); }
    if (!r || r.status !== 200)
        fail("download failed: " + src + " (status " + (r && r.status) + ")");
    fwrite(dst, r.body);
}

/* ---------- per-package install ---------- */

function installOne(name) {
    name = resolveLangtoolsAlias(name);

    var entry = manifest[name];
    if (!entry) fail("unknown package: " + name);

    if (entry.in_bundle) {
        info("[" + name + "] already installed via the rampart-install bundle, skipping");
        return null;
    }

    if (entry.deps && entry.deps.length) {
        info("[" + name + "] documented deps: " + entry.deps.join(", ") +
             " (no auto-resolution yet)");
    }

    var aname  = artifactName(name, entry);
    var src    = FROM + "/" + aname;
    var shaSrc = src + ".sha1";

    /* Stage downloads in a process-local tmp dir; clean up before return. */
    var tmpdir = "/tmp/rampart-install-" + process.getpid() + "-" + name;
    run("rm", ["-rf", tmpdir]);
    mkdir(tmpdir, true);

    /* Fetch the (cheap) sha1 first.  If it matches what installed.json
       already records for this package, skip the artifact download
       entirely -- unless --force was passed. */
    var localSha = tmpdir + "/" + aname + ".sha1";
    var haveSha = true;
    try { fetchTo(shaSrc, localSha); }
    catch (e) { haveSha = false; warn("no sha1 file for " + aname); }

    var remoteSha = null;
    if (haveSha) {
        remoteSha = _trim(readFile(localSha, true).split(/\s+/)[0]);
    }

    if (!FORCE && remoteSha) {
        var manifestPath0 = PREFIX + "/installed.json";
        if (fileExists(manifestPath0)) {
            var live0;
            try { live0 = JSON.parse(readFile(manifestPath0, true)); }
            catch (e) { live0 = null; }
            var prev = live0 && live0.packages && live0.packages[name];
            if (prev && prev.sha1 === remoteSha) {
                info("  already at sha1 " + remoteSha.substring(0,12) +
                     "... (use --force to reinstall)");
                try { run("rm", ["-rf", tmpdir]); } catch (e) {}
                return null;
            }
        }
    }

    var localArtifact = tmpdir + "/" + aname;
    info("  fetching " + src);
    fetchTo(src, localArtifact);

    if (haveSha) {
        /* In-process SHA-1 via rampart-crypto -- avoids the
           Linux-only `sha1sum` shell-out (macOS only has `shasum`,
           FreeBSD has `sha1`; rampart-crypto works everywhere
           rampart does). */
        var actual = require("rampart-crypto").sha1(readFile(localArtifact, false));
        if (remoteSha !== actual)
            fail("sha1 mismatch for " + aname + ": expected " + remoteSha +
                 ", got " + actual);
        info("  sha1 OK (" + actual.substring(0,12) + "...)");
    }

    mkdir(PREFIX, true);
    mkdir(PREFIX + "/modules", true);
    mkdir(PREFIX + "/bin", true);

    var installedFiles = [];

    if (entry.kind === "so" || entry.kind === "js") {
        /* drop the file under modules/, stripping the version+os suffix */
        var ext = (entry.kind === "so") ? ".so" : ".js";
        var basenameOut = name + ext;
        var dst = PREFIX + "/modules/" + basenameOut;
        run("cp", ["-p", localArtifact, dst]);
        installedFiles.push("modules/" + basenameOut);
        info("  installed: " + dst);
    } else {
        /* tar.gz -> extract under PREFIX, list extracted entries */
        var listOut = run("tar", ["-tzf", localArtifact]).stdout;
        var entries = listOut.split("\n").filter(function (s) { return s.length; });
        run("tar", ["-xzf", localArtifact, "-C", PREFIX]);
        installedFiles = entries.filter(function (e) { return e.charAt(e.length-1) !== "/"; });
        info("  extracted " + installedFiles.length + " files into " + PREFIX);
    }

    /* update installed.json */
    var manifestPath = PREFIX + "/installed.json";
    var live;
    if (fileExists(manifestPath)) {
        try { live = JSON.parse(readFile(manifestPath, true)); }
        catch (e) { fail("could not parse " + manifestPath + ": " + e.message); }
    } else {
        live = {
            schema:          1,
            rampart_version: rampart.version,
            installed_at:    new Date().toISOString(),
            prefix:          PREFIX,
            profile:         (PREFIX.indexOf("/usr/local") === 0) ? "system" : "user",
            packages:        {},
            rc_files:        [],
            symlinks:        []
        };
    }

    live.packages = live.packages || {};
    live.packages[name] = {
        installed_by: "rampart --install",
        installed_at: new Date().toISOString(),
        version:      rampart.version,
        source:       src,
        sha1:         remoteSha,
        files:        installedFiles
    };
    fwrite(manifestPath, JSON.stringify(live, null, 2) + "\n");
    info("  manifest updated: " + manifestPath);

    /* clean staged downloads */
    try { run("rm", ["-rf", tmpdir]); } catch (e) {}

    return installedFiles;
}

/* ---------- list mode ---------- */

function showList() {
    /* Which packages are already installed locally? */
    var installed = {};
    var mp = PREFIX + "/installed.json";
    if (fileExists(mp)) {
        try {
            var live = JSON.parse(readFile(mp, true));
            if (live && live.packages) {
                Object.keys(live.packages).forEach(function (k) {
                    if (k !== "core") installed[k] = true;
                });
            }
        } catch (e) { /* missing/corrupt -> show nothing as installed */ }
    }

    /* Partition manifest into installable vs bundled. */
    var bundled = [], avail = [];
    Object.keys(manifest).sort().forEach(function (name) {
        var e = manifest[name];
        (e.in_bundle ? bundled : avail).push({name: name, entry: e});
    });

    function kindTag(k) {
        if (k === "so")     return ".so";
        if (k === "js")     return ".js";
        if (k === "tar.gz") return "tar.gz";
        return k || "?";
    }

    /* shorten long notes for the table */
    function shortNote(n) {
        if (!n) return "";
        n = (""+n).replace(/\s+/g, " ").trim();
        if (n.length > 50) n = n.substring(0, 47) + "...";
        return n;
    }

    info("Installable via  rampart --install <name>:");
    info("");
    printf("  %-28s%-8s%s\n", "Name", "Kind", "Notes");
    printf("  %-28s%-8s%s\n", "----", "----", "-----");
    avail.forEach(function (p) {
        var tags = [];
        if (installed[p.name]) tags.push("* installed");
        var note = shortNote(p.entry.notes);
        if (note) tags.push(note);
        printf("  %-28s%-8s%s\n", p.name, kindTag(p.entry.kind), tags.join("; "));
    });

    info("");
    info("Already in the lean install (no --install needed):");
    info("");
    /* word-wrap at ~68 cols */
    var line = "";
    bundled.forEach(function (p) {
        var n = p.name;
        if (line.length + n.length + 2 > 68) { info("  " + line.replace(/, $/, "")); line = ""; }
        line += n + ", ";
    });
    if (line) info("  " + line.replace(/, $/, ""));

    info("");
    info("Aliases (auto-resolve to rampart-langtools):");
    info("  rampart-llamacpp, rampart-faiss, rampart-sentencepiece");
    info("");
    info("Install:          rampart --install <name> [<name>...]");
    info("Install all:      rampart --install all   (everything except `test`)");
    info("Force reinstall:    add --force");
    info("Custom prefix:      add --prefix <path>");
}

/* ---------- run ---------- */

if (LIST) {
    showList();
    process.exit(0);
}

/* "all" expands to every installable package (skips in_bundle entries
   and skips rampart-langtools-cuda since it would overwrite the
   rampart-langtools symlinks -- install the CUDA variant explicitly
   when you want it).  Targets are de-duped so `--install all rampart-redis`
   still does the right thing. */
if (TARGETS.indexOf("all") !== -1) {
    var _all = [];
    Object.keys(manifest).sort().forEach(function (n) {
        var e = manifest[n];
        if (e.in_bundle) return;
        if (n === "rampart-langtools-cuda") return;
        if (n === "test") return;             /* test ships rampart's
                                                 own test suite; not
                                                 part of `--install all` */
        _all.push(n);
    });
    var _seen = {};
    var _expanded = [];
    TARGETS.forEach(function (t) {
        if (t === "all") {
            _all.forEach(function (n) {
                if (!_seen[n]) { _seen[n] = true; _expanded.push(n); }
            });
        } else if (!_seen[t]) {
            _seen[t] = true; _expanded.push(t);
        }
    });
    TARGETS = _expanded;
    info("Expanding 'all' to " + _all.length + " packages:");
    info("  " + _all.join(", "));
    info("(skipping rampart-langtools-cuda; install it separately for CUDA.)");
    info("");
}

info("Install destination: " + PREFIX);
info("Platform:            " + PLAT);
info("Source:              " + FROM);
info("");

for (var ti = 0; ti < TARGETS.length; ti++) {
    info("Installing " + TARGETS[ti] + "...");
    installOne(TARGETS[ti]);
    info("");
}

info("Done.");
