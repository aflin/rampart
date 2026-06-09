/*
 * install/build-packages.js -- iterate install/packages.js and produce
 * one artifact per non-bundled entry under <outdir>/.
 *
 * Naming on output (matches what rampart --install will look up):
 *   <pkg>-<ver>-<os>.so          (kind:"so",  arch:"dep")
 *   <pkg>-<ver>.js               (kind:"js",  arch:"indep")
 *   <pkg>-<ver>-<os>.tar.gz      (kind:"tar.gz", arch:"dep")
 *   <pkg>-<ver>.tar.gz           (kind:"tar.gz", arch:"indep")
 * Plus a sibling .sha1 for each.
 *
 *
 * Usage:
 *   rampart install/build-packages.js <version> <os> <outdir> \
 *           [--source <prefix>] [--quiet]
 *
 *   <version>   e.g. 0.7.0 (auto-detected from rampart.version if "-")
 *   <os>        e.g. debian12-x86_64
 *   <outdir>    where to put artifacts; created if missing
 *   --source    where to read files from (default /usr/local/rampart)
 *   --quiet     suppress per-file chatter
 *
 * Exit non-zero if any required file is missing from <source>.
 */

rampart.globalize(rampart.utils);

/* ---------- arg parse ---------- */

var argv = process.argv;
if (argv.length < 5) {
    fprintf(stderr,
        "Usage: rampart build-packages.js <version> <os> <outdir>" +
        " [--source DIR] [--quiet]\n");
    process.exit(1);
}

var VERSION = argv[2];
var OS      = argv[3];
var OUTDIR  = argv[4];
var SOURCE  = "/usr/local/rampart";
var QUIET   = false;

for (var ai = 5; ai < argv.length; ai++) {
    if (argv[ai] === "--source") SOURCE = argv[++ai];
    else if (argv[ai] === "--quiet") QUIET = true;
    else { fprintf(stderr, "unknown arg: %s\n", argv[ai]); process.exit(1); }
}
if (VERSION === "-") VERSION = rampart.version;

mkdir(OUTDIR, true);

var packages = require(process.scriptPath + "/packages.js");

/* ---------- helpers ---------- */

function info(msg) { if (!QUIET) printf("%s\n", msg); }

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

function sha1OfFile(path) {
    var r = run("sha1sum", [path]);
    return r.stdout.split(/\s+/)[0];
}

/* ---------- per-kind producers ---------- */

function suffixedName(name, kind, archDep, ext) {
    var arch = archDep ? "-" + OS : "";
    return name + "-" + VERSION + arch + "." + ext;
}

function copySingle(name, entry, ext) {
    var src = SOURCE + "/" + entry.files[0];
    if (!fileExists(src)) {
        info("  SKIP " + name + " (source missing: " + src + ")");
        return null;
    }
    var dst = OUTDIR + "/" + suffixedName(name, entry.kind, entry.arch === "dep", ext);
    run("cp", ["-p", src, dst]);
    run("sh", ["-c", "cd " + OUTDIR + " && sha1sum " +
               dst.substring(OUTDIR.length+1) + " > " +
               dst.substring(OUTDIR.length+1) + ".sha1"]);
    info(sprintf("  built  %-50s (%d bytes)", basename(dst), stat(dst).size));
    return dst;
}

function basename(p) { var i = p.lastIndexOf("/"); return i < 0 ? p : p.substring(i+1); }

/* SONAMES that ship in the OS base (Linux + FreeBSD) and never need
   bundling.  Match by SONAME (the first column of ldd output). */
var BASE_LIB_SONAMES = {
    /* Linux glibc base */
    "linux-vdso.so.1": 1, "libc.so.6": 1, "libm.so.6": 1,
    "libdl.so.2": 1, "libpthread.so.0": 1, "librt.so.1": 1,
    "libresolv.so.2": 1, "libgcc_s.so.1": 1, "libstdc++.so.6": 1,
    "libz.so.1": 1,
    "ld-linux-x86-64.so.2": 1, "ld-linux-aarch64.so.1": 1,
    /* FreeBSD base */
    "libc.so.7": 1, "libm.so.5": 1, "libthr.so.3": 1,
    "libutil.so.9": 1, "libelf.so.2": 1, "libdl.so.1": 1,
    "libc++.so.1": 1, "libcxxrt.so.1": 1, "libkvm.so.7": 1,
    "libz.so.6": 1,
    "[vdso]": 1,
};

/* SONAMES already shipped in the install bundle's <prefix>/lib/
   (via build-installer.sh's SYSTEM_LIBS list).  Packages can rely on
   these being present and skip duplicating them. */
