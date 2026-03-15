/* Test module dependency tracking: when a submodule changes on disk,
   parent modules that require() it should be reloaded automatically. */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");

var nfailed = 0;
var server_pid = 0;

var iam = trim(exec('whoami').stdout);
var tmpdir = process.scriptPath + '/tmp-test';
var moddir = tmpdir + '/dep_test_mods';
var accessLog = tmpdir + '/dep-test-alog';
var errorLog  = tmpdir + '/dep-test-elog';

function cleanup() {
    if (server_pid) {
        kill(server_pid, 15);
        sleep(0.5);
        if (kill(server_pid, 0)) kill(server_pid, 9);
        sleep(0.2);
    }
    rmFile(accessLog);
    rmFile(errorLog);
    shell("rm -rf " + moddir);
}

function testFeature(name, test) {
    var error = false;
    if (typeof test == 'function') {
        try { test = test(); } catch(e) { error = e; test = false; }
    }
    printf("testing module-deps - %-46s - ", name);
    if (test)
        printf("passed\n");
    else {
        nfailed++;
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        cleanup();
        process.exit(1);
    }
}

/* ===== Setup: create temp modules ===== */

if (!stat(tmpdir))
    mkdir(tmpdir);

if (stat(moddir))
    shell("rm -rf " + moddir);

mkdir(moddir);

/* module_b: leaf module with a value */
fprintf(moddir + "/module_b.js", '%s',
    'module.exports = { val: "original" };\n');

/* module_a: requires module_b */
fprintf(moddir + "/module_a.js", '%s',
    'var b = require("' + moddir + '/module_b");\n' +
    'module.exports = { val: b.val };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir, recurse: true});
}

/* ===== Test 1: basic require + dep tracking ===== */

testFeature("initial require loads correctly", function() {
    var mod = require(moddir + "/module_a");
    return mod.val === "original";
});

/* Wait so mtime changes */
sleep(1.1);

/* Rewrite module_b with a new value */
fprintf(moddir + "/module_b.js", '%s',
    'module.exports = { val: "updated" };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir + "/module_b.js"});
}

testFeature("dep change triggers parent reload", function() {
    var mod = require(moddir + "/module_a");
    return mod.val === "updated";
});

/* ===== Test 2: transitive deps (A -> B -> C) ===== */

fprintf(moddir + "/module_c.js", '%s',
    'module.exports = { cval: "c_original" };\n');

sleep(1.1);

/* Rewrite module_b to require module_c */
fprintf(moddir + "/module_b.js", '%s',
    'var c = require("' + moddir + '/module_c");\n' +
    'module.exports = { val: "b_with_c", cval: c.cval };\n');

/* Rewrite module_a to expose cval too */
fprintf(moddir + "/module_a.js", '%s',
    'var b = require("' + moddir + '/module_b");\n' +
    'module.exports = { val: b.val, cval: b.cval };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir, recurse: true});
}

testFeature("transitive deps load correctly", function() {
    var mod = require(moddir + "/module_a");
    return mod.val === "b_with_c" && mod.cval === "c_original";
});

sleep(1.1);

/* Change only module_c — module_a should detect it via transitive deps */
fprintf(moddir + "/module_c.js", '%s',
    'module.exports = { cval: "c_updated" };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir + "/module_c.js"});
}

testFeature("transitive dep change triggers reload chain", function() {
    var mod = require(moddir + "/module_a");
    return mod.cval === "c_updated";
});

/* ===== Test 3: unchanged dep does NOT reload ===== */

testFeature("unchanged deps return cached module", function() {
    var mod1 = require(moddir + "/module_a");
    var mod2 = require(moddir + "/module_a");
    return mod1 === mod2;  /* same object reference = cached */
});

/* ===== Test 4: server path via getmod() ===== */

/* Write server modules */
fprintf(moddir + "/srv_sub.js", '%s',
    'module.exports = { msg: "hello_v1" };\n');

fprintf(moddir + "/srv_main.js", '%s',
    'var sub = require("' + moddir + '/srv_sub");\n' +
    'module.exports = function(req) {\n' +
    '    return { text: sub.msg };\n' +
    '};\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir, recurse: true});
}

server_pid = server.start({
    bind: "127.0.0.1:8119",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: accessLog,
    errorLog:  errorLog,
    map: {
        "/test/": {modulePath: moddir}
    }
});

sleep(0.5);
testFeature("server is running", kill(server_pid, 0));

testFeature("server returns initial value", function() {
    var res = curl.fetch("http://127.0.0.1:8119/test/srv_main");
    return res.status == 200 && trim(res.text) == "hello_v1";
});

sleep(1.1);

/* Modify the submodule */
fprintf(moddir + "/srv_sub.js", '%s',
    'module.exports = { msg: "hello_v2" };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir + "/srv_sub.js"});
}

testFeature("server detects dep change", function() {
    var res = curl.fetch("http://127.0.0.1:8119/test/srv_main");
    return res.status == 200 && trim(res.text) == "hello_v2";
});

/* Change the sub a second time to confirm repeatable */
sleep(1.1);

fprintf(moddir + "/srv_sub.js", '%s',
    'module.exports = { msg: "hello_v3" };\n');

if (iam == "root") {
    chown({user: "nobody", path: moddir + "/srv_sub.js"});
}

testFeature("server detects second dep change", function() {
    var res = curl.fetch("http://127.0.0.1:8119/test/srv_main");
    return res.status == 200 && trim(res.text) == "hello_v3";
});

/* ===== Cleanup ===== */
cleanup();

if (nfailed > 0) {
    printf("\n%d test(s) FAILED.\n", nfailed);
    process.exit(1);
}

printf("\nAll module dependency tests passed.\n");
