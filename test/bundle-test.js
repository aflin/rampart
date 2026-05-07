#!/usr/bin/env rampart
/*
   bundle-test.js -- exercises the single-file-bundle feature:

     1. Stage a tiny app directory with an entry_script.js and a couple of
        resources (a .js module, a data file, an include target).
     2. Zip it and concatenate onto a copy of the rampart binary.
     3. Run the resulting bundle in two modes:
          a) bare invocation  -> auto-runs entry_script.js
          b) explicit zip path -> runs :zip:/admin/run.js
     4. Parse a tagged "RESULT key=value" stdout protocol and assert each
        expected check passed.
     5. Remove all artifacts.

   Critical surface covered:
     * SFX zip detection / payload parsing
     * entry_script auto-run + process.scriptPath==":zip:"
     * argv splice for auto-run
     * explicit ':zip:/path' positional script
     * require(":zip:/...")           (absolute zip require)
     * require("./mod.js")            (relative require resolves into zip)
     * rampart.utils.readFile/stat/readdir on :zip:
     * rampart.utils.fopen("w") -> EROFS on :zip:
     * rampart.utils.payloadList()
     * rampart.include(":zip:/...")
*/

rampart.globalize(rampart.utils);

var _nfailed = 0;
function check(name, ok)
{
    printf("testing bundle  - %-50s - ", name);
    if (ok) printf("passed\n");
    else { printf(">>>>> FAILED <<<<<\n"); _nfailed++; }
}

/* ---- locate the rampart binary ---------------------------------- */

var RAMPART_BIN = process.installPathExec;
if (!RAMPART_BIN || !stat(RAMPART_BIN)) {
    fprintf(stderr,
        "bundle-test: cannot locate rampart binary (process.installPathExec=%J)\n",
        process.installPathExec);
    process.exit(1);
}

/* ---- check 'zip' is on the path; skip if not -------------------- */

var _hasShell = !!stat('/bin/sh');
function which_zip() {
    if (_hasShell) {
        var r = shell("which zip");
        if (r.exitStatus === 0) return trim(r.stdout);
    }
    var p = ['/usr/bin/zip', '/usr/local/bin/zip', '/bin/zip'];
    for (var i = 0; i < p.length; i++) if (stat(p[i])) return p[i];
    return '';
}
var ZIP = which_zip();
if (!ZIP) {
    fprintf(stderr,
        "bundle-test: 'zip' not in PATH; SKIPPING bundle tests\n");
    process.exit(0);
}

/* ---- stage a temp build tree ------------------------------------- */

var work    = '/tmp/rampart-bundle-test-' + process.getpid();
var stage   = work + '/staged';
var bundle  = work + '/myapp-bin';
var zipfile = work + '/payload.zip';

try { mkdir(work, true); } catch(e) {}
mkdir(stage, true);
mkdir(stage + '/lib', true);
mkdir(stage + '/admin', true);

/* entry_script -- runs when the bundle is invoked with no args.
   Emits "RESULT key=value" lines that the parent process verifies.
   We pass everything through `fprintf(file, "%s", body)` so % chars in
   the embedded JS source are not interpreted by the host fprintf. */
var entry_src =
    'rampart.globalize(rampart.utils);\n' +
    'var u = rampart.utils;\n' +
    /* process.scriptPath should be ":zip:" */
    'printf("RESULT scriptPath=%s\\n", process.scriptPath);\n' +
    /* argv splice: argv[1] should be entry_script.js */
    'printf("RESULT argv1=%s\\n", process.argv[1]);\n' +
    /* require(":zip:/lib/mod.js") */
    'var m1 = require(":zip:/lib/mod.js");\n' +
    'printf("RESULT zipRequire=%s\\n", m1.tag);\n' +
    /* relative require -- should resolve into the zip */
    'var m2 = require("./lib/mod.js");\n' +
    'printf("RESULT relRequire=%s\\n", m2.tag);\n' +
    /* readFile from :zip: */
    'var s = u.readFile(":zip:/data.txt", true);\n' +
    'printf("RESULT readFile=%s\\n", s.replace(/\\n/g,"\\\\n"));\n' +
    /* stat from :zip: */
    'var st = u.stat(":zip:/data.txt");\n' +
    'printf("RESULT statSize=%d\\n", st.size);\n' +
    /* readdir on :zip: root */
    'var entries = u.readdir(":zip:").sort().join(",");\n' +
    'printf("RESULT readdirRoot=%s\\n", entries);\n' +
    /* fopen("w") on :zip: must fail with EROFS */
    'var threw = "";\n' +
    'try { u.fopen(":zip:/data.txt", "w"); } catch(e) { threw = e.message; }\n' +
    'printf("RESULT fopenWrite=%s\\n", /Read-only/.test(threw) ? "EROFS" : threw);\n' +
    /* payloadList: must be an object with at least the entries we put in */
    'var pl = u.payloadList();\n' +
    'printf("RESULT payloadHas=%s\\n",\n' +
    '       (pl["entry_script.js"] && pl["data.txt"] && pl["lib/mod.js"]) ? "yes" : "no");\n' +
    /* include() into the bundle */
    'rampart.include(":zip:/inc.js");\n' +
    'printf("RESULT include=%s\\n", typeof __included === "string" ? __included : "missing");\n' +
    'process.exit(0);\n';
