/*
   rampart installer driver
   ========================

   When the rampart-install bundle is invoked with no positional script
   argument, the runtime auto-runs this file as the entry script.  It
   walks the user through installation rustup-style.

   Two install profiles:

     SYSTEM ("/usr/local/rampart")
       Selected when the running user owns /usr/local and /usr/local/bin
       (or is root).  Files land in <prefix>/{bin,modules,...} and a
       symlink farm under /usr/local/bin picks up `rampart` and the
       texis tools.  No shell-rc edits needed because /usr/local/bin is
       already on everyone's PATH.

     USER ("$HOME/.rampart")
       Selected otherwise (and as a fallback when the user picks
       "install to home" from the menu).  Files land in <prefix>/.
       Three sourceable env scripts (env / env.csh / env.fish) are
       written, and every shell rc file the user has gets a guarded
       block that sources the right one.  Modelled on rustup's
       rc-file-modification logic.

   Both profiles drop a rampart-uninstall.sh shim (and the matching
   rampart-uninstall.js smart driver) under <prefix>/bin/ that reverses
   the install on demand.
*/

rampart.globalize(rampart.utils);

/* ============== command-line flags ============== */
/* process.argv = ["<bundle>", "entry_script.js", ...user args] */
var AUTO = false;
for (var _ai = 2; _ai < process.argv.length; _ai++) {
    var _arg = process.argv[_ai];
    if (_arg === "-q" || _arg === "--quiet"
     || _arg === "-a" || _arg === "--auto"
     || _arg === "-y" || _arg === "--yes")
        AUTO = true;
    else if (_arg === "-h" || _arg === "--help") {
        printf("Usage: %s [options]\n\n", process.argv[0]);
        printf("Options:\n");
        printf("  -a, --auto, --quiet, -q, -y\n");
        printf("        Non-interactive install.  Uses the default prefix\n");
        printf("        (/usr/local/rampart if you own /usr/local, else\n");
        printf("        $HOME/.rampart) and answers yes to every prompt.\n");
        printf("  -h, --help\n");
        printf("        Show this message.\n");
        process.exit(0);
    } else {
        printf("Unknown option: %s\nTry %s --help\n", _arg, process.argv[0]);
        process.exit(1);
    }
}

/* ============== small helpers ============== */

/* local string trim that tolerates non-strings -- the globalised rampart
   trim() throws on non-string input, which we hit when readLine returns
   `false` (Ctrl-C / EOF / empty enter). */
function _trim(s) {
    if (s == null || s === false) return "";
    return (""+s).replace(/^\s+|\s+$/g, "");
}

function shellOK(cmd) {
    var r;
    try { r = exec.apply(null, [].slice.call(arguments)); }
    catch (e) { return null; }
    if (!r || r.exitStatus !== 0) return null;
    return _trim(r.stdout);
}

function uidOf(path) {
    var st;
    try { st = stat(path); } catch (e) { return -1; }
    return (st && typeof st.uid === "number") ? st.uid : -1;
}

function fileExists(path) {
    var st;
    try { st = stat(path); } catch (e) { return false; }
    return !!st;
}

/* who am I, really -- if running under sudo we want the invoking user
   so the rc-file edits land in the invoking user's home, not root's. */
function homeOf(user) {
    /* Portable: parse /etc/passwd via awk.  getent exists on Linux only;
       macOS's directory service stores accounts in dscl, but admin/local
       users still land in /etc/passwd, so this works for the install-time
       common case across Linux/FreeBSD/macOS. */
    var line = shellOK("awk", "-F:", "$1==\""+user+"\"{print $6;exit}",
                       "/etc/passwd");
    if (line) return line;
    /* Last-ditch fallback so we still return *something* sensible. */
    return "/home/" + user;
}
function effectiveUser() {
    if (process.env.SUDO_USER && process.env.SUDO_USER !== "root")
        return {
            name: process.env.SUDO_USER,
            uid:  parseInt(shellOK("id","-u",process.env.SUDO_USER) || "-1", 10),
            home: homeOf(process.env.SUDO_USER)
        };
    var name = shellOK("whoami") || "unknown";
    return {
        name: name,
        uid:  parseInt(shellOK("id","-u") || "-1", 10),
        home: process.env.HOME || homeOf(name)
    };
}