var BUNDLE_SHIPPED_SONAMES = {
    "libopenblas.so.0": 1, "libgfortran.so.5": 1,
    "libgomp.so.1": 1,     "libquadmath.so.0": 1,
};

/* macOS detection: process.platform isn't always set in older rampart.
   Probe by checking for /System/Library which only exists on macOS. */
function isMacOS() { return fileExists("/System/Library/CoreServices"); }

/* macOS system paths whose libs we never bundle. */
var MAC_SYSTEM_PREFIXES = [
    "/usr/lib/", "/System/", "/Library/Apple/",
];

/* Collect transitive shared-lib deps for the .so files listed in `files`.
   Returns [{soname, path}] for every dep that needs to be bundled.
   On Linux/FreeBSD: uses ldd (gives the full transitive closure in one
     call) and filters by SONAME.
   On macOS: uses otool -L (direct deps only) + BFS to walk the closure
     ourselves, filtering by absolute install-name path. */
function collectSoDeps(stage, files) {
    var deps = {};

    if (isMacOS()) {
        /* macOS: BFS through transitive deps via otool */
        var visited = {};
        var queue = [];
        for (var i = 0; i < files.length; i++) {
            var rel = files[i];
            if (!/\.(so|dylib)(\.[0-9.]+)?$/.test(rel)) continue;
            var src = stage + "/" + rel;
            if (fileExists(src)) queue.push(src);
        }
        while (queue.length) {
            var f = queue.shift();
            if (visited[f]) continue;
            visited[f] = true;
            var o = exec("otool", "-L", f);
            if (!o || o.exitStatus !== 0) continue;
            /* otool output:
               /path/to/file:
                   /opt/homebrew/lib/libfoo.dylib (compatibility version ..., current version ...)
                   /usr/lib/libSystem.B.dylib (...)
            */
            var lines = (o.stdout || "").split("\n").slice(1);
            for (var li = 0; li < lines.length; li++) {
                var m = lines[li].match(/^\s+(\S.*?)\s+\(compatibility/);
                if (!m) continue;
                var fullPath = m[1];
                if (fullPath[0] === "@") continue;  /* already rpath'd */
                var skip = false;
                for (var pi = 0; pi < MAC_SYSTEM_PREFIXES.length; pi++) {
                    if (fullPath.indexOf(MAC_SYSTEM_PREFIXES[pi]) === 0) { skip = true; break; }
                }
                if (skip) continue;
                var soname = fullPath.substring(fullPath.lastIndexOf("/") + 1);
                if (BASE_LIB_SONAMES[soname]) continue;
                if (BUNDLE_SHIPPED_SONAMES[soname]) continue;
                if (!fileExists(fullPath)) continue;
                deps[soname] = fullPath;
                queue.push(fullPath);
            }
        }
    } else {
        /* Linux / FreeBSD: ldd gives the full closure in one shot. */
        for (var li2 = 0; li2 < files.length; li2++) {
            var rel2 = files[li2];
            if (!/\.so(\.[0-9.]+)?$/.test(rel2)) continue;
            var src2 = stage + "/" + rel2;
            if (!fileExists(src2)) continue;
            var ldd = exec("ldd", src2);
            if (!ldd || ldd.exitStatus !== 0) continue;
            (ldd.stdout || "").split("\n").forEach(function (line) {
                var m = line.match(/^\s*(\S+)\s*=>\s*(\S+)\s/);
                if (!m) return;
                var soname = m[1], path = m[2];
                if (BASE_LIB_SONAMES[soname]) return;
                if (BUNDLE_SHIPPED_SONAMES[soname]) return;
                if (path === "not" || path[0] !== "/") return;
                deps[soname] = path;
            });
        }
    }

    var result = [];
    for (var soname in deps) result.push({ soname: soname, path: deps[soname] });
    return result;
}

/* macOS: rewrite a Mach-O file's LC_LOAD_DYLIB entries so any reference
   to a sibling-bundled lib goes through @rpath, set the LC_ID, add
   @loader_path to the LC_RPATH, then ad-hoc resign.  Idempotent enough
   to call twice. */
function macRewriteForBundle(filePath, bundledSonames, isOwnLib) {
    /* brew copies are 555 -- make writable before install_name_tool */
    run("chmod", ["u+w", filePath]);

    if (isOwnLib) {
        var sn = basename(filePath);
        run("install_name_tool", ["-id", "@rpath/" + sn, filePath]);
    }

    var o = exec("otool", "-L", filePath);
    if (o && o.exitStatus === 0) {
        var lines = (o.stdout || "").split("\n").slice(1);
        for (var i = 0; i < lines.length; i++) {
            var m = lines[i].match(/^\s+(\S.*?)\s+\(compatibility/);
            if (!m) continue;
            var full = m[1];
            if (full[0] === "@") continue;
            var so = full.substring(full.lastIndexOf("/") + 1);
            if (!bundledSonames[so]) continue;
            run("install_name_tool", ["-change", full, "@rpath/" + so, filePath]);
        }
    }

    /* Ensure @loader_path is in LC_RPATH so @rpath resolves to siblings.
       Use exec (not run) -- add_rpath errors if already present. */
    var rpath = isOwnLib ? "@loader_path" : "@loader_path";
    exec("install_name_tool", "-add_rpath", rpath, filePath);

    /* Re-sign ad-hoc so Gatekeeper accepts the rewritten Mach-O. */
    run("codesign", ["--force", "--sign", "-", filePath]);
}

function buildTarball(name, entry) {
    var anyExists = false;
    for (var i = 0; i < entry.files.length; i++) {
        if (fileExists(SOURCE + "/" + entry.files[i])) { anyExists = true; break; }
    }
    if (!anyExists) {
        info("  SKIP " + name + " (no source files present)");
        return null;
    }

    /* stage everything under a temp dir, then tar from there.  This
       lets us materialize the symlinks deterministically rather than
       hoping the live tree already has them. */
    var stage = "/tmp/rampart-pkg-" + name + "-" + process.getpid();
    run("rm", ["-rf", stage]);
    mkdir(stage, true);

    var staged = [];

    /* files
     *
     * For each entry, strip any trailing "/" -- both for the relative
     * path bookkeeping and for the cp arguments.  `cp -a SRC DST`
     * behaves correctly when DST does NOT pre-exist:
     *   - file -> file
     *   - dir  -> dir (creates DST as a new directory copy of SRC)
     * If DST already exists as a directory, cp instead copies SRC
     * INTO it, producing nested layouts like include/include/.  So we
     * mkdir only the PARENT of the destination, never the destination
     * itself.
     */
    for (var fi = 0; fi < entry.files.length; fi++) {
        var rel = entry.files[fi].replace(/\/+$/, "");   /* drop trailing / */
        var src = SOURCE + "/" + rel;
        if (!fileExists(src)) {
            info("    skip missing: " + rel);
            continue;
        }
        var dst = stage + "/" + rel;
        var lastSlash = dst.lastIndexOf("/");
        var parent = (lastSlash > 0) ? dst.substring(0, lastSlash) : stage;
        run("mkdir", ["-p", parent]);
        run("cp", ["-a", src, dst]);
        staged.push(rel);
    }

    /* symlinks
     *
     * Skip a symlink if the target it points to wasn't staged.  This
     * matters on platforms where a *_cpu / *_cuda suffixed variant
     * isn't built (FreeBSD, macOS, raspi) -- the langtools manifest
     * tries to point rampart-llamacpp.so -> rampart-llamacpp_cpu.so,
     * but on those platforms only the plain rampart-llamacpp.so is
     * built, so the symlink would dangle.  Skipping it leaves the
     * plain file (staged above) as the canonical target. */
    if (entry.symlinks) {
        for (var lpath in entry.symlinks) {
            var target = entry.symlinks[lpath];
            var dst = stage + "/" + lpath;
            var lastSlash = dst.lastIndexOf("/");
            var parent = (lastSlash > 0) ? dst.substring(0, lastSlash) : stage;
            /* target is relative to the symlink's directory; resolve
               it to a stage-absolute path for the existence check. */
            var resolved = parent + "/" + target;
            if (!fileExists(resolved)) {
                info("    skip symlink (target absent): " + lpath + " -> " + target);
                continue;
            }
            run("mkdir", ["-p", parent]);
            /* `ln -sfn target dst` -- -s symlink, -f force, -n don't
               deref existing target dir */
            run("ln", ["-sfn", target, dst]);
            staged.push(lpath);
        }
    }

    /* bundle_so_deps: ldd-scan each .so in `files`, drop a copy of
       every non-base, non-already-bundled-by-installer transitive dep
       into lib/<SONAME> inside the staging dir.  Combined with the
       package's RPATH "$ORIGIN/../lib" on its .so, this means a user
       can install on a vanilla machine without an apt/pkg install of
       the heavy native libraries. */
    if (entry.bundle_so_deps) {
        var deps = collectSoDeps(stage, entry.files);
        if (deps.length) {
            run("mkdir", ["-p", stage + "/lib"]);
            deps.forEach(function (d) {
                var dst = stage + "/lib/" + d.soname;
                run("cp", ["-L", d.path, dst]);
                staged.push("lib/" + d.soname);
            });

            if (isMacOS()) {
                /* Build a soname-set of bundled libs for the path-rewriter. */
                var bundledSet = {};
                deps.forEach(function (d) { bundledSet[d.soname] = true; });

                /* Rewrite each bundled .dylib: id -> @rpath/<name>, deps
                   pointing to other bundled libs -> @rpath/<name>, add
                   @loader_path to its LC_RPATH, ad-hoc re-sign. */
                deps.forEach(function (d) {
                    macRewriteForBundle(stage + "/lib/" + d.soname,
                                        bundledSet, true);
                });
                /* Rewrite the package's own .so files so their references
                   to bundled libs go through @rpath as well.  RPATH for
                   the .so is supplied by cmake (INSTALL_RPATH
                   "@loader_path/../lib" on Apple). */
                entry.files.forEach(function (rel) {
                    if (!/\.(so|dylib)(\.[0-9.]+)?$/.test(rel)) return;
                    var p = stage + "/" + rel;
                    if (!fileExists(p)) return;
                    macRewriteForBundle(p, bundledSet, false);
                });
            } else {
                /* Linux / FreeBSD: patchelf each bundled lib's RPATH to
                   "$ORIGIN" so it finds its SIBLING libs in the same
                   directory.  Without this, libs built with a path-
                   specific RPATH (e.g. FreeBSD gcc14 libgfortran ->
                   /usr/local/lib/gcc14) fail to resolve their
                   DT_NEEDED on vanilla install targets. */
                deps.forEach(function (d) {
                    var dst = stage + "/lib/" + d.soname;
                    try { run("patchelf", ["--set-rpath", "$ORIGIN", dst]); }
                    catch (e) {
                        fail("patchelf failed on " + d.soname + ": " + e.message +
                             "\n  install with: apt install patchelf  (Debian)" +
                             "  /  pkg install patchelf  (FreeBSD)");
                    }
                });
            }
            info(sprintf("  bundled %d transitive .so deps into lib/", deps.length));
        }
    }

    var tarball = OUTDIR + "/" + suffixedName(name, "tar.gz",
                                              entry.arch === "dep", "tar.gz");
    /* dedupe paths so tar doesn't warn about double-archive */
    var seen = {};
    var unique = [];
    for (var si = 0; si < staged.length; si++)
        if (!seen[staged[si]]) { seen[staged[si]] = 1; unique.push(staged[si]); }

    run("sh", ["-c", "cd " + stage + " && tar -czf " + tarball + " " +
               unique.join(" ")]);
    run("sh", ["-c", "cd " + OUTDIR + " && sha1sum " +
               basename(tarball) + " > " + basename(tarball) + ".sha1"]);

    run("rm", ["-rf", stage]);
    info(sprintf("  built  %-50s (%d bytes, %d entries)",
                 basename(tarball), stat(tarball).size, unique.length));
    return tarball;
}

/* ---------- iterate ---------- */

var counts = { bundle: 0, so: 0, js: 0, tar: 0, skip: 0 };
var produced = [];

info("Producing packages for " + VERSION + " on " + OS);
info("Source: " + SOURCE);
info("Output: " + OUTDIR);
info("");

for (var name in packages) {
    var entry = packages[name];

    if (entry.in_bundle) {
        counts.bundle++;
        continue;
    }

    info("[" + name + "]");
    var artifact = null;

    if (entry.kind === "so") {
        artifact = copySingle(name, entry, "so");
        if (artifact) counts.so++;
        else counts.skip++;
    } else if (entry.kind === "js") {
        artifact = copySingle(name, entry, "js");
        if (artifact) counts.js++;
        else counts.skip++;
    } else if (entry.kind === "tar.gz") {
        artifact = buildTarball(name, entry);
        if (artifact) counts.tar++;
        else counts.skip++;
    } else {
        info("  WARN: unknown kind " + entry.kind);
        counts.skip++;
    }

    if (artifact) produced.push(artifact);
}

printf("\nSummary:\n");
printf("  in_bundle (no artifact):  %d\n", counts.bundle);
printf("  .so packages produced:     %d\n", counts.so);
printf("  .js packages produced:     %d\n", counts.js);
printf("  .tar.gz packages produced: %d\n", counts.tar);
printf("  skipped (sources missing): %d\n", counts.skip);
printf("\n%d artifacts in %s\n", produced.length, OUTDIR);
