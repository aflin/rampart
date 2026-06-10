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
        else { printf("  WARN: could not remove %s: %s\n", p, e.message); }
    }
}

/* Directory entries: rm -rf as a whole.  Guarded by the same isSubpath
 * safety check so a malformed manifest can't escape the prefix. */
var dirsRemoved = 0, dirsRefused = 0;
for (var d in ownedDirs) {
    if (!isSubpath(d, prefix)) {
        printf("  WARN: refusing to rm -rf %s (outside prefix %s)\n", d, prefix);
        dirsRefused++;
        continue;
    }
    if (!fileExists(d)) { continue; }   /* already gone */
    try {
        exec("rm", "-rf", d);
        dirsRemoved++;
        printf("  removed dir tree: %s\n", d);
    } catch (e) {
        printf("  WARN: could not remove dir %s: %s\n", d, e.message);
    }
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

var links = manifest.symlinks || [];
var linksRemoved = 0;
var linksPermDenied = [];
var linksOtherFail = [];
for (var k = 0; k < links.length; k++) {
    var ln = links[k];
    if (!lexists(ln)) continue;
    /* only remove if it still points inside our prefix */
    var target = null;
    try { target = _trim(exec("readlink", ln).stdout); } catch (e) {}
    if (!(target && isSubpath(target, prefix))) continue;
    var rmRes;
    try { rmRes = exec("rm","-f",ln); }
    catch (e) { linksOtherFail.push({path: ln, why: e.message}); continue; }
    if (rmRes && rmRes.exitStatus === 0 && !lexists(ln)) {
        linksRemoved++;
    } else {
        var stderr = (rmRes && _trim(rmRes.stderr)) || "";
        if (/permission denied|not permitted/i.test(stderr)) {
            linksPermDenied.push(ln);
        } else {
            linksOtherFail.push({path: ln, why: stderr || "rm exited "+(rmRes && rmRes.exitStatus)});
        }
    }
}
if (linksRemoved) printf("Removed %d symlink(s).\n", linksRemoved);

function reportLinkFailures() {
    if (!linksPermDenied.length && !linksOtherFail.length) return;
    printf("\n");
    if (linksPermDenied.length) {
        printf("Could not remove %d symlink(s) -- permission denied:\n",
               linksPermDenied.length);
        for (var i = 0; i < linksPermDenied.length; i++)
            printf("    %s\n", linksPermDenied[i]);
        printf("Re-run with sudo to remove them, e.g.:\n");
        printf("    sudo rm -f");
        for (var j = 0; j < linksPermDenied.length; j++)
            printf(" %s", linksPermDenied[j]);
        printf("\n");
    }
    if (linksOtherFail.length) {
        printf("Could not remove %d symlink(s):\n", linksOtherFail.length);
        for (var f = 0; f < linksOtherFail.length; f++)
            printf("    %s  (%s)\n", linksOtherFail[f].path, linksOtherFail[f].why);
    }
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

/* ignore the install scaffolding -- we'll remove them last (after the
   user decides on wipe-or-keep). */
var SCAFFOLD_PATHS = [
    "installed.json",
    "bin/rampart-uninstall.sh",
    "bin/rampart-uninstall.js",
    "bin/rampart"           /* we just removed it; if it lingered, still ours */
];
var SCAFFOLD = {};
for (var sp = 0; sp < SCAFFOLD_PATHS.length; sp++) SCAFFOLD[SCAFFOLD_PATHS[sp]] = 1;
leftover = leftover.filter(function (p) { return !SCAFFOLD[p]; });

if (leftover.length === 0) {
    /* nothing left except scaffold -- nuke it and the dir.  We can't
       `rm` the very script we're running, but the kernel keeps the
       inode alive until the process exits, so this works on POSIX. */
    for (var sk in SCAFFOLD) try { exec("rm","-f",prefix+"/"+sk); } catch (e) {}
    try { exec("rmdir", prefix + "/bin"); } catch (e) {}
    try { exec("rmdir", prefix); } catch (e) {}
    printf("\nDone.  %s removed cleanly.\n", prefix);
    reportLinkFailures();
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
    try { exec("rm","-rf",prefix); printf("Removed %s.\n", prefix); }
    catch (e) { printf("ERROR: could not remove %s: %s\n", prefix, e.message); process.exit(1); }
} else {
    /* preserve user files; drop install scaffolding only */
    for (var sk2 in SCAFFOLD) try { exec("rm","-f",prefix+"/"+sk2); } catch (e) {}
    try { exec("rmdir", prefix + "/bin"); } catch (e) {}
    printf("Kept %s (%d user file(s) preserved).\n", prefix, leftover.length);
}
reportLinkFailures();