/* ============== input ============== */

/* Single-keystroke prompt with a one-char default.  Mirrors install-helper.js's
   getresp(): one getchar(); plain enter -> default; anything else -> that key,
   lowercased.  Use this for menu choices and y/n. */
function askKey(def) {
    var ret;
    try { ret = stdin.getchar(1); } catch (e) { ret = null; }
    if (ret === null || ret === undefined || ret === false || ret === "")
    { printf("\n"); process.exit(0); }   /* EOF / Ctrl-C */
    if (ret === "\n") { return def; }
    printf("\n");
    return (""+ret).toLowerCase();
}

/* Full-line prompt for free-text input (paths etc.).  readLine(stdin) returns
   an iterator; .next() returns the line.  Plain enter -> default. */
function askLine(def) {
    var line;
    try { line = readLine(stdin).next(); }
    catch (e) { printf("\n"); process.exit(0); }
    if (line === null || line === undefined || line === false)
    { printf("\n"); process.exit(0); }
    line = _trim(line);
    return line === "" ? (def != null ? def : "") : line;
}

function askYN(prompt, def) {
    var hint = def ? " [Y/n] " : " [y/N] ";
    while (true) {
        printf("%s%s", prompt, hint);
        stdout.fflush();
        var ans = askKey(def ? "y" : "n");
        if (ans === "y") return true;
        if (ans === "n") return false;
        printf("Please answer y or n.\n");
    }
}

/* ============== detect default prefix ============== */

var me = effectiveUser();

/* The actual process euid -- distinct from me.uid when we were invoked
   via `sudo` or from a sudo-spawned root shell.  In that case me is the
   SUDO_USER (whose home / rc files we still want to edit), but the
   *process* is root and can write anywhere. */
var EUID = parseInt(shellOK("id","-u") || "-1", 10);

function canSystemInstall() {
    if (EUID === 0) return true;
    if (me.uid === 0) return true;
    /* Non-root and the system prefix already exists owned by us -- common
       case: a prior `sudo chown $USER /usr/local/rampart` (or the chown
       this installer does on a sudo install) means the user can write to
       /usr/local/rampart even though /usr/local itself is root-owned (as
       on macOS).  We can re-install in place; only the /usr/local/bin
       symlink step will need to be skipped if /usr/local/bin isn't ours. */
    if (fileExists(SYSTEM_PREFIX) && uidOf(SYSTEM_PREFIX) === me.uid) return true;
    return uidOf("/usr/local") === me.uid
        && (uidOf("/usr/local/bin") === me.uid || !fileExists("/usr/local/bin"));
}

var SYSTEM_PREFIX = "/usr/local/rampart";
var USER_PREFIX   = me.home + "/.rampart";

/* ============== banner ============== */

