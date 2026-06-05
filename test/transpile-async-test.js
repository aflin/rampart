"use transpilerGlobally"
rampart.globalize(rampart.utils);

chdir(process.scriptPath);

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var rpids = [];
var _hasShell = !!stat('/bin/bash');

/* Local echo server for the curl async tests.  The curl tests used to
   hit https://httpbin.org, which 503s/rate-limits intermittently and
   made the suite flap (non-200 -> spurious FAILED).  A self-hosted
   endpoint makes them deterministic and tests the async lowering, not
   the internet.  Plain HTTP on loopback — no cert needed. */
var CURL_BASE = 'http://127.0.0.1:18287';
var serverPid = 0;

/* ===================================================================
   Test harness
   =================================================================== */

var _baseTestFeature = new (require('./test-feature.js'))({prefix: "async"});

/* Drop stale .transpiled.js so we always test fresh transpiler output. */
try { rampart.utils.rmFile(process.scriptPath + '/transpile-async-test.transpiled.js'); } catch(e) {}

// Async-aware testFeature: supports both sync booleans and Promises
var _testQueue = [];
var _testRunning = false;

function testFeature(name, test) {
    if (typeof test === 'function') {
        try { test = test(); } catch(e) { test = false; }
    }
    if (test && typeof test === 'object' && typeof test.then === 'function') {
        _testQueue.push({ name: name, promise: test });
        _drainTests();
        return;
    }
    _baseTestFeature(name, !!test);
}

function _drainTests() {
    if (_testRunning || _testQueue.length === 0) return;
    _testRunning = true;
    var item = _testQueue.shift();
    item.promise.then(function(result) {
        _baseTestFeature(item.name, !!result);
        _testRunning = false;
        _drainTests();
    })['catch'](function(err) {
        _baseTestFeature(item.name, function(){ throw err; });
        _testRunning = false;
        _drainTests();
    });
}
testFeature.exit = function() { _baseTestFeature.exit(); };

/* ===================================================================
   Local echo server (for curl async tests)
   =================================================================== */

function start_echo_server() {
    var server = require("rampart-server");
    var pid = server.start({
        bind: "127.0.0.1:18287",
        daemon: true,        /* returns the detached server's pid */
        useThreads: true,
        map: {
            /* Returns 200 with a non-empty body, so res.status === 200
               and (with returnText) res.text is truthy.  Query strings
               are ignored — the tests vary them only to be distinct. */
            "/get": function(req) { return {text: "ok"}; }
        }
    });
    sleep(0.2);              /* let the forked server bind */
    if (!kill(pid, 0)) {
        fprintf(stderr, "Failed to start echo server\n");
        return false;
    }
    serverPid = pid;
    return true;
}

/* ===================================================================
   Redis server management
   =================================================================== */

function find_redis_exec() {
    if (_hasShell) {
        var ret = shell("which redis-server");
        if (ret.exitStatus == 0) return trim(ret.stdout);
        ret = shell("which redis6-server");
        if (ret.exitStatus == 0) return trim(ret.stdout);
        return '';
    }
    var paths = [
        '/usr/bin/redis-server',
        '/usr/local/bin/redis-server',
        'C:/tools/redis/redis-server.exe',
        'C:/tools/redis/redis-server'
    ];
    for (var i = 0; i < paths.length; i++) {
        if (stat(paths[i])) return paths[i];
    }
    return '';
}

function start_redis(port) {
    var rdexec = find_redis_exec();
    if (!rdexec) {
        fprintf(stderr, "Could not find redis-server!! SKIPPING REDIS ASYNC TESTS\n");
        return false;
    }
    var ret = exec(rdexec, {background: true}, "--port", port, "--dbfilename", port + "-dump.rdb", "--dir", tmpdir);
    sleep(0.5);
    if (!kill(ret.pid, 0)) {
        fprintf(stderr, "Failed to start redis-server\n");
        return false;
    }
    rpids.push(ret.pid);
    sleep(0.5);
    return true;
}

function kill_server(pid) {
    if (!kill(pid, 0)) return;
    kill(pid, 15);
    sleep(0.5);
    if (!kill(pid, 0)) return;
    kill(pid, 9);
    sleep(0.5);
    if (!kill(pid, 0)) return;
    fprintf(stderr, "WARNING: process %d could not be terminated\n", pid);
}

function cleanup() {
    kill_server(serverPid);
    rpids.forEach(function(rpid) {
        kill_server(rpid);
    });
    try {
        var files = readdir(tmpdir);
        if (files) {
            files.forEach(function(f) {
                if (f.match(/-dump\.rdb$/))
                    rmFile(tmpdir + '/' + f);
            });
        }
    } catch(e) {}
    try { rmdir(tmpdir); } catch(e) {}
    // remove transpiler cache file
    try { rmFile(process.scriptPath + '/transpile-async-test.transpiled.js'); } catch(e) {}
}

/* ===================================================================
   1. CURL fetchAsync / submitAsync Promise tests
   =================================================================== */

var curl = require('rampart-curl');

