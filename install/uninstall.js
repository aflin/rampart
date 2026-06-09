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

printf("\nThis will uninstall rampart from %s\n", prefix);
if (manifest.installed_at)
    printf("(installed %s, profile %s)\n", manifest.installed_at, manifest.profile || "unknown");
printf("Proceed? [y/N] ");
stdout.fflush();
var ans = askKey("n");
if (ans !== "y") { printf("Cancelled.\n"); process.exit(0); }
printf("\n");

/* ---------- 1) remove installer-owned files ---------- */

var owned = {};
var packages = manifest.packages || {};
for (var pkg in packages) {
    var pf = packages[pkg].files || [];
    for (var i = 0; i < pf.length; i++) owned[pf[i]] = true;
}

/* Per-file unlink via rampart.utils.rm -- in-process, no fork.  Python
   installs have ~10k files in modules/python/... ; the previous
   exec("rm","-f",p) cost ~2ms per file (fork + exec), so ~20s total.
   The in-process path is ~10x faster. */
var removed = 0, missing = 0;
for (var rel in owned) {
    var p = prefix + "/" + rel;
    try { rampart.utils.rm(p); removed++; }
    catch (e) {
        if (/No such file/.test(e.message)) { missing++; }
        else { printf("  WARN: could not remove %s: %s\n", p, e.message); }
    }
}
printf("Removed %d installed file(s) (%d already missing).\n", removed, missing);

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

/* ---------- 3) remove symlinks ---------- */

var links = manifest.symlinks || [];
var linksRemoved = 0;
for (var k = 0; k < links.length; k++) {
    var ln = links[k];
    if (!fileExists(ln)) continue;
    /* only remove if it still points inside our prefix */
    var target = null;
    try { target = _trim(exec("readlink", ln).stdout); } catch (e) {}
    if (target && isSubpath(target, prefix)) {
        try { exec("rm","-f",ln); linksRemoved++; }
        catch (e) { printf("  WARN: could not remove symlink %s: %s\n", ln, e.message); }
    }
}
if (linksRemoved) printf("Removed %d symlink(s).\n", linksRemoved);

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