function banner(prefix, isSystem) {
    printf(
"\n"+
"Welcome to the rampart installer.\n"+
"\n"+
"Rampart and its modules are licensed under the MIT license, with the\n"+
"exception of rampart-sql and the texis command-line tools (addtable,\n"+
"backref, kdbfchk, metamorph, rex, texislockd, tsql), which are covered\n"+
"by a separate license -- see licenses/rampart-sql-license.txt inside\n"+
"the resulting install.  Other components carry their own licenses;\n"+
"all texts are in the licenses/ directory.\n"+
"\n"+
"This installer will copy rampart and a curated set of modules into\n"+
"the location below.  The whole install lives in a single directory\n"+
"and can be removed at any time by running rampart-uninstall.sh.\n"+
"\n"+
"  Install location:  %s\n", prefix);

    if (isSystem) {
        printf(
"  PATH:              already covered by /usr/local/bin\n"+
"  /usr/local/bin/    symlinks for: rampart, addtable, backref,\n"+
"                                   kdbfchk, metamorph, rex,\n"+
"                                   texislockd, tsql\n");
    } else {
        printf(
"  PATH:              modified via %s/env\n", prefix);
        printf(
"                     (rc files for every shell you have will be\n"+
"                     edited to source it on login)\n");
    }
    printf(
"\n"+
"Bundled modules: rampart-{crypto,curl,html,lmdb,net,server,sql,\n"+
"                          totext}.so, rampart-{sqlUpdate,url,\n"+
"                          webserver}.js, babel.js, babel-polyfill.js\n"+
"\n"+
"Uninstall any time with %s/bin/rampart-uninstall.sh\n"+
"\n", prefix);
}

/* ============== menu ============== */

function chooseProfile() {
    var sys = canSystemInstall();
    var prefix, isSystem;

    while (true) {
        printf("\nInstall options:\n\n");
        if (sys) {
            printf("  1) Install to %-30s (system, with /usr/local/bin links)\n", SYSTEM_PREFIX);
            printf("  2) Install to %-30s (user, modifies your shell PATH)\n",   USER_PREFIX);
            printf("  3) Install to a custom location\n");
            printf("  4) Cancel installation\n\n");
        } else {
            printf("  1) Install to %-30s (user, modifies your shell PATH)\n",   USER_PREFIX);
            printf("  2) Install to a custom location\n");
            printf("  3) Cancel installation\n\n");
        }
        printf("[1] > ");
        stdout.fflush();
        var choice = askKey("1");

        if (sys) {
            if (choice === "1") { prefix = SYSTEM_PREFIX; isSystem = true;  break; }
            if (choice === "2") { prefix = USER_PREFIX;   isSystem = false; break; }
            if (choice === "3") { return customize(true); }
            if (choice === "4") { printf("Cancelled.\n"); process.exit(0); }
        } else {
            if (choice === "1") { prefix = USER_PREFIX;   isSystem = false; break; }
            if (choice === "2") { return customize(false); }
            if (choice === "3") { printf("Cancelled.\n"); process.exit(0); }
        }
        printf("Please pick one of the listed options.\n");
    }

    return { prefix: prefix, isSystem: isSystem, modifyPath: !isSystem };
}

function customize(sysAvailable) {
    var defaultPrefix = sysAvailable ? SYSTEM_PREFIX : USER_PREFIX;
    printf("Install prefix [%s]: ", defaultPrefix);
    stdout.fflush();
    var prefix = askLine(defaultPrefix);

    /* if their custom prefix is under /usr/local, treat as system-style
       install -- they own it and want links.  otherwise user-style. */
    var isSystem = sysAvailable && /^\/usr\/local(\/|$)/.test(prefix);
    if (sysAvailable && !isSystem)
        isSystem = askYN("Treat as a system install (links in /usr/local/bin)?", false);

    var modifyPath;
    if (isSystem) {
        modifyPath = false; /* /usr/local/bin already on PATH */
    } else {
        modifyPath = askYN("Modify your shell rc files to add " + prefix + "/bin to PATH?", true);
    }

    return { prefix: prefix, isSystem: isSystem, modifyPath: modifyPath };
}

/* ============== filesystem dance ============== */

function ensureDir(path) {
    try { mkdir(path, true); } catch (e) {
        printf("ERROR: could not create %s: %s\n", path, e.message);
        process.exit(1);
    }
}

function writeFile(path, content, mode) {
    try {
        fwrite(path, content);
        if (mode != null) chmod(path, mode);
    } catch (e) {
        printf("ERROR: could not write %s: %s\n", path, e.message);
        process.exit(1);
    }
}

/* Write a file with execute bits set atomically, so there's no
   window where bash can find a 0644 version on PATH and refuse it. */
