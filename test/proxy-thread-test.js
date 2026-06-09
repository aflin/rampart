/* Test that the proxy extra thread handles requests independently of JS threads.
   With threads:1, the single JS thread is tied up by a sleep, but the proxy
   thread should still be able to serve requests. */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var upstream_pid = 0;
var proxy_pid = 0;

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
    if (proxy_pid) kill_server(proxy_pid);
    if (upstream_pid) kill_server(upstream_pid);
    rmFile(tmpdir + '/proxy-thread-test-upstream-alog');
    rmFile(tmpdir + '/proxy-thread-test-upstream-elog');
    rmFile(tmpdir + '/proxy-thread-test-proxy-alog');
    rmFile(tmpdir + '/proxy-thread-test-proxy-elog');
}

var testFeature = new (require('./test-feature.js'))({
    prefix: "proxy-thr",
    onFail: function() { cleanup(); process.exit(1); }
});

/* *** Upstream server on port 8102 *** */
upstream_pid = server.start({
    bind: "127.0.0.1:8102",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/proxy-thread-test-upstream-alog',
    errorLog:  tmpdir + '/proxy-thread-test-upstream-elog',
    useThreads: true,

    map: {
        "/hello": function(req) {
            return { text: "hello from upstream" };
        }
    }
});

sleep(0.5);
testFeature("upstream server is running", kill(upstream_pid, 0));

/* *** Proxy server on port 8103 with only 1 JS thread *** */
proxy_pid = server.start({
    bind: "127.0.0.1:8103",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/proxy-thread-test-proxy-alog',
    errorLog:  tmpdir + '/proxy-thread-test-proxy-elog',
    threads: 1,

    map: {
        "/slow": function(req) {
            sleep(3);
            return { text: "slow done" };
        },
        "/fast": function(req) {
            return { text: "fast done" };
        },
        "/proxied/": { proxy: "http://127.0.0.1:8102/" }
    }
});

sleep(0.5);
testFeature("proxy server is running", kill(proxy_pid, 0));

/* Sanity: both routes work when idle */
testFeature("JS route works when idle", function() {
    var res = curl.fetch("http://127.0.0.1:8103/fast");
    return res.status == 200 && res.text == "fast done";
});

testFeature("proxy route works when idle", function() {
    var res = curl.fetch("http://127.0.0.1:8103/proxied/hello");
    return res.status == 200 && res.text == "hello from upstream";
});

/* Now the real test: fire off a slow request to tie up the JS thread,
   then check that the proxy still responds while JS is blocked. */

/* Start the slow request in the background using curl async */
var slow_done = false;
var slow_ok = false;
var proxy_during_slow_ok = false;
var js_during_slow_blocked = true;

/* Fire the slow request in a rampart thread so we don't depend on
   a system curl(1) being installed.  thr.exec is fire-and-forget;
   the child thread runs curl.fetch synchronously and blocks for the
   3-second /slow handler. */
var slow_thr = new rampart.thread();
slow_thr.exec(function() {
    var curl = require("rampart-curl");
    curl.fetch({"max-time": 10}, "http://127.0.0.1:8103/slow");
});

/* Give the slow request a moment to reach the server and start sleeping */
sleep(0.5);

/* Test 1: proxy should respond while the JS thread is sleeping */
testFeature("proxy works while JS thread is blocked", function() {
    var res = curl.fetch({"max-time": 4}, "http://127.0.0.1:8103/proxied/hello");
    if (res.status == 200 && res.text == "hello from upstream") {
        proxy_during_slow_ok = true;
        return true;
    }
    printf("\nstatus=%d text='%s'\n", res.status, res.text);
    return false;
});

/* Test 2: another JS request should block (we use a short timeout to prove it) */
testFeature("JS route blocks while thread is busy", function() {
    var start = new Date().getTime();
    var res = curl.fetch({"max-time": 1}, "http://127.0.0.1:8103/fast");
    var elapsed = (new Date().getTime() - start) / 1000;
    /* If the JS thread is tied up, curl should time out (status 0 or 28)
       or take close to the full timeout. If it returns instantly with
       "fast done", the thread wasn't actually blocked. */
    if (res.status == 200 && res.text == "fast done" && elapsed < 0.5) {
        /* responded instantly - thread was NOT blocked */
        printf("\nfast returned instantly: status=%d text='%s' elapsed=%.3f\n",
               res.status, res.text, elapsed);
        return false;
    }
    /* either timed out or took a long time - thread was blocked */
    return true;
});

/* Wait for the slow request to finish */
sleep(4);

/* Test 3: after the slow request finishes, JS route works again */
testFeature("JS route works after slow request completes", function() {
    var res = curl.fetch({"max-time": 5}, "http://127.0.0.1:8103/fast");
    if (res.status != 200 || res.text != "fast done") {
        printf("\nstatus=%d text='%s' errMsg='%s'\n", res.status, res.text, res.errMsg || "");
        return false;
    }
    return true;
});

cleanup();
testFeature.exit();
