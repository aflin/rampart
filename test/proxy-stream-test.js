/* Streaming through the reverse proxy: body pieces reach the client as the
   upstream sends them (SSE), `timeout` is a headers deadline and then an
   idle limit, and the usual small responses still work. */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");
var crypto = require("rampart-crypto");

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var upstream_pid = 0, proxy_pid = 0;
var UP = "http://127.0.0.1:8070", PX = "http://127.0.0.1:8071";
var logs = ['up-alog', 'up-elog', 'px-alog', 'px-elog'].map(function(n) {
    return tmpdir + '/proxy-stream-test-' + n;
});

function kill_server(pid) {
    if (!kill(pid, 0)) return;
    kill(pid, 15);
    sleep(0.5);
    if (!kill(pid, 0)) return;
    kill(pid, 9);
    sleep(0.5);
}

function cleanup() {
    if (proxy_pid) kill_server(proxy_pid);
    if (upstream_pid) kill_server(upstream_pid);
    for (var i = 0; i < logs.length; i++) rmFile(logs[i]);
}

var testFeature = new (require('./test-feature.js'))({
    prefix: "proxy-stream",
    onFail: function() { cleanup(); process.exit(1); }
});

/* ---- upstream: a rampart server that streams ---- */

/* ten ticks; rampart sends the first chunk after the first chunkDelay */
function ticks(req) {
    var d = "data: tick " + req.chunkIndex + "\n\n";
    if (req.chunkIndex < 9) req.chunkSend(d);
    else req.chunkEnd(d);
}

function twoticks(req) {
    var d = "data: tick " + req.chunkIndex + "\n\n";
    if (req.chunkIndex < 1) req.chunkSend(d);
    else req.chunkEnd(d);
}

var bigstr = "x".repeat(8 * 1024 * 1024);   /* 8 MB */

upstream_pid = server.start({
    bind: "127.0.0.1:8070",
    daemon: true,
    log: true,
    user: 'nobody',
    threads: 4,
    accessLog: logs[0],
    errorLog:  logs[1],
    map: {
        /* ten ticks 300 ms apart: a 3 s stream */
        "/sse":     function(req) {
            return { chunk: true, chunkDelay: 300, txt: ticks,
                     headers: { "Content-Type": "text/event-stream" } };
        },
        /* headers at once, then 4 s of silence before the first tick */
        "/gap":     function(req) {
            return { chunk: true, chunkDelay: 4000, txt: twoticks,
                     headers: { "Content-Type": "text/event-stream" } };
        },
        /* no headers for 6 s */
        "/slowhdr": function(req) { rampart.utils.sleep(6); return { text: "late" }; },
        "/plain":   function(req) { return { text: "hello plain" }; },
        "/nobody":  function(req) { return { status: 204, text: "" }; },
        "/big":     function(req) { return { text: bigstr }; }
    }
});

testFeature.waitServer(UP + "/plain");
testFeature("upstream is running", kill(upstream_pid, 0));

/* ---- proxy ---- */

proxy_pid = server.start({
    bind: "127.0.0.1:8071",
    daemon: true,
    log: true,
    user: 'nobody',
    threads: 2,
    accessLog: logs[2],
    errorLog:  logs[3],
    map: {
        "/":      { proxy: UP + "/" },
        "/t2/":   { proxy: UP + "/", timeout: 2 },
        "/down/": { proxy: "http://127.0.0.1:8079/" }
    }
});

testFeature.waitServer(PX + "/plain");
testFeature("proxy is running", kill(proxy_pid, 0));

/* fetch with per-chunk arrival times */
function timed(url, opts) {
    var r = { times: [], body: "", start: Date.now() };
    opts = opts || {};
    opts.chunkCallback = function(res) {
        r.times.push((Date.now() - r.start) / 1000);
        r.body += bufferToString(res.body);
    };
    opts.callback = function(res) {
        r.status = res.status;
        r.errMsg = res.errMsg;
        r.headers = res.headers;
    };
    curl.fetch(url, opts);
    r.elapsed = (Date.now() - r.start) / 1000;
    return r;
}