function writeExec(path, content) {
    var tmp = path + ".tmp." + new Date().getTime();
    try {
        fwrite(tmp, content);
        chmod(tmp, "755");
        exec("mv", tmp, path);   /* rename is atomic on the same fs */
    } catch (e) {
        try { exec("rm","-f",tmp); } catch (_) {}
        printf("ERROR: could not write %s: %s\n", path, e.message);
        process.exit(1);
    }
}

/* slice rampart binary out of this bundle and drop at <prefix>/bin/rampart */
function installBareRampart(prefix) {
    var off = payloadOffset();
    var all = readFile(process.installPathExec);
    var bare = all.subarray(0, off);
    ensureDir(prefix + "/bin");
    writeExec(prefix + "/bin/rampart", bare);
    printf("  installed: %s/bin/rampart (%d bytes)\n", prefix, bare.length);
}

/* drop bin/, modules/, lib/ (bundled system libs), licenses/ */
function extractPayload(prefix) {
    payloadExtract(prefix, ["bin/", "modules/", "lib/", "licenses/"]);
    printf("  installed: %s/{bin,modules,lib,licenses}/*\n", prefix);
}

/* ============== env files ============== */

function writeEnvFiles(prefix) {
    var posix =
"#!/bin/sh\n"+
"# rampart shell setup\n"+
"case \":${PATH}:\" in\n"+
"    *:\""+prefix+"/bin\":*)\n"+
"        ;;\n"+
"    *)\n"+
"        export PATH=\""+prefix+"/bin:$PATH\"\n"+
"        ;;\n"+
"esac\n";

    var csh =
"# rampart shell setup\n"+
"if (\"$path\" !~ *"+prefix+"/bin*) then\n"+
"    set path = ( "+prefix+"/bin $path )\n"+
"endif\n";

    var fish =
"# rampart shell setup\n"+
"if not contains "+prefix+"/bin $PATH\n"+
"    set -gx PATH "+prefix+"/bin $PATH\n"+
"end\n";

    writeFile(prefix + "/env",       posix, "644");
    writeFile(prefix + "/env.csh",   csh,   "644");
    writeFile(prefix + "/env.fish",  fish,  "644");
    printf("  wrote env: %s/env (+ .csh, .fish)\n", prefix);
}

/* ============== rc-file editing (rustup-style) ============== */

var RC_BEGIN = "# >>> rampart installer >>>";
var RC_END   = "# <<< rampart installer <<<";

function rcBlock(prefix, kind) {
    if (kind === "csh")  return RC_BEGIN + "\nsource \"" + prefix + "/env.csh\"\n"  + RC_END + "\n";
    if (kind === "fish") return RC_BEGIN + "\nsource \"" + prefix + "/env.fish\"\n" + RC_END + "\n";
    return                      RC_BEGIN + "\n. \""     + prefix + "/env\"\n"      + RC_END + "\n";
}

function rcFiles(homedir) {
    /* shells -> rc files; mirror rustup's discovery list */
    return [
        { kind: "posix", path: homedir + "/.profile" },
        { kind: "posix", path: homedir + "/.bashrc" },
        { kind: "posix", path: homedir + "/.bash_profile" },
        { kind: "posix", path: homedir + "/.bash_login" },
        { kind: "posix", path: homedir + "/.zshenv" },
        { kind: "posix", path: homedir + "/.zshrc" },     /* zsh interactive */
        { kind: "posix", path: homedir + "/.zprofile" },
        { kind: "csh",   path: homedir + "/.cshrc" },
        { kind: "csh",   path: homedir + "/.tcshrc" },
        { kind: "fish",  path: homedir + "/.config/fish/conf.d/rampart.fish" }
    ];
}