fprintf(stage + '/entry_script.js', "%s", entry_src);

/* a small require()-able module inside the bundle */
fprintf(stage + '/lib/mod.js', "%s",
    'module.exports = { tag: "MOD-OK" };\n');

/* a data file */
fprintf(stage + '/data.txt', "%s", "hello-from-zip\n");

/* an include() target */
fprintf(stage + '/inc.js', "%s",
    '__included = "INC-OK";\n');

/* an explicit-path script for the second invocation */
var admin_src =
    'var printf = rampart.utils.printf;\n' +
    'printf("RESULT explicitMode=ADMIN-OK\\n");\n' +
    'printf("RESULT explicitArgs=%s,%s\\n",\n' +
    '       process.argv[1], process.argv[2] || "(none)");\n' +
    'process.exit(0);\n';
fprintf(stage + '/admin/run.js', "%s", admin_src);

/* ---- build the bundle: zip the stage tree, cat onto rampart ------ */

var r = exec(ZIP, "-qr", zipfile, ".", { changeDirectory: stage });
if (r.exitStatus !== 0) {
    fprintf(stderr, "bundle-test: zip failed: %J\n", r);
    cleanup(); process.exit(1);
}
copyFile(RAMPART_BIN, bundle);
chmod(bundle, "755");

/* concat zip after binary.  Read zip into a buffer, append via
   fwrite(filename, data, append=true).  ('rb'/'ab' are rejected by
   rampart.utils.fopen, and we want a shell-free path so the test runs
   without /bin/sh.) */
{
    var zipBytes = readFile(zipfile);            /* Buffer */
    fwrite(bundle, zipBytes, true);              /* append */
}

/* ---- run #1: bare invocation -> entry_script auto-runs ---------- */

var got = exec(bundle);
if (got.exitStatus !== 0) {
    fprintf(stderr, "bundle-test: bundle (auto) exited %d, stderr:\n%s\n",
            got.exitStatus, got.stderr);
    cleanup(); process.exit(1);
}

/* parse RESULT lines into a map */
var results = {};
got.stdout.split('\n').forEach(function(line) {
    var m = line.match(/^RESULT (\w+)=(.*)$/);
    if (m) results[m[1]] = m[2];
});

check("entry_script auto-run + scriptPath",
      results.scriptPath === ":zip:");
check("argv splice (argv[1] = entry_script.js)",
      results.argv1 === "entry_script.js");
check("require(:zip:/lib/mod.js)",
      results.zipRequire === "MOD-OK");
check("require('./lib/mod.js') relative resolves into zip",
      results.relRequire === "MOD-OK");
check("readFile(:zip:/data.txt)",
      results.readFile === "hello-from-zip\\n");
check("stat(:zip:/data.txt).size",
      results.statSize === "15"); /* "hello-from-zip\n" = 15 bytes */
check("readdir(:zip:) lists entries",
      results.readdirRoot &&
      results.readdirRoot.indexOf("entry_script.js") !== -1 &&
      results.readdirRoot.indexOf("data.txt") !== -1 &&
      results.readdirRoot.indexOf("lib") !== -1);
check("fopen(:zip:, 'w') -> EROFS",
      results.fopenWrite === "EROFS");
check("payloadList contains expected names",
      results.payloadHas === "yes");
check("rampart.include(:zip:/inc.js)",
      results.include === "INC-OK");

/* ---- run #2: explicit ':zip:/admin/run.js' positional arg ------- */

got = exec(bundle, ":zip:/admin/run.js", "alpha");
if (got.exitStatus !== 0) {
    fprintf(stderr, "bundle-test: bundle (explicit) exited %d, stderr:\n%s\n",
            got.exitStatus, got.stderr);
    cleanup(); process.exit(1);
}
results = {};
got.stdout.split('\n').forEach(function(line) {
    var m = line.match(/^RESULT (\w+)=(.*)$/);
    if (m) results[m[1]] = m[2];
});

check("explicit ':zip:/path' script runs",
      results.explicitMode === "ADMIN-OK");
check("explicit-mode argv (argv[1] is the :zip:/ path)",
      results.explicitArgs === ":zip:/admin/run.js,alpha");

/* ---- cleanup ----------------------------------------------------- */

function cleanup()
{
    function rmrf(d) {
        var es;
        try { es = readdir(d); } catch(e) { return; }
        for (var i = 0; i < es.length; i++) {
            if (es[i] === '.' || es[i] === '..') continue;
            var p = d + '/' + es[i];
            var s = lstat(p);
            if (s && s.isDirectory && !s.isSymbolicLink) rmrf(p);
            else try { rmFile(p); } catch(e) {}
        }
        try { rmdir(d); } catch(e) {}
    }
    rmrf(stage);
    try { rmFile(zipfile); } catch(e) {}
    try { rmFile(bundle);  } catch(e) {}
    try { rmdir(work);     } catch(e) {}
}

cleanup();

printf("\nbundle tests complete: ");
if (_nfailed) printf("%d FAILED\n", _nfailed);
else          printf("all passed\n");
process.exit(_nfailed ? 1 : 0);