function hasTicks(body, n) {
    for (var i = 0; i < n; i++)
        if (body.indexOf("data: tick " + i + "\n\n") < 0) return false;
    return true;
}

var direct = timed(UP + "/sse");
testFeature("upstream itself streams ten ticks", function() {
    return direct.status == 200 && hasTicks(direct.body, 10) && direct.times.length >= 5;
});

var viaproxy = timed(PX + "/sse");
testFeature("proxied ticks arrive as they are sent, not at the end", function() {
    /* first piece well before the stream ends; pieces spread over the run */
    return viaproxy.status == 200 && hasTicks(viaproxy.body, 10) &&
           viaproxy.times.length >= 5 &&
           viaproxy.times[0] < 0.7 &&
           viaproxy.times[viaproxy.times.length - 1] - viaproxy.times[0] >= 2.0;
});

testFeature("timeout is not a total deadline: a 3 s stream finishes under timeout 2", function() {
    var r = timed(PX + "/t2/sse");
    return r.status == 200 && hasTicks(r.body, 10) && r.elapsed >= 2.5;
});

testFeature("timeout is an idle limit: 4 s silence is cut after about 2 s", function() {
    /* headers arrived (200) but the body was cut short: curl reports a
       partial transfer, nothing after the cut, and no terminating chunk */
    var r = timed(PX + "/t2/gap");
    return r.status == 200 && r.body.indexOf("tick") < 0 &&
           /partial/i.test(r.errMsg || "") &&
           r.elapsed >= 1.8 && r.elapsed < 3.5;
});

testFeature("no headers within timeout: 504", function() {
    var start = Date.now();
    var res = curl.fetch(PX + "/t2/slowhdr");
    var t = (Date.now() - start) / 1000;
    return res.status == 504 && t >= 1.8 && t < 3.5;
});

testFeature("upstream down: 502", function() {
    var res = curl.fetch(PX + "/down/x");
    return res.status == 502;
});

testFeature("Content-Length response passes through with its length", function() {
    var res = curl.fetch(PX + "/plain");
    var h = res.headers || {};
    var cl = h["Content-Length"] || h["content-length"];
    var te = h["Transfer-Encoding"] || h["transfer-encoding"];
    return res.status == 200 && res.text == "hello plain" && cl == "11" && !te;
});

testFeature("204 response has no body and completes", function() {
    var res = curl.fetch(PX + "/nobody");
    return res.status == 204 && res.text == "";
});

testFeature("HEAD completes", function() {
    var res = curl.fetch(PX + "/plain", { "nobody": true });
    return res.status == 200;
});

testFeature("8 MB body intact through the proxy to a rate-limited client", function() {
    var res = curl.fetch(PX + "/big", { "limit-rate": 4000000 });
    return res.status == 200 && res.body.length == bigstr.length &&
           crypto.sha256(res.body) == crypto.sha256(bigstr);
});

function fdcount(pid) {
    var d = stat("/proc/" + pid + "/fd") ? readdir("/proc/" + pid + "/fd") : null;
    return d ? d.length : -1;
}

testFeature("clients leaving mid-stream do not pile up on the proxy", function() {
    /* leave 20 streams early, then let things settle */
    var before = fdcount(proxy_pid);
    for (var i = 0; i < 20; i++)
        curl.fetch(PX + "/sse", { "max-time": 0.4 });
    sleep(2);
    var after = fdcount(proxy_pid);
    var res = curl.fetch(PX + "/plain");
    return res.status == 200 && res.text == "hello plain" &&
           (before < 0 || after <= before + 2);
});

testFeature("proxy error log is empty", function() {
    var e = readFile(logs[3], true);
    return !e || e.trim() == "";
});

cleanup();