function modifyRcFiles(prefix, homedir) {
    var touched = [];
    var files = rcFiles(homedir);

    /* macOS convention: bash login shells read .bash_profile.  rustup
       creates one if it doesn't exist so the env source actually fires. */
    var isMac = /Darwin/i.test(shellOK("uname") || "");

    for (var i = 0; i < files.length; i++) {
        var f = files[i];
        var content = "";
        var existed = fileExists(f.path);

        if (!existed) {
            /* only auto-create the macOS .bash_profile or the fish
               conf.d/rampart.fish.  Everything else: skip silently. */
            if (f.kind === "fish") {
                ensureDir(homedir + "/.config/fish/conf.d");
            } else if (isMac && f.path === homedir + "/.bash_profile") {
                content = "# .bash_profile\n[ -f ~/.bashrc ] && . ~/.bashrc\n";
            } else {
                continue;
            }
        } else {
            try { content = readFile(f.path, true); }
            catch (e) { printf("  WARN: could not read %s: %s\n", f.path, e.message); continue; }
        }

        var blockRe = /# >>> rampart installer >>>[\s\S]*?# <<< rampart installer <<<\n?/;
        var newBlock = rcBlock(prefix, f.kind);
        var newContent;

        if (blockRe.test(content)) {
            newContent = content.replace(blockRe, newBlock);
        } else {
            if (content.length && content.charAt(content.length-1) !== "\n") content += "\n";
            newContent = content + "\n" + newBlock;
        }

        try { fwrite(f.path, newContent); touched.push(f.path); }
        catch (e) { printf("  WARN: could not update %s: %s\n", f.path, e.message); }
    }

    /* chown back to invoking user if we ran via sudo.  `chown user:` (trailing
       colon, no group name) tells BSD/GNU chown to use the user's primary
       group from /etc/passwd -- portable across Linux (where the personal
       per-user group convention makes "name:name" work too) and macOS/BSD
       (where regular accounts use the shared `staff` group and no per-user
       group exists). */
    if (me.name && process.env.SUDO_USER) {
        try { for (var j = 0; j < touched.length; j++) exec("chown", me.name+":", touched[j]); } catch(e) {}
    }

    return touched;
}

/* ============== symlinks (system install) ============== */

var BIN_NAMES = ["rampart","addtable","backref","kdbfchk","metamorph",
                 "rex","texislockd","tsql","rampart-uninstall.sh"];

function makeSymlinks(prefix) {
    /* If /usr/local/bin doesn't exist yet, try to create it.  We can only
       do that if we own /usr/local, which the new canSystemInstall()
       path (user owns prefix but not /usr/local) won't have -- so the
       ensureDir would fail.  Wrap in try and skip cleanly. */
    if (!fileExists("/usr/local/bin")) {
        try { ensureDir("/usr/local/bin"); }
        catch (e) {
            printf("  cannot create /usr/local/bin (%s); skipping symlink step.\n",
                   e.message);
            return [];
        }
    }
    /* Even if it exists, we still need write access.  EUID 0 always has it;
       a non-root user installing into a previously-chowned prefix usually
       doesn't.  Skip with a hint rather than spam a WARN per binary. */
    if (EUID !== 0 && uidOf("/usr/local/bin") !== me.uid) {
        printf("  /usr/local/bin is not writable; skipping symlink step.\n");
        printf("  (sudo re-install if you want /usr/local/bin/rampart shims.)\n");
        return [];
    }
    var made = [];
    for (var i = 0; i < BIN_NAMES.length; i++) {
        var name = BIN_NAMES[i];
        var src  = prefix + "/bin/" + name;
        var dst  = "/usr/local/bin/" + name;
        try { exec("rm","-f",dst); } catch(e) {}
        try { exec("ln","-s",src,dst); made.push(dst); }
        catch (e) { printf("  WARN: ln -s %s -> %s failed: %s\n", src, dst, e.message); }
    }
    printf("  linked %d binaries under /usr/local/bin/\n", made.length);
    return made;
}

/* ============== installed.json manifest ============== */

