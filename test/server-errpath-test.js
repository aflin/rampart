/* Error paths must send exactly one response and must not recurse:
   - a modulePath route whose path fails validation replied twice
   - a notFound handler that throws used to re-enter itself until the
     stack ran out */
rampart.globalize(rampart.utils);
var server = require("rampart-server");
var curl   = require("rampart-curl");

var testFeature = new (require('./test-feature.js'))({
    prefix: "server-errpath",
    onFail: function() { if (!process.env.SERVER_TEST_KEEPGOING) { cleanup(); process.exit(1); } }
});

var port = 8747, base = "http://127.0.0.1:" + port;
var pid = 0;
var dir = process.scriptPath + "/tmp-errpath";
if (!stat(dir)) mkdir(dir);
/* a module for the modulePath route */
fprintf(dir + "/mod.js", "module.exports = function(req) { return {txt: 'mod ok'}; };\n");

function kill_server(p) {
    if (!p || !kill(p, 0)) return;
    kill(p, 15); sleep(0.5);
    if (kill(p, 0)) { kill(p, 9); sleep(0.2); }
}
function cleanup() { kill_server(pid); shell("rm -rf " + dir); }

/* notFound handler that throws on every request */
function nf(req) { throw new Error("notFound handler failed on purpose"); }

pid = server.start({
    bind: "127.0.0.1:" + port,
    daemon: true,
    log: false,
    threads: 2,
    developerMode: false,
    notFoundFunc: nf,
    map: {
        "/ok":    function(req) { return {txt: "ok"}; },
        "/boom":  function(req) { throw new Error("handler failed on purpose"); },
        "/mod/":  {modulePath: dir}
    }
});
testFeature.waitServer(base + "/ok");

function alive() {
    if (!kill(pid, 0)) return false;
    return curl.fetch(base + "/ok", {maxTime: 5}).status === 200;
}

/* one request, one response: a second response would show up as extra
   bytes on the same connection, which curl reports as a protocol error
   or as a second reply on a reused connection */
/* One request must produce exactly one response.  Reading the whole
   connection shows a second reply as an extra status line. */
function oneReply(name, path) {
    testFeature(name, function() {
        var reqf = dir + "/pipe.bin", outf = dir + "/pipe.out";
        fprintf(reqf, "GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", path);
        shell(sprintf("bash -c 'exec 3<>/dev/tcp/127.0.0.1/%d; cat %s >&3; "
                      + "timeout 5 cat <&3 > %s; exec 3<&-' 2>/dev/null",
                      port, reqf, outf));
        var out = stat(outf) ? readFile(outf, true) : "";
        var n = out.split("HTTP/1.").length - 1;
        if (!alive()) return false;
        if (n !== 1) printf("    got %d responses for one request\n", n);
        return n === 1;
    });
}

oneReply("modulePath with dot-dot in path",      base + "/mod/../x");
oneReply("modulePath with many segments",        base + "/mod/" + "a/".repeat(200) + "x");
oneReply("modulePath missing module",            base + "/mod/nosuchmodule");
testFeature("modulePath module still works", function() {
    return curl.fetch(base + "/mod/mod", {maxTime: 5}).text === "mod ok"; });

testFeature("throwing handler + throwing notFound does not recurse", function() {
    var r = curl.fetch(base + "/boom", {maxTime: 10});
    return r.status === 500 && alive();
});
testFeature("plain 404 with throwing notFound does not recurse", function() {
    var r = curl.fetch(base + "/nosuchpath", {maxTime: 10});
    return (r.status === 500 || r.status === 404) && alive();
});
testFeature("repeated failures keep the server up", function() {
    for (var i = 0; i < 25; i++) curl.fetch(base + "/boom", {maxTime: 10});
    return alive();
});

cleanup();
