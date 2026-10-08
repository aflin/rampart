/*
   rampart uninstall driver
   ========================

   Reads <prefix>/installed.json and removes everything the installer
   put there.  Preserves any user-created files in the install dir;
   offers to wipe them too at the end.

   Invoked via the uninstall.sh shim:
       /home/aaron/.rampart/uninstall.sh

   Run directly:
       /home/aaron/.rampart/bin/rampart /home/aaron/.rampart/uninstall.js
*/

rampart.globalize(rampart.utils);

/* ---------- helpers ---------- */

function _trim(s) {
    if (s == null || s === false) return "";
    return (""+s).replace(/^\s+|\s+$/g, "");
}

function fileExists(p) {
    try { return !!stat(p); } catch (e) { return false; }
}

function askKey(def) {
    var r;
    try { r = stdin.getchar(1); } catch (e) { r = null; }
    if (r === null || r === undefined || r === false || r === "")
    { printf("\n"); process.exit(0); }
    if (r === "\n") return def;
    printf("\n");
    return (""+r).toLowerCase();
}

function isSubpath(p, parent) {
    return p === parent || p.indexOf(parent + "/") === 0;
}

/* A removal blocked by privilege is recoverable -- re-running under sudo
   finishes the job -- so those paths are collected here and the install
   scaffolding (this script included) is KEPT when the list is non-empty.
   Deleting ourselves on a failed run leaves no second chance. */
var permFiles = [];   /* recover with: sudo rm -f   */
var permDirs  = [];   /* recover with: sudo rm -rf  */
var hardFail  = [];   /* failed for some other reason */

function isPermErr(msg) { return /permission denied|not permitted/i.test("" + msg); }
function permBlocked()  { return permFiles.length > 0 || permDirs.length > 0; }

/* Refuse to uninstall from a system directory.  Mirrors entry_script.js's
   validatePrefix -- if a manifest's recorded prefix is "/" or "/usr" or
   any other top-level system path, every "owned" entry would pass
   isSubpath and we'd cheerfully rm system files.  Both files are kept
   in lockstep; if one list grows the other should too. */
var UNSAFE_PREFIXES = {
    "/": 1,
    "/usr": 1, "/usr/local": 1, "/usr/bin": 1, "/usr/sbin": 1, "/usr/lib": 1,
    "/usr/include": 1, "/usr/share": 1, "/usr/libexec": 1,
    "/etc": 1, "/var": 1, "/bin": 1, "/sbin": 1, "/lib": 1, "/opt": 1,
    "/tmp": 1, "/root": 1, "/dev": 1, "/proc": 1, "/sys": 1, "/boot": 1,
    "/Users": 1, "/home": 1, "/mnt": 1, "/media": 1,
    "/Applications": 1, "/System": 1, "/Library": 1,
    "/private": 1, "/Volumes": 1, "/Network": 1
};
function validatePrefix(p) {
    if (typeof p !== "string" || p === "") return "empty prefix";
    if (p.charAt(0) !== "/") return "prefix not absolute: " + p;
    if (p.indexOf("/..") >= 0 || p.indexOf("/./") >= 0 || /\/\.$/.test(p))
        return "prefix has '.' or '..' segment: " + p;
    var norm = p;
    while (norm.length > 1 && norm.charAt(norm.length-1) === "/")
        norm = norm.slice(0, -1);
    if (UNSAFE_PREFIXES[norm])
        return norm + " is a system directory; refusing to uninstall";
    return null;
}

/* ---------- locate manifest ----------
 * uninstall.js lives at <prefix>/bin/rampart-uninstall.js.  process.scriptPath
 * is the script's own directory.  The install prefix is one level up.
 * If the manifest disagrees with that (rare), trust the manifest.
 */

var prefix = process.scriptPath.replace(/\/bin\/?$/, "");
var manifestPath = prefix + "/installed.json";

if (!fileExists(manifestPath)) {
    printf("ERROR: %s not found.\n", manifestPath);
    printf("This is required for a clean uninstall.  If you really want to nuke\n");
    printf("the install, run:  rm -rf %s\n", prefix);
    process.exit(1);
}

var manifest;
try { manifest = JSON.parse(readFile(manifestPath, true)); }
catch (e) { printf("ERROR: could not parse %s: %s\n", manifestPath, e.message); process.exit(1); }

/* manifest.prefix is authoritative; ignore process.scriptPath if they disagree */
if (manifest.prefix) prefix = manifest.prefix;