function buildManifest(prefix, isSystem, rcFilesTouched, symlinksMade) {
    /* the list of files the installer is responsible for at this prefix.
       used by uninstall.js to know what to delete vs. what's user data. */
    var coreFiles = ["bin/rampart"];

    /* every bin extra + every module we extracted from the payload */
    var pl = payloadList();
    for (var name in pl) {
        if (name.charAt(name.length-1) === "/") continue;  /* dir markers */
        if (name === "entry_script.js" || name === "uninstall.js") continue;
        coreFiles.push(name);
    }

    /* generated metadata */
    coreFiles.push("installed.json");
    coreFiles.push("bin/rampart-uninstall.sh");
    coreFiles.push("bin/rampart-uninstall.js");
    coreFiles.push("bin/rampart-install.js");
    coreFiles.push("bin/packages.js");

    /* env files only for user installs */
    if (!isSystem) {
        coreFiles.push("env");
        coreFiles.push("env.csh");
        coreFiles.push("env.fish");
    }

    coreFiles.sort();

    var nowIso = new Date().toISOString();
    return {
        schema:          1,
        rampart_version: rampart.version,
        installed_at:    nowIso,
        prefix:          prefix,
        profile:         isSystem ? "system" : "user",
        packages: {
            "core": {
                installed_by: "installer",
                installed_at: nowIso,
                version:      rampart.version,
                files:        coreFiles
            }
        },
        rc_files: rcFilesTouched.slice(),
        symlinks: symlinksMade.slice()
    };
}

function writeManifest(prefix, manifest) {
    var json = JSON.stringify(manifest, null, 2) + "\n";
    writeFile(prefix + "/installed.json", json, "644");
    printf("  wrote: %s/installed.json\n", prefix);
}

/* ============== rampart-uninstall.{sh,js} =========
 * rampart-uninstall.js (smart cleanup driver) is shipped in the bundle
 * and just copied to <prefix>/bin/.  rampart-uninstall.sh is a tiny
 * shim that execs rampart on the driver, falling back to an interactive
 * rm -rf if rampart is missing.
 */

function writeUninstallScripts(prefix) {
    /* uninstall.{sh,js} and rampart-install.js all land under
       <prefix>/bin/ so the shim is on PATH (via the env-file edits for
       user installs, or via the /usr/local/bin symlink for system
       installs).  rampart-install.js is invoked by the rampart binary
       when its --install flag is used; shipping it as a separate JS
       file lets us update the install logic without rebuilding the
       binary.  packages.js is the manifest read by both. */
    fwrite(prefix + "/bin/rampart-uninstall.js", payloadGet("uninstall.js"));
    chmod(prefix + "/bin/rampart-uninstall.js", "644");
    fwrite(prefix + "/bin/rampart-install.js",   payloadGet("rampart-install.js"));
    chmod(prefix + "/bin/rampart-install.js",    "644");
    fwrite(prefix + "/bin/packages.js",          payloadGet("packages.js"));
    chmod(prefix + "/bin/packages.js",           "644");

    var sh =
"#!/bin/sh\n"+
"# rampart uninstall shim -- delegates to rampart-uninstall.js for the\n"+
"# smart cleanup, with a manual rm -rf fallback if rampart is missing.\n"+
"# Self-locating so it works correctly when called via a /usr/local/bin\n"+
"# symlink.\n"+
"SELF=$(readlink -f \"$0\" 2>/dev/null || echo \"$0\")\n"+
"BIN=$(cd \"$(dirname \"$SELF\")\" && pwd)\n"+
"PREFIX=$(cd \"$BIN/..\" && pwd)\n"+
"\n"+
"if [ -x \"$BIN/rampart\" ] && [ -f \"$BIN/rampart-uninstall.js\" ]; then\n"+
"    exec \"$BIN/rampart\" \"$BIN/rampart-uninstall.js\"\n"+
"fi\n"+
"\n"+
"# Fallback: rampart binary or rampart-uninstall.js missing.  Offer a wipe.\n"+
"printf 'rampart appears damaged at %s.\\nRemove the entire directory? [y/N] ' \"$PREFIX\"\n"+
"read ans\n"+
"case \"$ans\" in\n"+
"    [yY]*) rm -rf \"$PREFIX\" && echo \"Removed $PREFIX.\" ;;\n"+
"    *)     echo \"Cancelled.\" ;;\n"+
"esac\n";

    writeExec(prefix + "/bin/rampart-uninstall.sh", sh);
    printf("  wrote: %s/bin/rampart-uninstall.{sh,js}\n", prefix);
}

