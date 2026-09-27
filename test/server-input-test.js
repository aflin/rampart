/* rampart-server must survive malformed or unusual request bodies.
   Each case sends one request, then checks the server is still alive
   and answering. */
rampart.globalize(rampart.utils);
var server = require("rampart-server");
var curl   = require("rampart-curl");

var testFeature = new (require('./test-feature.js'))({
    prefix: "server-input",
    onFail: function() { if (!process.env.SERVER_TEST_KEEPGOING) { cleanup(); process.exit(1); } }
});

var port = 8741;
var base = "http://127.0.0.1:" + port;
var pid  = 0;

function echo(req) {
    var o = {params: req.params, bodyLen: req.body ? req.body.length : -1};
    if (req.postData && Array.isArray(req.postData.content))
        o.parts = req.postData.content.length;
    return {json: sprintf("%J", o)};
}

function kill_server(p) {
    if (!p || !kill(p, 0)) return;
    kill(p, 15); sleep(0.5);
    if (kill(p, 0)) { kill(p, 9); sleep(0.2); }
}
function cleanup() { kill_server(pid); }

function start() {
    pid = server.start({
        bind: "127.0.0.1:" + port,
        daemon: true,
        log: false,
        threads: 2,
        map: {"/app/": echo}
    });
    testFeature.waitServer(base + "/app/ok");
}
start();

function alive() {
    if (!kill(pid, 0)) return false;
    var r = curl.fetch(base + "/app/ok", {maxTime: 5});
    return r.status === 200;
}

/* send a body with the given content-type; pass if the server survives.
   A server that died is restarted so each case is judged on its own. */
function survives(name, ctype, body) {
    testFeature(name, function() {
        curl.fetch(base + "/app/x", {post: body, headers: ["Content-Type: " + ctype], maxTime: 5});
        var ok = alive();
        if (!ok) { kill_server(pid); start(); }
        return ok;
    });
}

var MP = "multipart/form-data; boundary=XyZ";
function part(head, content) { return "--XyZ\r\n" + head + "\r\n\r\n" + content + "\r\n"; }
var END = "--XyZ--\r\n";

/* JSON bodies whose parsed content is not a plain object */
survives("json array body",            "application/json", "[1,2,3]");
survives("json array of objects",      "application/json", '[{"name":"x"}]');
survives("json number body",           "application/json", "5");
survives("json null body",             "application/json", "null");
survives("json string body",           "application/json", '"str"');
survives("invalid json body",          "application/json", "[1,");

/* multipart field names that end in ']' */
["]", "a]", "ab]", "[]", "a[b]", "a[]", "[b]"].forEach(function(n) {
    survives("multipart name '" + n + "'", MP,
        part('Content-Disposition: form-data; name="' + n + '"', "v") + END);
});

/* malformed multipart structure */
survives("multipart part with no name",       MP, part('Content-Disposition: form-data', "v") + END);
survives("multipart header line without colon", MP, part('garbage line', "v") + END);
survives("multipart part without blank line", MP, "--XyZ\r\nContent-Disposition: form-data; name=\"a\"\r\nv\r\n" + END);
survives("multipart unterminated quote",      MP, part('Content-Disposition: form-data; name="a', "v") + END);
survives("multipart attribute without value", MP, part('Content-Disposition: form-data; name=', "v") + END);
survives("multipart empty boundary",          "multipart/form-data; boundary=", part('Content-Disposition: form-data; name="a"', "v") + END);
survives("multipart body ends at boundary",   MP, "--XyZ");
survives("multipart empty body",              MP, "");
survives("multipart only end marker",         MP, END);

/* query strings and urlencoded bodies */
function survivesGet(name, url) {
    testFeature(name, function() {
        curl.fetch(url, {maxTime: 5});
        var ok = alive();
        if (!ok) { kill_server(pid); start(); }
        return ok;
    });
}
survivesGet("empty query string",        base + "/app/x?");
survivesGet("empty query with fragment", base + "/app/x?#f");
survivesGet("query of only '&'",         base + "/app/x?&");
survivesGet("query with empty element",  base + "/app/x?a=1&&b=2");
survivesGet("query key only",            base + "/app/x?a");
survives("empty urlencoded body", "application/x-www-form-urlencoded", "");

/* keys that name inherited object properties must not reach shared objects */
[ "__proto__%5BCookie%5D=%7B%7D",
  "__proto__%5BContent-Type%5D=%7B%7D",
  "__proto__=x",
  "constructor%5B%5D=x",
  "toString=a&toString=b",
  "valueOf%5Bx%5D=1"
].forEach(function(q) {
    survivesGet("query " + decodeURIComponent(q), base + "/app/x?" + q);
    /* a second plain request on the same thread must still work */
    survivesGet("  ... and a plain request after it", base + "/app/ok");
});

/* well-formed multipart still parses */
testFeature("multipart fields still parsed", function() {
    var body = part('Content-Disposition: form-data; name="a"', "one")
             + part('Content-Disposition: form-data; name="b[]"', "two")
             + part('Content-Disposition: form-data; name="b[]"', "three")
             + part('Content-Disposition: form-data; name="f"; filename="x.txt"\r\nContent-Type: text/plain', "file")
             + END;
    var r = curl.fetch(base + "/app/x", {post: body, headers: ["Content-Type: " + MP]});
    var o = JSON.parse(r.text);
    return o.parts === 4 && o.params.a === "one"
        && o.params.b.length === 2 && o.params.b[1] === "three";
});
testFeature("json object params still copied", function() {
    var r = curl.fetch(base + "/app/x", {postJSON: {k: "v"}});
    return JSON.parse(r.text).params.k === "v";
});

