/* Test that the proxy extra thread handles requests independently of JS threads.
   With threads:1, the single JS thread is tied up by a sleep, but the proxy
   thread should still be able to serve requests. */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");

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
}

var nfailed = 0;

function testFeature(name, test)
{
    var error = false;
    if (typeof test == 'function') {
        try {
            test = test();
        } catch(e) {
            error = e;
            test = false;
        }
    }
    printf("testing proxy-thr- %-49s - ", name);
    if (test)
        printf("passed\n");
    else
    {
        nfailed++;
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        cleanup();
        process.exit(1);
    }
    if (error) console.log(error);
}

/* *** Upstream server on port 8102 *** */
upstream_pid = server.start({
    bind: "127.0.0.1:8102",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: process.scriptPath + '/proxy-thread-test-upstream-alog',
    errorLog:  process.scriptPath + '/proxy-thread-test-upstream-elog',
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
    accessLog: process.scriptPath + '/proxy-thread-test-proxy-alog',
    errorLog:  process.scriptPath + '/proxy-thread-test-proxy-elog',
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

/* Use exec to fire the slow request in a subprocess */
var slow_proc = exec("curl", { background: true, timeout: 10000 },
    "-s", "-o", "/dev/null", "-w", "%{http_code}",
    "http://127.0.0.1:8103/slow");

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
if (nfailed > 0)
{
    printf("\n%d test(s) FAILED.\n", nfailed);
    process.exit(1);
}
printf("\nAll proxy thread tests passed.\n");