/* ============== back up an existing install ==============
 * Don't rm -rf the prefix -- a user may keep their own data alongside
 * (databases, custom configs, scratch dirs).  Only the directories the
 * installer owns -- bin/ and modules/ -- get rotated out with a date
 * suffix.  Returns the list of {from,to} renames so the post-install
 * summary can report them.  Upgrade-in-place semantics come later.
 */
function dateStamp() {
    var d = new Date();
    function p(n) { return (n < 10 ? "0" : "") + n; }
    return d.getFullYear() + "-" + p(d.getMonth()+1) + "-" + p(d.getDate()) +
           "_" + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds());
}

function backupExistingInstall(prefix) {
    var moved = [];
    if (!fileExists(prefix)) return moved;

    var subdirs = ["bin", "modules"];
    var stamp = dateStamp();
    for (var i = 0; i < subdirs.length; i++) {
        var src = prefix + "/" + subdirs[i];
        if (!fileExists(src)) continue;
        var dst = prefix + "/" + subdirs[i] + "." + stamp;
        try { exec("mv", src, dst); }
        catch (e) {
            printf("ERROR: could not rename %s -> %s: %s\n", src, dst, e.message);
            process.exit(1);
        }
        moved.push({ from: src, to: dst });
    }
    return moved;
}

/* ============== root check ============== */

function ensurePermsForPrefix(prefix) {
    /* If the prefix is /usr/local-anything, we need write access there.
       Test the actual process EUID, not me.uid -- the latter is the
       SUDO_USER (the *invoking* user) so a real root shell with
       SUDO_USER inherited from its sudo parent would otherwise be
       rejected.
       Exception: the user already owns the exact prefix dir (e.g. a
       previous sudo install that we chown'd, or a manual chown to
       reclaim ownership).  In that case the installer can write its
       payload there without root -- only the /usr/local/bin symlink
       step needs root, and makeSymlinks() already skips cleanly when
       /usr/local/bin isn't ours. */
    if (/^\/usr\//.test(prefix) && EUID !== 0) {
        if (fileExists(prefix) && uidOf(prefix) === me.uid) return;
        printf("ERROR: installing to %s requires root.  Run with sudo.\n", prefix);
        process.exit(1);
    }
}

/* ============== main ============== */