/* query parsing still works as documented */
testFeature("query params still parsed", function() {
    var o = JSON.parse(curl.fetch(base + "/app/x?a=1&b=two&c%20d=e+f").text);
    return o.params.a === "1" && o.params.b === "two" && o.params["c d"] === "e f";
});
testFeature("query array and object syntax still parsed", function() {
    var o = JSON.parse(curl.fetch(base + "/app/x?a[]=1&a[]=2&b[k]=v").text);
    return o.params.a.length === 2 && o.params.a[1] === "2" && o.params.b.k === "v";
});
testFeature("repeated query key becomes array", function() {
    var o = JSON.parse(curl.fetch(base + "/app/x?a=1&a=2").text);
    return Array.isArray(o.params.a) && o.params.a[0] === "1" && o.params.a[1] === "2";
});
testFeature("urlencoded body still parsed", function() {
    var o = JSON.parse(curl.fetch(base + "/app/x", {post: {k: "v", n: "2"}}).text);
    return o.params.k === "v" && o.params.n === "2";
});

/* ---- static file serving ---- */
kill_server(pid);
pid = 0;

var www = process.scriptPath + "/tmp-serverinput";
if (!stat(www)) mkdir(www);
if (!stat(www + "/sub")) mkdir(www + "/sub");
fprintf(www + "/index.html", "0123456789");
fprintf(www + "/sub/index.html", "sub");
fprintf(www + "/empty.txt", "");

var fport = 8742, fbase = "http://127.0.0.1:" + fport;
var fpid = server.start({
    bind: "127.0.0.1:" + fport,
    daemon: true,
    log: false,
    threads: 2,
    map: {"/static": www}          /* key longer than some resolved paths */
});
testFeature.waitServer(fbase + "/static/index.html");

function falive() {
    if (!kill(fpid, 0)) return false;
    return curl.fetch(fbase + "/static/index.html", {maxTime: 5}).status === 200;
}
function fget(url, hdrs) {
    var o = {maxTime: 5};
    if (hdrs) o.headers = hdrs;
    return curl.fetch(url, o);
}
function fstart() {
    fpid = server.start({
        bind: "127.0.0.1:" + fport, daemon: true, log: false, threads: 2,
        map: {"/static": www}
    });
    testFeature.waitServer(fbase + "/static/index.html");
}
function fsurvives(name, url, hdrs, check) {
    testFeature(name, function() {
        var r = fget(url, hdrs);
        var ok = falive();
        if (!ok) { kill_server(fpid); fstart(); return false; }
        return check ? check(r) : true;
    });
}

/* Range headers: a malformed one is ignored and the whole file sent */
fsurvives("Range bytes=x",   fbase + "/static/index.html", ["Range: bytes=x"],
    function(r) { return r.status === 200 && r.body.length === 10; });
fsurvives("Range bytes=",    fbase + "/static/index.html", ["Range: bytes="],
    function(r) { return r.status === 200 && r.body.length === 10; });
fsurvives("Range bytes=-",   fbase + "/static/index.html", ["Range: bytes=-"],
    function(r) { return r.status === 200 && r.body.length === 10; });
fsurvives("Range bytes=5 (no dash)", fbase + "/static/index.html", ["Range: bytes=5"],
    function(r) { return r.status === 200 && r.body.length === 10; });
fsurvives("Range on empty file", fbase + "/static/empty.txt", ["Range: bytes=0-5"],
    function(r) { return r.status === 416 || r.status === 200; });
testFeature("valid Range still works", function() {
    var r = fget(fbase + "/static/index.html", ["Range: bytes=2-5"]);
    return r.status === 206 && bufferToString(r.body) === "2345";
});
testFeature("open-ended Range still works", function() {
    var r = fget(fbase + "/static/index.html", ["Range: bytes=7-"]);
    return r.status === 206 && bufferToString(r.body) === "789";
});

/* paths that resolve shorter than, or outside, the map key */
fsurvives("path resolving to '/'", fbase + "/static/..", null,
    function(r) { return r.status === 404 || r.status === 400; });
fsurvives("path resolving above the key", fbase + "/static/../x/index.html", null,
    function(r) { return r.status === 404 || r.status === 400; });
fsurvives("path resolving out of the map", fbase + "/static/../../etc/passwd", null,
    function(r) { return r.status === 404 || r.status === 400; });
fsurvives("encoded dot-dot", fbase + "/static/%2e%2e/index.html", null,
    function(r) { return r.status === 404 || r.status === 400; });

/* normal serving still works */
testFeature("static file served", function() {
    return bufferToString(fget(fbase + "/static/index.html").body) === "0123456789"; });
testFeature("percent-encoded filename served", function() {
    return fget(fbase + "/static/%69ndex.html").status === 200; });
testFeature("directory redirect keeps the map key", function() {
    var r = fget(fbase + "/static/sub");
    if (r.status !== 301 && r.status !== 302) return false;
    return /\/static\/sub\/$/.test(r.headers.Location || r.headers.location || "");
});
testFeature("directory index served", function() {
    return bufferToString(fget(fbase + "/static/sub/").body) === "sub"; });
testFeature("unmatched key prefix 404s", function() {
    return fget(fbase + "/staticjunk").status === 404; });

kill_server(fpid);
shell("rm -rf " + www);
cleanup();
