/* rampart-server must survive clients that vanish or misbehave mid-request:
   pipelined/trailing bytes on proxied requests, disconnects during chunked
   and deferred replies, and websocket clients that drop.
   Each case ends by checking the server still answers. */
rampart.globalize(rampart.utils);
var server = require("rampart-server");
var curl   = require("rampart-curl");
var net    = require("rampart-net");

var testFeature = new (require('./test-feature.js'))({
    prefix: "server-abort",
    onFail: function() { if (!process.env.SERVER_TEST_KEEPGOING) { cleanup(); process.exit(1); } }
});

var uport = 8745, sport = 8746;            /* upstream, and the proxy/front */
var ubase = "http://127.0.0.1:" + uport, sbase = "http://127.0.0.1:" + sport;
var upid = 0, spid = 0;

function kill_server(p) {
    if (!p || !kill(p, 0)) return;
    kill(p, 15); sleep(0.5);
    if (kill(p, 0)) { kill(p, 9); sleep(0.2); }
}
function cleanup() { kill_server(spid); kill_server(upid); }

/* ---- upstream: answers slowly so the client can leave first ---- */
function uslow(req)  { sleep(1.5); return {txt: "upstream slow"}; }
function ufast(req)  { return {txt: "upstream fast"}; }
upid = server.start({
    bind: "127.0.0.1:" + uport, daemon: true, log: false, threads: 4,
    map: {"/slow": uslow, "/fast": ufast}
});
testFeature.waitServer(ubase + "/fast");

/* ---- front server: proxy, chunked, deferred, websocket ---- */
function chunkcb(req) {
    if (req.chunkIndex >= 20) req.chunkEnd("end\n");
    else req.chunkSend("chunk " + req.chunkIndex + "\n");
}
function chunked(req)  { return {txt: chunkcb, chunk: true, chunkDelay: 40}; }
function deferred(req) { setTimeout(function(){ req.reply({txt:"deferred"}); }, 600); return {defer:true}; }
function neverreply(req) { return {defer: true}; }    /* app never replies */
function ws(req)       { return req.count ? req.body : undefined; }
function ok(req)       { return {txt: "ok"}; }

function sstart() {
    spid = server.start({
        bind: "127.0.0.1:" + sport, daemon: true, log: false, threads: 4,
        map: {
            "/ok":      ok,
            "/chunk":   chunked,
            "/defer":   deferred,
            "/never":   neverreply,
            "ws:/ws":   ws,
            "/proxy/":  {proxy: ubase + "/"}
        }
    });
    testFeature.waitServer(sbase + "/ok");
}
sstart();

function alive() {
    if (!kill(spid, 0)) return false;
    return curl.fetch(sbase + "/ok", {maxTime: 5}).status === 200;
}

/* Send raw bytes, hold the socket open briefly, then close it.
   bash's /dev/tcp keeps this synchronous, which a net.connect callback
   could not be (the event loop is not running inside this script). */
var tmpd = process.scriptPath + "/tmp-serverabort";
if (!stat(tmpd)) mkdir(tmpd);
var reqfile = tmpd + "/req.bin";

function rawsend(bytes, waitMs) {
    fprintf(reqfile, "%s", bytes);
    shell(sprintf(
        "bash -c 'exec 3<>/dev/tcp/127.0.0.1/%d; cat %s >&3; sleep %.3f; exec 3<&-' 2>/dev/null",
        sport, reqfile, waitMs / 1000));
}

function survives(name, fn) {
    testFeature(name, function() {
        fn();
        sleep(0.3);
        var ok = alive();
        if (!ok) { kill_server(spid); sstart(); }
        return ok;
    });
}

var REQ = "GET /proxy/slow HTTP/1.1\r\nHost: x\r\n\r\n";

survives("proxied request with trailing junk, client closes", function() {
    rawsend(REQ + "X", 300);
});
survives("proxied request pipelined twice, client closes", function() {
    rawsend(REQ + REQ, 300);
});
survives("proxied request then immediate close", function() {
    rawsend(REQ, 50);
});
survives("proxied websocket upgrade with trailing junk", function() {
    rawsend("GET /proxy/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
          + "Connection: Upgrade\r\nSec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n"
          + "Sec-WebSocket-Version: 13\r\n\r\nX", 300);
});
survives("chunked reply, client closes early", function() {
    rawsend("GET /chunk HTTP/1.1\r\nHost: x\r\n\r\n", 120);
});
survives("chunked reply, client closes before first chunk", function() {
    rawsend("GET /chunk HTTP/1.1\r\nHost: x\r\n\r\n", 5);
});
survives("deferred reply, client closes early", function() {
    rawsend("GET /defer HTTP/1.1\r\nHost: x\r\n\r\n", 100);
});
survives("deferred reply never sent, client closes", function() {
    rawsend("GET /never HTTP/1.1\r\nHost: x\r\n\r\n", 100);
});
survives("websocket client drops without close frame", function() {
    rawsend("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
          + "Connection: Upgrade\r\nSec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n"
          + "Sec-WebSocket-Version: 13\r\n\r\n", 150);
});
survives("many aborted proxied requests", function() {
    for (var i = 0; i < 10; i++) rawsend(REQ + "X", 40);
});

/* normal operation still works */
testFeature("proxy still works", function() {
    return curl.fetch(sbase + "/proxy/fast", {maxTime: 10}).text === "upstream fast"; });
testFeature("chunked reply still complete", function() {
    var r = curl.fetch(sbase + "/chunk", {maxTime: 15});
    return /chunk 0/.test(r.text) && /end/.test(r.text); });
testFeature("deferred reply still works", function() {
    return curl.fetch(sbase + "/defer", {maxTime: 10}).text === "deferred"; });

cleanup();