(function main() {
    var defaultPrefix = canSystemInstall() ? SYSTEM_PREFIX : USER_PREFIX;
    banner(defaultPrefix, defaultPrefix === SYSTEM_PREFIX);

    var choice;
    if (AUTO) {
        var isSystem = canSystemInstall();
        choice = {
            prefix:      isSystem ? SYSTEM_PREFIX : USER_PREFIX,
            isSystem:    isSystem,
            modifyPath:  !isSystem    /* system install gets /usr/local/bin links instead */
        };
        printf("\nAuto install --> %s (%s profile)\n",
               choice.prefix, choice.isSystem ? "system" : "user");
    } else {
        choice = chooseProfile();
    }
    ensurePermsForPrefix(choice.prefix);

    printf("\nReady to install with:\n");
    printf("  prefix:        %s\n", choice.prefix);
    printf("  system links:  %s\n", choice.isSystem ? "yes (/usr/local/bin)" : "no");
    printf("  modify PATH:   %s\n", choice.modifyPath ? "yes (rc-file edits)" : "no");
    if (!AUTO && !askYN("\nProceed?", true)) {
        printf("Cancelled.\n");
        process.exit(0);
    }

    var moved = backupExistingInstall(choice.prefix);
    ensureDir(choice.prefix);

    printf("\nInstalling...\n");
    installBareRampart(choice.prefix);
    extractPayload(choice.prefix);

    var rcTouched = [];
    var linksMade = [];

    if (choice.modifyPath) {
        writeEnvFiles(choice.prefix);
        rcTouched = modifyRcFiles(choice.prefix, me.home);
        printf("  touched rc files:\n");
        for (var i = 0; i < rcTouched.length; i++)
            printf("    %s\n", rcTouched[i]);
    }

    if (choice.isSystem)
        linksMade = makeSymlinks(choice.prefix);

    writeUninstallScripts(choice.prefix);
    writeManifest(choice.prefix, buildManifest(choice.prefix, choice.isSystem, rcTouched, linksMade));

    /* If we ran under sudo, the install tree is currently owned by root.
       That makes routine maintenance (re-install over the top, edits
       under prefix/etc/, log rotation, etc.) require sudo forever after.
       Chown the whole install tree to the invoking user so they own
       what they just installed.  Trailing `:` keeps this portable
       across Linux (per-user group convention) and macOS/BSD (shared
       primary group). */
    if (EUID === 0 && me.name && process.env.SUDO_USER) {
        try {
            exec("chown", "-R", me.name + ":", choice.prefix);
            printf("  chown -R %s: %s\n", me.name, choice.prefix);
        } catch (e) {
            printf("  WARN: could not chown %s to %s: %s\n",
                   choice.prefix, me.name, e.message);
        }
    }

    printf("\nDone.\n");
    if (moved.length) {
        printf("\nYour previous install was preserved:\n");
        for (var mi = 0; mi < moved.length; mi++)
            printf("    %s  ->  %s\n", moved[mi].from, moved[mi].to);
        printf("Remove them at your leisure once you confirm the new install works.\n");
    }
    printf("\nUninstall any time with:  %s/bin/rampart-uninstall.sh\n", choice.prefix);

    /* The next two blocks (release notes + PATH hint) come AFTER the
       install.sh wrapper's "[install] Installer bundle is still at ..."
       line in the curl-pipe flow.  install.sh sets RAMPART_PIPE_INSTALL=1
       and reads /tmp/rampart-post-install.txt after the bundle exits.
       For direct-bundle invocations there is no install.sh; we print
       inline instead. */
    var postLines = [];

    /* release notes (optional) */
    var pl = payloadList();
    if (pl && pl["release-notes.txt"]) {
        var notes = payloadGet("release-notes.txt");
        var notesStr = (notes && notes.byteLength != null)
                       ? rampart.utils.bufferToString(notes)
                       : (""+notes);
        postLines.push("");
        postLines.push("[release notes]");
        postLines.push(notesStr.replace(/\n+$/, ""));
    }

    /* PATH hint (user install) or system-install hint */
    if (choice.modifyPath) {
        postLines.push("");
        postLines.push("To pick up the new PATH in this shell without logging out:");
        postLines.push("    . " + choice.prefix + "/env");
        postLines.push("Or just open a new shell.");
    } else if (choice.isSystem) {
        postLines.push("");
        postLines.push("rampart and its texis tools are linked from /usr/local/bin -- they");
        postLines.push("should be on your PATH already.  Try:  rampart --version");
    }

    if (postLines.length) {
        var postText = postLines.join("\n") + "\n";
        if (process.env.RAMPART_PIPE_INSTALL) {
            try { fwrite("/tmp/rampart-post-install.txt", postText); }
            catch (e) { /* fall back to inline print */
                printf("%s", postText);
            }
        } else {
            printf("%s", postText);
        }
    }
    printf("\n");
})();
