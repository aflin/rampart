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

/* An entry may carry `platforms: /regex/` -- a test against PLAT naming
   the ONLY platforms it is built for (the cuNN variants are x86_64-only,
   arm8a is armv7-only).  Entries without the field are universal.  Used
   to keep --list honest and to refuse a wrong-platform install with a
   real message instead of a 404 from the download. */
function platformOK(entry) {
    return !entry || !entry.platforms || entry.platforms.test(PLAT);
}

/* Every langtools flavour: the default package plus the opt-in variants
   that re-point the unsuffixed module symlinks at their own builds
   (cuNN on linux x86_64/arm64, arm8a on armv7). */
var LANGTOOLS_VARIANT_RE = /^rampart-langtools(-cu[0-9]+|-arm8a)?$/;
var LANGTOOLS_OPTIN_RE   = /^rampart-langtools(-cu[0-9]+|-arm8a)$/;

/* The opt-in langtools variants BUILT FOR THIS PLATFORM, in manifest
   order.  Derived from the manifest rather than hard-coded, so adding a
   variant (or changing which platforms it is built for) needs no change
   here -- just its `platforms` regex in packages.js. */
function langtoolsOptins() {
    return Object.keys(manifest).sort().filter(function (k) {
        return LANGTOOLS_OPTIN_RE.test(k) && platformOK(manifest[k]);
    });
}

/* ---------- alias / variant resolution ----------
 * The langtools modules do not ship as individual packages: they come in
 * one tarball that exists in several flavours (cpu/cuNN on linux x86_64
 * and arm64; armv6/armv8-a on armv7).  Someone typing
 * 'rampart --install rampart-llamacpp' does not necessarily know which
 * one they want, so offer the choice wherever there IS one, and remap
 * silently where the platform has only the default build (mac, freebsd,
 * legacy raspi).
 */
function resolveLangtoolsAlias(name) {
    var langAliases = { "rampart-llamacpp": 1, "rampart-faiss": 1,
                        "rampart-sentencepiece": 1, "rampart-clip": 1,
                        "rampart-onnx": 1 };
    if (!langAliases[name]) return name;

    var optins = langtoolsOptins();
    if (!optins.length) return "rampart-langtools";   /* nothing to choose */

    printf("\n%s is part of the langtools bundle.  Which variant?\n", name);
    printf("  1) %-26s %s\n", "rampart-langtools",
           "the default build for this platform");
    /* single-keystroke menu, so never offer more than 1..9 */
    var shown = (optins.length > 8) ? 8 : optins.length;
    for (var i = 0; i < shown; i++)
        printf("  %d) %-26s %s\n", i + 2, optins[i],
               (manifest[optins[i]] || {}).notes || "");
    printf("[1] > ");
    stdout.fflush();
    var c;
    try { c = stdin.getchar(1); } catch (e) { c = null; }
    if (c === null || c === undefined || c === false || c === "") {
        printf("\nCancelled.\n"); process.exit(0);
    }
    printf("\n");
    var pick = parseInt(c, 10);
    if (pick >= 2 && pick <= shown + 1) return optins[pick - 2];
    return "rampart-langtools";   /* default / "1" / Enter / anything else */
}

/* ---------- pre-extract cleanup for langtools variants ----------
 * Every langtools tarball (cpu / cu11 / cu12 / cu13 / arm8a) ships the
 * unsuffixed symlinks modules/rampart-llamacpp.so,
 * modules/rampart-faiss.so and modules/rampart-clip.so pointing at its
 * own variant-specific .so.  Before extracting a new variant we need to
 * clear those unsuffixed names so the tarball's symlinks land cleanly:
 *
 *   - symlink present  -> rm it (it was ours from a prior install)
 *   - regular file     -> mv to <name>.so.bak (preserve in case the
 *                         user hand-installed something there; not
 *                         our place to delete it)
 *
 * We do NOT touch the suffixed .so files (rampart-faiss_cpu.so,
 * rampart-llamacpp_cu11.so, etc.) -- those belong to whichever
 * variant installed them and stay put so the user can switch back
 * without re-downloading.  Likewise rampart-sentencepiece.so is
 * shared and never removed here.
 *
 * Manifest hygiene: any *other* rampart-langtools{,-cuNN,-arm8a} entry
 * in installed.json may still list the unsuffixed paths as its own.
 * Strip those paths from its file list so uninstalling that entry
 * later doesn't rm the live symlinks out from under the variant
 * we're about to install.  Suffixed-file ownership is left intact.
 *
 * No-op for non-langtools packages. */