/* HARD BAIL on a manifest that records a system-directory prefix.
   Without this, owned-files iteration plus isSubpath would happily
   accept rm of /etc/passwd because it's "under prefix /".  This is the
   load-bearing final guard against a tampered or corrupted manifest. */
var prefixErr = validatePrefix(prefix);
if (prefixErr) {
    printf("ABORT: %s.\n", prefixErr);
    printf("       Manifest at %s records this prefix and is therefore\n", manifestPath);
    printf("       unsafe to act on.  No files will be removed.\n");
    printf("       Inspect the manifest manually, then delete files\n");
    printf("       by hand if you really mean to.\n");
    process.exit(1);
}

printf("\nThis will uninstall rampart from %s\n", prefix);
if (manifest.installed_at)
    printf("(installed %s, profile %s)\n", manifest.installed_at, manifest.profile || "unknown");
printf("Proceed? [y/N] ");
stdout.fflush();
var ans = askKey("n");
if (ans !== "y") { printf("Cancelled.\n"); process.exit(0); }
printf("\n");

/* ---------- 1) remove installer-owned files ----------
 * Schema v2: manifest.packages[pkg].files entries are absolute paths.
 *   A trailing "/" marks a directory entry that we rm -rf (used for
 *   rampart-python -- the embedded Python tree has ~10k files we don't
 *   enumerate individually).
 * Schema v1 (legacy): entries are relative to manifest.prefix.  We
 *   convert on the fly so old installs uninstall correctly too.
 *
 * Safety rail: rm -rf only fires if (a) the entry has a trailing "/"
 *   AND (b) the resolved path is under our recorded prefix.  Anything
 *   else -- including paths that escape the prefix -- gets skipped
 *   with a WARN.  The directory removal can't escape upward. */
var schemaV2 = (manifest.schema === 2);

/* The install scaffolding goes LAST, once everything else is gone.
   bin/rampart in particular: the shim re-execs it to run this script, so
   deleting it early would leave a sudo re-run with nothing to run but
   the shim's "appears damaged" rm -rf fallback. */
var SCAFFOLD_PATHS = [
    "installed.json",
    "bin/rampart-uninstall.sh",
    "bin/rampart-uninstall.js",
    "bin/rampart"
];
var SCAFFOLD = {};
for (var sp = 0; sp < SCAFFOLD_PATHS.length; sp++) SCAFFOLD[SCAFFOLD_PATHS[sp]] = 1;

var owned = {};
var ownedDirs = {};
var packages = manifest.packages || {};
for (var pkg in packages) {
    var pf = packages[pkg].files || [];
    for (var i = 0; i < pf.length; i++) {
        var entry = pf[i];
        var abs = (entry.charAt(0) === "/") ? entry : (prefix + "/" + entry);
        if (entry.charAt(entry.length-1) === "/") {
            /* dir marker -- rm -rf'd in a separate pass below */
            ownedDirs[abs] = true;
        } else {
            if (SCAFFOLD[abs.indexOf(prefix + "/") === 0
                         ? abs.slice(prefix.length + 1) : abs]) continue;
            owned[abs] = true;
        }
    }
}

/* Files: per-path rm via rampart.utils.rm -- in-process, no fork. */
var removed = 0, missing = 0, refusedOutsidePrefix = 0;
for (var p in owned) {
    if (!isSubpath(p, prefix)) {
        printf("  WARN: refusing to remove %s (outside prefix %s)\n", p, prefix);
        refusedOutsidePrefix++;
        continue;
    }
    try { rampart.utils.rm(p); removed++; }
    catch (e) {
        if (/No such file/.test(e.message)) { missing++; }
        else if (isPermErr(e.message)) { permFiles.push(p); }
        else {
            printf("  WARN: could not remove %s: %s\n", p, e.message);
            hardFail.push({path: p, why: e.message});
        }
    }
}
if (permFiles.length)
    printf("  %d file(s) could not be removed (permission denied).\n",
           permFiles.length);

/* Directory entries: rm -rf as a whole.  Guarded by isSubpath so a
 * malformed manifest can't escape the prefix, AND we check exitStatus
 * (exec doesn't throw on rm's non-zero exit -- a permission-denied
 * rm -rf returns a result object with exitStatus != 0 that we'd
 * otherwise treat as success). */