async function curlTests() {
    // Single URL, no callback — returns Promise
    var res = await curl.fetchAsync(CURL_BASE + '/get?test=1');
    testFeature("curl - await fetchAsync", res.status === 200);

    // With options
    var res2 = await curl.fetchAsync(CURL_BASE + '/get?test=2', {returnText: true});
    testFeature("curl - await fetchAsync with options", res2.status === 200 && !!res2.text);

    // Error handling
    var caught = false;
    try {
        await curl.fetchAsync('https://nonexistent.invalid/');
    } catch(e) {
        caught = (e.message.indexOf('curl failed') !== -1);
    }
    testFeature("curl - await fetchAsync error", caught);

    // Promise.all with multiple fetches
    var results = await Promise.all([
        curl.fetchAsync(CURL_BASE + '/get?p=1'),
        curl.fetchAsync(CURL_BASE + '/get?p=2'),
        curl.fetchAsync(CURL_BASE + '/get?p=3')
    ]);
    testFeature("curl - Promise.all fetchAsync",
        results.length === 3 &&
        results[0].status === 200 &&
        results[1].status === 200 &&
        results[2].status === 200);

    // Multiple URLs with callback — await waits for all
    var urls = [
        CURL_BASE + '/get?m=1',
        CURL_BASE + '/get?m=2',
        CURL_BASE + '/get?m=3'
    ];
    var collected = [];
    await curl.fetchAsync(urls, function(res) {
        collected.push(res.status);
    });
    testFeature("curl - await multi-URL with callback",
        collected.length === 3 &&
        collected[0] === 200 && collected[1] === 200 && collected[2] === 200);

    // submitAsync single, no callback
    var res3 = await curl.submitAsync({url: CURL_BASE + '/get?s=1'});
    testFeature("curl - await submitAsync", res3.status === 200);

    // Old callback style still works
    var cbResult = await new Promise(function(resolve) {
        curl.fetchAsync(CURL_BASE + '/get?old=1', function(res) {
            resolve(res.status === 200);
        });
    });
    testFeature("curl - callback style still works", cbResult);
}

/* ===================================================================
   2. REDIS async/await Promise tests
   =================================================================== */

async function redisTests(rcl) {
    // Setup
    rcl.set("await_key1", "hello");
    rcl.set("await_key2", "world");
    rcl.set("await_num", "42");

    // Basic get
    var val = await rcl.get({async: true}, "await_key1");
    testFeature("redis - await get", val === "hello");

    // Get number (auto-converted)
    var num = await rcl.get({async: true}, "await_num");
    testFeature("redis - await get number", num === 42);

    // Set and get
    var setret = await rcl.set({async: true}, "await_key3", "async_value");
    testFeature("redis - await set returns OK", setret === "OK");

    var getret = await rcl.get({async: true}, "await_key3");
    testFeature("redis - await get after set", getret === "async_value");

    // Multiple sequential awaits
    var v1 = await rcl.get({async: true}, "await_key1");
    var v2 = await rcl.get({async: true}, "await_key2");
    testFeature("redis - sequential awaits", v1 === "hello" && v2 === "world");

    // Delete
    var delret = await rcl.del({async: true}, "await_key3");
    testFeature("redis - await del", delret === 1);

    var gone = await rcl.get({async: true}, "await_key3");
    testFeature("redis - await get deleted key", gone === null);

    // Error handling
    var caught = false;
    try {
        await rcl.hget({async: true}, "await_key1", "field");
    } catch(e) {
        caught = (e.message.indexOf("WRONGTYPE") !== -1);
    }
    testFeature("redis - await error handling", caught);

    // List operations
    rcl.del("await_list");
    await rcl.rpush({async: true}, "await_list", "a", "b", "c");
    var llen = await rcl.llen({async: true}, "await_list");
    testFeature("redis - await rpush + llen", llen === 3);

    var lrange = await rcl.lrange({async: true}, "await_list", 0, -1);
    testFeature("redis - await lrange",
        Array.isArray(lrange) && lrange.length === 3 &&
        lrange[0] === "a" && lrange[2] === "c");

    // Exists
    var ex = await rcl.exists({async: true}, "await_key1");
    testFeature("redis - await exists (true)", ex === true);

    var nex = await rcl.exists({async: true}, "nonexistent_key");
    testFeature("redis - await exists (false)", nex === false);

    // Hash operations
    rcl.del("await_hash");
    await rcl.hset({async: true}, "await_hash", "f1", "v1", "f2", "v2");
    var hval = await rcl.hget({async: true}, "await_hash", "f1");
    testFeature("redis - await hset + hget", hval === "v1");

    var hall = await rcl.hgetall({async: true}, "await_hash");
    testFeature("redis - await hgetall",
        typeof hall === 'object' && hall.f1 === "v1" && hall.f2 === "v2");

    // Callback style still works alongside Promises
    var cbResult = await new Promise(function(resolve) {
        rcl.get({async: true}, "await_key1", function(res, err) {
            resolve(res === "hello" && !err);
        });
    });
    testFeature("redis - callback style still works", cbResult);

    // Cleanup
    rcl.del("await_key1", "await_key2", "await_num", "await_list", "await_hash");
}

/* ===================================================================
   Run tests
   =================================================================== */

async function main() {
    try {
        printf("=== curl async/await tests ===\n");
        if (start_echo_server())
            await curlTests();
        else
            fprintf(stderr, "SKIPPING CURL ASYNC TESTS\n");

        printf("\n=== redis async/await tests ===\n");
        var redis = require("rampart-redis");
        var rcl;

        try {
            rcl = new redis.init(13287);
        } catch(e) {
            if (!start_redis(13287)) {
                fprintf(stderr, "SKIPPING REDIS ASYNC TESTS\n");
                return;
            }
            rcl = new redis.init(13287);
        }

        rcl.flushall();
        await redisTests(rcl);
    } finally {
        /* Always run cleanup so test-spawned redis-server processes
           are reaped — even on uncaught error / SIGINT.  Without
           this, an aborted run leaves a redis-server bound to
           13287 (PPID reparents to init), and the next test run
           connects to that orphan instead of spawning fresh.  The
           orphan's `--dir` points at a since-deleted tmpdir, so
           bgsave fails, `stop-writes-on-bgsave-error` engages, and
           every SET/GET/PING returns -MISCONF. */
        cleanup();
        testFeature.exit();
    }
}

main()['catch'](function(e) {
    fprintf(stderr, "main() rejected: %s\n", e && (e.stack || e.message || String(e)));
});
