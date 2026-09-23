/* Test server.start({threadLimit}) - limited URL prefixes run on a
   pinned subset of the JS threads.  Handlers report the thread they ran
   on via rampart.thread.getCurrentId(); the tests check that limited
   paths never use more threads than allowed while unlimited paths are
   unaffected. */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var curl = require("rampart-curl");

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var alog = tmpdir + '/thread-limit-test-alog';
var elog = tmpdir + '/thread-limit-test-elog';
var pid = 0;
var base = "http://127.0.0.1:8290";
var NAP = 0.4;

function kill_server(p) {
    if (!kill(p, 0)) return;
    kill(p, 15);
    sleep(0.5);
    if (!kill(p, 0)) return;
    kill(p, 9);
    sleep(0.5);
}

function cleanup() {
    if (pid) kill_server(pid);
    rmFile(alog);
    rmFile(elog);
}

var testFeature = new (require('./test-feature.js'))({
    prefix: "thread-limit",
    onFail: function() { cleanup(); process.exit(1); }
});

function slow(req) {
    rampart.utils.sleep(0.4);
    return { json: { id: rampart.thread.getCurrentId() } };
}

function fast(req) {
    return { json: { id: rampart.thread.getCurrentId() } };
}

pid = server.start({
    bind: "127.0.0.1:8290",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: alog,
    errorLog:  elog,
    threads: 6,

    threadLimit: {
        "/tl/":     2,
        "/tl/one/": 1,
        "/g1/":     { threads: 3, group: "g" },
        "/g2/":     { threads: 3, group: "g" },
        "/big/":    50
    },

    map: {
        "/fast":        fast,
        "/free/slow":   slow,
        "/tl/slow":     slow,
        "/tl/one/slow": slow,
        "/g1/slow":     slow,
        "/g2/slow":     slow,
        "/big/slow":    slow
    }
});

testFeature.waitServer(base + "/fast");
testFeature("server is running", kill(pid, 0));

/* fetch n copies of each url in parallel; return {ids, statuses, elapsed} */
function burst(urls, n) {
    var list = [];
    for (var i = 0; i < n; i++)
        for (var j = 0; j < urls.length; j++)
            list.push(base + urls[j]);
    var ids = {}, statuses = {}, start = Date.now();
    curl.fetch(list, function(res) {
        statuses[res.status] = (statuses[res.status] || 0) + 1;
        if (res.status == 200)
            ids[JSON.parse(res.text).id] = true;
    });
    return { ids: Object.keys(ids), statuses: statuses, elapsed: (Date.now() - start) / 1000 };
}

testFeature("limited path serves a single request", function() {
    var res = curl.fetch(base + "/tl/slow");
    return res.status == 200 && typeof JSON.parse(res.text).id == 'number';
});

testFeature("startup log lists resolved thread sets", function() {
    var log = readFile(alog, true);
    return /threadLimit: \/tl\/ -> 2 threads \[5,4\]/.test(log) &&
           /threadLimit: \/tl\/one\/ -> 1 thread \[3\]/.test(log) &&
           /threadLimit: \/g1\/ -> 3 threads \[2,1,5\] group=g/.test(log) &&
           /threadLimit: \/g2\/ -> 3 threads \[2,1,5\] group=g/.test(log) &&
           /threadLimit: \/big\/ -> 5 threads \[4,3,2,1,5\] \(clamped/.test(log);
});

var tl;
testFeature("limited path: 8 concurrent requests use at most 2 threads", function() {
    tl = burst(["/tl/slow"], 8);
    return tl.statuses[200] === 8 && tl.ids.length <= 2;
});

testFeature("limited path: requests queue behind the 2 threads", function() {
    /* 8 requests over 2 threads is at least 4 rounds of the handler's nap */
    return tl.elapsed >= 4 * NAP;
});

testFeature("unlimited path: 8 concurrent requests spread across threads", function() {
    var r = burst(["/free/slow"], 8);
    return r.statuses[200] === 8 && r.ids.length >= 3 && r.elapsed < 4 * NAP;
});

testFeature("longest prefix wins: /tl/one/ pinned to a single thread", function() {
    var r = burst(["/tl/one/slow"], 4);
    return r.statuses[200] === 4 && r.ids.length == 1 && r.elapsed >= 4 * NAP;
});

testFeature("grouped paths share one set of 3 threads", function() {
    var r = burst(["/g1/slow", "/g2/slow"], 4);
    return r.statuses[200] === 8 && r.ids.length <= 3;
});

testFeature("oversized limit is clamped and still serves", function() {
    var r = burst(["/big/slow"], 3);
    return r.statuses[200] === 3;
});

testFeature("unlimited requests stay fast while the limited path is saturated", function() {
    /* tie up the two /tl/ threads from a helper thread, then time /fast */
    var thr = new rampart.thread();
    thr.exec(function() {
        var curl = require("rampart-curl");
        var list = [];
        for (var i = 0; i < 6; i++) list.push("http://127.0.0.1:8290/tl/slow");
        curl.fetch(list, function(res) {});
    });
    sleep(0.15);
    var worst = 0;
    for (var i = 0; i < 6; i++) {
        var start = Date.now();
        var res = curl.fetch(base + "/fast");
        var t = (Date.now() - start) / 1000;
        if (res.status != 200) return false;
        if (t > worst) worst = t;
    }
    sleep(1.5);
    return worst < NAP / 2;
});

testFeature("deep queue spreads evenly across the set", function() {
    /* 16 requests over 2 threads: 8 rounds if balanced.  Stale connection
       accounting once piled nearly all of these onto one thread. */
    var list = [], per = {}, ok = 0, start = Date.now();
    for (var i = 0; i < 16; i++) list.push(base + "/tl/slow");
    curl.fetch(list, function(res) {
        if (res.status != 200) return;
        ok++;
        var id = JSON.parse(res.text).id;
        per[id] = (per[id] || 0) + 1;
    });
    var elapsed = (Date.now() - start) / 1000, worst = 0;
    for (var id in per) if (per[id] > worst) worst = per[id];
    return ok === 16 && worst <= 10 && elapsed < 12 * NAP;
});

testFeature("limited path still healthy after load", function() {
    var res = curl.fetch(base + "/tl/slow");
    return res.status == 200;
});

testFeature("error log is empty", function() {
    var e = readFile(elog, true);
    return !e || e.trim() == "";
});

cleanup();