var dirsRemoved = 0, dirsRefused = 0, dirsFailed = [];
for (var d in ownedDirs) {
    if (!isSubpath(d, prefix)) {
        printf("  WARN: refusing to rm -rf %s (outside prefix %s)\n", d, prefix);
        dirsRefused++;
        continue;
    }
    if (!fileExists(d)) { continue; }   /* already gone */
    var rmRes;
    try { rmRes = exec("rm", "-rf", d); }
    catch (e) {
        dirsFailed.push({path: d, why: e.message});
        if (isPermErr(e.message)) permDirs.push(d);
        else hardFail.push({path: d, why: e.message});
        continue;
    }
    /* Even after rm returns, double-check the dir is actually gone --
       on macOS some root-owned subtrees yield exitStatus 0 yet leave
       contents behind under specific permission shapes, and jetsam can
       SIGKILL /bin/rm midway through (exit 137) leaving most of the
       tree intact. */
    if (rmRes && rmRes.exitStatus === 0 && !fileExists(d)) {
        dirsRemoved++;
        printf("  removed dir tree: %s\n", d);
        continue;
    }
    /* Fallback: in-process recursive walk via rampart.utils.rm.  Slower
       but immune to whatever just killed /bin/rm. */
    var why = (rmRes && _trim(rmRes.stderr)) ||
              ("rm exited " + (rmRes ? rmRes.exitStatus : "?"));
    if (fileExists(d)) why += " (dir still present)";
    printf("  %s rm failed (%s); falling back to in-process walk...\n", d, why);
    if (rmTreeInProc(d)) {
        dirsRemoved++;
        printf("  removed dir tree (in-process): %s\n", d);
    } else {
        dirsFailed.push({path: d, why: why + "; in-process fallback also failed"});
        if (isPermErr(why)) permDirs.push(d);
        else hardFail.push({path: d, why: why});
    }
}
if (dirsFailed.length) {
    printf("Could NOT remove %d dir tree(s):\n", dirsFailed.length);
    for (var df = 0; df < dirsFailed.length; df++)
        printf("    %s  (%s)\n", dirsFailed[df].path, dirsFailed[df].why);
}

printf("Removed %d file(s) (%d already missing)%s%s.\n",
       removed, missing,
       dirsRemoved ? ", " + dirsRemoved + " dir tree(s)" : "",
       refusedOutsidePrefix || dirsRefused
           ? ", " + (refusedOutsidePrefix + dirsRefused) + " entries refused (outside prefix)"
           : "");

/* ---------- 2) strip guarded blocks from rc files ---------- */