function langtoolsPreExtract(name, newEntries) {
    if (!LANGTOOLS_VARIANT_RE.test(name)) return;
    var ut = rampart.utils;

    var unsuffixed = ["rampart-llamacpp.so", "rampart-faiss.so",
                      "rampart-clip.so"];

    /* clear or back up the unsuffixed names in the live tree */
    unsuffixed.forEach(function (fn) {
        var p = PREFIX + "/modules/" + fn;
        var st;
        try { st = ut.lstat(p); } catch (e) { st = null; }
        if (!st) return;
        if (st.isSymbolicLink) {
            run("rm", ["-f", p]);
            info("  cleared stale symlink: modules/" + fn);
            return;
        }
        var bak = p + ".bak";
        var n = 1;
        while (fileExists(bak)) { n++; bak = p + ".bak." + n; }
        run("mv", [p, bak]);
        info("  preserved existing file: modules/" + fn +
             " -> " + basename(bak));
    });

    /* Manifest hygiene: this install OWNS every path it ships.  Strip
       those paths from any OTHER langtools variant's file list so two
       entries never claim the same file.
       Several files are shipped by EVERY langtools variant --
       rampart-sentencepiece.so, rampart-onnx.so, rampart-models.js,
       plus the unsuffixed faiss/llamacpp symlinks.  Without this,
       `--install all` (cpu) followed by `--install rampart-langtools-cu12`
       leaves both entries claiming them, and a later uninstall of one
       would rm the files out from under the other.
       The old entry keeps its UNIQUE files (its own _cuNN.so binaries),
       so it still uninstalls cleanly and you can switch back to it
       without re-downloading. */
    var manifestPath = PREFIX + "/installed.json";
    if (!fileExists(manifestPath)) return;
    var live;
    try { live = JSON.parse(readFile(manifestPath, true)); }
    catch (e) { return; }
    if (!live || !live.packages) return;

    var rmPaths = {};
    function claim(rel) {
        rmPaths[PREFIX + "/" + rel] = 1;   /* v2 absolute */
        rmPaths[rel]                = 1;   /* v1 relative */
    }
    /* every file the incoming tarball ships */
    (newEntries || []).forEach(function (e) {
        if (e.charAt(e.length - 1) === "/") return;   /* skip dir entries */
        claim(e);
    });
    /* plus the unsuffixed symlinks (the tarball may or may not carry them) */
    unsuffixed.forEach(function (fn) { claim("modules/" + fn); });

    var dirty = false;
    Object.keys(live.packages).forEach(function (k) {
        if (k === name) return;
        if (!LANGTOOLS_VARIANT_RE.test(k)) return;
        var pkg = live.packages[k];
        if (!pkg || !pkg.files) return;
        var before = pkg.files.length;
        pkg.files = pkg.files.filter(function (f) { return !rmPaths[f]; });
        if (pkg.files.length !== before) {
            dirty = true;
            info("  released " + (before - pkg.files.length) +
                 " shared file(s) from prior variant: " + k);
        }
    });
    if (dirty) fwrite(manifestPath, JSON.stringify(live, null, 2) + "\n");
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

    if (!platformOK(entry))
        fail(name + " is not built for this platform (" + PLAT + ").  " +
             "See `rampart --install --list` for what is available here.");

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

    /* installedFiles holds ABSOLUTE paths (schema v2).  A trailing slash
       marks a directory entry that uninstall rm -rf's as a whole -- used
       for rampart-python, where the tree has ~10k files we don't need
       to enumerate individually.  Anything else is a regular file. */
    var installedFiles = [];

    if (entry.kind === "so" || entry.kind === "js") {
        /* drop the file under modules/, stripping the version+os suffix */
        var ext = (entry.kind === "so") ? ".so" : ".js";
        var basenameOut = name + ext;
        var dst = PREFIX + "/modules/" + basenameOut;
        run("cp", ["-p", localArtifact, dst]);
        installedFiles.push(dst);
        info("  installed: " + dst);
    } else {
        /* tar.gz -> extract under PREFIX, list extracted entries */
        var listOut = run("tar", ["-tzf", localArtifact]).stdout;
        var entries = listOut.split("\n").filter(function (s) { return s.length; });
        /* clear stale langtools symlinks / preserve hand-installed
           files, and hand ownership of shared files to this install
           (no-op for non-langtools packages) */
        langtoolsPreExtract(name, entries);
        run("tar", ["-xzf", localArtifact, "-C", PREFIX]);
        if (name === "rampart-python") {
            /* Special case: the embedded Python 3.11 tree under
               modules/python/ has thousands of files (stdlib,
               site-packages, headers, share/, etc.).  Tracking each
               bloats installed.json by ~250KB.  Record the directory
               itself as one bulk entry (uninstall rm -rf's it).
               BUT -- the rampart-python tarball also ships top-level
               shims like bin/python3r and bin/pip3r that live OUTSIDE
               modules/python/.  Those need to be tracked individually
               or uninstall leaves them stranded.  So we collapse only
               the modules/python/ subtree; everything else stays
               per-file. */
            var outside = entries
                .filter(function (e) {
                    if (e.charAt(e.length-1) === "/") return false;
                    return !/^modules\/python\//.test(e);
                })
                .map(function (rel) { return PREFIX + "/" + rel; });
            installedFiles = outside.concat([PREFIX + "/modules/python/"]);
            info("  extracted Python tree into " + PREFIX + "/modules/python/" +
                 " (" + outside.length + " extra files outside the tree)");
        } else {
            installedFiles = entries
                .filter(function (e) { return e.charAt(e.length-1) !== "/"; })
                .map(function (rel) { return PREFIX + "/" + rel; });
            /* rampart-graphicsmagick ships codec/filter modules at
               share/graphicsmagick/modules-Q16/{coders,filters}/<name>.so .
               GraphicsMagick's module loader uses libltdl, which loads
               <name>.la (NOT <name>.so directly).  libltdl's .la parser
               requires single-quoted values; without a .la file (or with
               one that has the wrong libdir baked in from the build host)
               it fails silently with "(null)" as the dlerror.  Generate a
               stub .la for each .so at install time, with libdir set to
               the actual directory the .so lives in -- works for both
               system installs (/usr/local/rampart) and user installs
               ($HOME/.rampart) without any placeholders to substitute. */
            if (name === "rampart-graphicsmagick") {
                var ut = rampart.utils;
                ["coders","filters"].forEach(function (sub) {
                    var dir = PREFIX + "/share/graphicsmagick/modules-Q16/" + sub;
                    var ents;
                    try { ents = ut.readdir(dir) || []; } catch (e) { return; }
                    var count = 0;
                    ents.forEach(function (f) {
                        if (!/\.so$/.test(f)) return;
                        var base = f.replace(/\.so$/, "");
                        var la =
                            "dlname='" + base + ".so'\n" +
                            "library_names='" + base + ".so'\n" +
                            "old_library=''\n" +
                            "libdir='" + dir + "'\n" +
                            "installed=yes\n" +
                            "shouldnotlink=no\n";
                        var laPath = dir + "/" + base + ".la";
                        ut.writeFile(laPath, la);
                        installedFiles.push(laPath);
                        count++;
                    });
                    if (count) info("  wrote " + count + " .la stubs in " +
                                    "share/graphicsmagick/modules-Q16/" + sub + "/");
                });
            }
            info("  extracted " + installedFiles.length + " files into " + PREFIX);
        }
    }

    /* update installed.json */
    var manifestPath = PREFIX + "/installed.json";
    var live;
    if (fileExists(manifestPath)) {
        try { live = JSON.parse(readFile(manifestPath, true)); }
        catch (e) { fail("could not parse " + manifestPath + ": " + e.message); }
    } else {
        live = {
            schema:          2,
            rampart_version: rampart.version,
            installed_at:    new Date().toISOString(),
            prefix:          PREFIX,
            profile:         (PREFIX.indexOf("/usr/local") === 0) ? "system" : "user",
            packages:        {},
            rc_files:        [],
            symlinks:        []
        };
    }
    /* Upgrade existing v1 manifest in place on next write -- future
       packages get tracked with absolute paths.  Old v1 entries stay
       relative until they're re-installed, which is fine: uninstall.js
       handles both forms. */
    if (live.schema !== 2) live.schema = 2;

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

    /* Partition manifest into installable vs bundled, dropping anything
       not built for THIS platform (the cuNN langtools variants off
       x86_64, the arm8a one off armv7) -- listing a package whose
       download does not exist here is worse than not listing it. */
    var bundled = [], avail = [];
    Object.keys(manifest).sort().forEach(function (name) {
        var e = manifest[name];
        if (!platformOK(e)) return;
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
    info("  rampart-llamacpp, rampart-faiss, rampart-sentencepiece," +
         " rampart-clip, rampart-onnx");
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
   and skips the opt-in langtools variants -- cuda, and arm8a on armv7 --
   since they'd overwrite the default rampart-langtools symlinks.  The
   default is the one that runs everywhere on the platform (cpu; armv6 on
   armv7), so `all` can never leave a machine with modules its CPU cannot
   execute; install the faster variant explicitly when you want it).
   Targets are de-duped so `--install all rampart-redis` still does the
   right thing. */
if (TARGETS.indexOf("all") !== -1) {
    var _all = [];
    Object.keys(manifest).sort().forEach(function (n) {
        var e = manifest[n];
        if (e.in_bundle) return;
        if (LANGTOOLS_OPTIN_RE.test(n)) return;
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
    info("(skipping the opt-in langtools variants -- install " +
         "rampart-langtools-cuNN for CUDA, or rampart-langtools-arm8a " +
         "on a Pi 3+.)");
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