var rcFiles = manifest.rc_files || [];
var stripped = 0;
for (var j = 0; j < rcFiles.length; j++) {
    var rcf = rcFiles[j];
    if (!fileExists(rcf)) continue;
    var content;
    try { content = readFile(rcf, true); } catch (e) { continue; }
    var newC = content.replace(/\n?# >>> rampart installer >>>[\s\S]*?# <<< rampart installer <<<\n?/g, "\n");
    if (newC !== content) {
        try { fwrite(rcf, newC); stripped++; }
        catch (e) { printf("  WARN: could not update %s: %s\n", rcf, e.message); }
    }
}
if (stripped) printf("Stripped rampart block from %d rc file(s).\n", stripped);

/* ---------- 3) remove symlinks ----------
 * Use lstat (not stat) so broken symlinks still get detected.
 * Check `rm`'s exitStatus rather than relying on exec throwing -- rm
 * exits non-zero on permission denied but doesn't raise, and `-f`
 * silences its stderr.  Differentiate "permission denied" failures
 * (the user just needs sudo) from other failures and report both at
 * the end so the user knows whether the install is fully gone. */

function lexists(p) { try { return !!lstat(p); } catch (e) { return false; } }

/* In-process recursive remove.  rampart.utils.rm with {recursive:true,
   force:true} walks the tree natively in C -- no fork, no subprocess,
   immune to whatever's killing /bin/rm with SIGKILL on this host
   (jetsam under memory pressure being the prime suspect).  Returns
   true if the dir is gone after the call, false otherwise.  We don't
   try to enumerate failures here -- the post-call existence check
   is the only thing that matters for the uninstall flow. */
function rmTreeInProc(dir) {
    try { rampart.utils.rm(dir, {recursive: true, force: true}); }
    catch (e) { /* fall through to existence check */ }
    return !fileExists(dir);
}

var links = manifest.symlinks || [];
var linksRemoved = 0;
for (var k = 0; k < links.length; k++) {
    var ln = links[k];
    if (!lexists(ln)) continue;
    /* only remove if it still points inside our prefix */
    var target = null;
    try { target = _trim(exec("readlink", ln).stdout); } catch (e) {}
    if (!(target && isSubpath(target, prefix))) continue;
    var rmRes;
    try { rmRes = exec("rm","-f",ln); }
    catch (e) { hardFail.push({path: ln, why: e.message}); continue; }
    if (rmRes && rmRes.exitStatus === 0 && !lexists(ln)) {
        linksRemoved++;
    } else {
        var stderr = (rmRes && _trim(rmRes.stderr)) || "";
        if (isPermErr(stderr)) permFiles.push(ln);
        else hardFail.push({path: ln,
                            why: stderr || "rm exited "+(rmRes && rmRes.exitStatus)});
    }
}
if (linksRemoved) printf("Removed %d symlink(s).\n", linksRemoved);

/* Drop the install scaffolding (this script included).  Only ever called
   once every owned file is gone.  Returns the paths that survived -- the
   caller must say so rather than claim a clean uninstall. */
function removeScaffold() {
    var stuck = [];
    /* SCAFFOLD_PATHS order, not key order: bin/rampart is last, so the
       interpreter outlives the files that might fail before it. */
    for (var s = 0; s < SCAFFOLD_PATHS.length; s++) {
        var sp = prefix + "/" + SCAFFOLD_PATHS[s];
        if (!lexists(sp)) continue;
        try { exec("rm", "-f", sp); } catch (e) {}
        if (lexists(sp)) stuck.push(sp);
    }
    return stuck;
}

/* Can `sudo rampart-uninstall.sh` still do anything?  The shim re-execs
   bin/rampart on this script, and the script needs the manifest.  Asked
   at print time rather than inferred from which branch we are in: an
   interrupted `rm -rf` can have taken any subset of these. */
function canRerun() {
    return lexists(prefix + "/bin/rampart-uninstall.sh") &&
           lexists(prefix + "/bin/rampart-uninstall.js") &&
           lexists(prefix + "/bin/rampart") &&
           lexists(prefix + "/installed.json");
}

/* The one place recovery instructions are printed. */
function reportFailures() {
    if (hardFail.length) {
        printf("\nCould not remove %d item(s):\n", hardFail.length);
        for (var f = 0; f < hardFail.length; f++)
            printf("    %s  (%s)\n", hardFail[f].path, hardFail[f].why);
    }
    if (!permBlocked()) return;

    var rerun = canRerun();
    var n = permFiles.length + permDirs.length;
    printf("\n");
    printf("UNINSTALL INCOMPLETE -- %d item(s) could not be removed because\n", n);
    printf("this user lacks permission.  rampart was most likely installed\n");
    printf("with sudo, so removing it needs sudo too.\n");

    if (rerun) {
        printf("\nTo finish, re-run the uninstaller as root:\n");
        printf("    sudo %s\n", prefix + "/bin/rampart-uninstall.sh");
        printf("\nOr remove the remaining files by hand:\n");
    } else {
        /* The uninstaller is already (partly) gone, so re-running it is
           not an option.  Hand over the commands instead. */
        printf("\nFinish as root:\n");
    }

    var i, listedPrefix = false;
    for (i = 0; i < permFiles.length; i++)
        printf("    sudo rm -f %s\n", permFiles[i]);
    for (i = 0; i < permDirs.length; i++) {
        printf("    sudo rm -rf %s\n", permDirs[i]);
        if (permDirs[i] === prefix) listedPrefix = true;
    }
    /* The sweep-up line, unless it is already above. */
    if (!listedPrefix) printf("    sudo rm -rf %s\n", prefix);

    if (rerun)
        printf("\nThe uninstaller has been left in place so it can be re-run.\n");
}

/* ---------- 4) walk prefix; find leftovers ---------- */

function walk(dir, base, out) {
    var entries;
    try { entries = readdir(dir); } catch (e) { return; }
    for (var i = 0; i < entries.length; i++) {
        var name = entries[i];
        if (name === "." || name === "..") continue;
        var full = dir + "/" + name;
        var rel  = base ? base + "/" + name : name;
        var st;
        try { st = lstat(full); } catch (e) { continue; }
        if (st && st.isDirectory && !st.isSymbolicLink) {
            walk(full, rel, out);
            /* if the dir is now empty, it's an installer-owned scaffold dir */
            try {
                var rest = readdir(full).filter(function (n) { return n !== "." && n !== ".."; });
                if (!rest.length) rampart.utils.rmdir(full);
            } catch (e) {}
        } else {
            out.push(rel);
        }
    }
}

var leftover = [];
walk(prefix, "", leftover);

/* Ignore the install scaffolding (removed last, after the user decides on
   wipe-or-keep) and anything we own but could not remove -- those are
   ours, not the user's, so don't offer them up as "not installed by us". */
var stuckSet = {};
for (var si0 = 0; si0 < permFiles.length; si0++) stuckSet[permFiles[si0]] = 1;
for (var si1 = 0; si1 < hardFail.length; si1++) stuckSet[hardFail[si1].path] = 1;

leftover = leftover.filter(function (p) {
    if (SCAFFOLD[p]) return false;
    var abs = prefix + "/" + p;
    if (owned[abs] || stuckSet[abs]) return false;
    for (var d in ownedDirs) if (isSubpath(abs, d.replace(/\/$/, ""))) return false;
    return true;
});

/* Nothing below this point may delete the scaffolding if a removal was
   blocked by privilege: the shim and this script are the user's only way
   to finish the job with sudo. */
if (permBlocked()) {
    if (leftover.length)
        printf("\n%d user file(s) under %s were left untouched.\n",
               leftover.length, prefix);
    reportFailures();
    process.exit(1);
}

if (leftover.length === 0) {
    /* nothing left except scaffold -- nuke it and the dir.  We can't
       `rm` the very script we're running, but the kernel keeps the
       inode alive until the process exits, so this works on POSIX. */
    var stuck = removeScaffold();
    try { exec("rmdir", prefix + "/bin"); } catch (e) {}
    try { exec("rmdir", prefix); } catch (e) {}
    if (stuck.length) {
        printf("\nAlmost -- %d install file(s) could not be removed:\n", stuck.length);
        for (var si = 0; si < stuck.length; si++) printf("    %s\n", stuck[si]);
        printf("\nFinish as root:\n    sudo rm -rf %s\n", prefix);
        process.exit(1);
    }
    printf("\nDone.  %s removed cleanly.\n", prefix);
    reportFailures();
    process.exit(0);
}

printf("\nFound %d file(s) under %s that weren't installed by us:\n", leftover.length, prefix);
for (var l = 0; l < leftover.length; l++) {
    if (l >= 20) { printf("    ... and %d more\n", leftover.length - 20); break; }
    printf("    %s/%s\n", prefix, leftover[l]);
}

printf("\n");
printf("  1) Preserve them -- leave %s in place (default)\n", prefix);
printf("  2) Wipe %s and everything in it\n", prefix);
printf("[1] > ");
stdout.fflush();
var c = askKey("1");
printf("\n");
if (c === "2") {
    /* Same defense-in-depth as the dir-tree branch above: check
       exitStatus + verify the dir is actually gone + fall back to
       in-process rmTree if /bin/rm got SIGKILL'd or returned non-zero.
       The previous version blindly printed "Removed ..." regardless. */
    var wipeRes;
    try { wipeRes = exec("rm","-rf",prefix); }
    catch (e) {
        printf("  exec rm threw (%s); using in-process walk\n", e.message);
        wipeRes = null;
    }
    if (wipeRes && wipeRes.exitStatus === 0 && !fileExists(prefix)) {
        printf("Removed %s.\n", prefix);
    } else {
        var wreason = (wipeRes && _trim(wipeRes.stderr)) ||
                      ("rm exited " + (wipeRes ? wipeRes.exitStatus : "?"));
        if (fileExists(prefix)) wreason += " (dir still present)";
        printf("  rm -rf %s failed (%s); falling back to in-process walk...\n",
               prefix, wreason);
        if (rmTreeInProc(prefix)) {
            printf("Removed %s (in-process).\n", prefix);
        } else {
            printf("ERROR: %s NOT fully removed.\n", prefix);
            /* If privilege was the problem, route through the standard
               recovery notice -- the shim survived the failed rm -rf. */
            if (isPermErr(wreason)) permDirs.push(prefix);
            else hardFail.push({path: prefix, why: wreason});
            reportFailures();
            if (!permBlocked()) printf("       Try:  sudo rm -rf %s\n", prefix);
            process.exit(1);
        }
    }
} else {
    /* preserve user files; drop install scaffolding only */
    var stuck2 = removeScaffold();
    try { exec("rmdir", prefix + "/bin"); } catch (e) {}
    printf("Kept %s (%d user file(s) preserved).\n", prefix, leftover.length);
    if (stuck2.length) {
        printf("Note: %d install file(s) remain and need root to remove:\n",
               stuck2.length);
        for (var sj = 0; sj < stuck2.length; sj++)
            printf("    sudo rm -f %s\n", stuck2[sj]);
    }
}
reportFailures();
