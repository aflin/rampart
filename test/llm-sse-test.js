/* rampart-llm: streamed replies whose SSE lines are split across network
 * chunks.  A hosted API over TLS cuts the stream anywhere -- mid-line,
 * mid-JSON, inside a UTF-8 character -- and each line split that way used
 * to be dropped: lost words in the text, and tool calls that lost their id
 * or name.  A local server here sends every reply in 7-byte pieces. */
rampart.globalize(rampart.utils);

var net  = require("rampart-net");
var curl = require("rampart-curl");
var llm  = require("rampart-llm");

var testFeature = new (require('./test-feature.js'))({ prefix: "llm-sse" });

var PORT = 8297;

/* ---- what is streamed, and so what must come back ---- */
var TEXT = ["Héllo ", "“wörld” — ", "naïve ", "日本語 ", "done."];
var CALL_ID = "call_abc123", CALL_NAME = "keep_search";
var CALL_ARGS = { query: "Skilling résumé", limit: 20 };

function sse(o) { return "data: " + JSON.stringify(o) + "\n\n"; }

function openaiBody() {
    var s = "";
    TEXT.forEach(function (t) { s += sse({ choices: [{ delta: { content: t } }] }); });
    var args = JSON.stringify(CALL_ARGS);
    s += sse({ choices: [{ delta: { tool_calls: [{ index: 0, id: CALL_ID, type: "function",
            function: { name: CALL_NAME, arguments: "" } }] } }] });
    s += sse({ choices: [{ delta: { tool_calls: [{ index: 0,
            function: { arguments: args.substring(0, 11) } }] } }] });
    s += sse({ choices: [{ delta: { tool_calls: [{ index: 0,
            function: { arguments: args.substring(11) } }] } }] });
    /* the last event has no trailing newline: only a flush at stream end sees it */
    s += "data: " + JSON.stringify({ choices: [{ finish_reason: "tool_calls", delta: {} }] });
    return s;
}

function anthropicBody() {
    function ev(type, o) { o.type = type; return "event: " + type + "\n" + sse(o); }
    var s = ev("message_start", { message: { usage: { input_tokens: 5, output_tokens: 1 } } });
    s += ev("content_block_start", { index: 0, content_block: { type: "text", text: "" } });
    TEXT.forEach(function (t) {
        s += ev("content_block_delta", { index: 0, delta: { type: "text_delta", text: t } });
    });
    s += ev("content_block_stop", { index: 0 });
    s += ev("content_block_start", { index: 1, content_block:
            { type: "tool_use", id: CALL_ID, name: CALL_NAME, input: {} } });
    var args = JSON.stringify(CALL_ARGS);
    s += ev("content_block_delta", { index: 1, delta: { type: "input_json_delta",
            partial_json: args.substring(0, 11) } });
    s += ev("content_block_delta", { index: 1, delta: { type: "input_json_delta",
            partial_json: args.substring(11) } });
    s += ev("content_block_stop", { index: 1 });
    s += ev("message_delta", { delta: { stop_reason: "tool_use" }, usage: { output_tokens: 9 } });
    s += "event: message_stop\ndata: " + JSON.stringify({ type: "message_stop" });
    return s;
}

/* ---- a server that sends its reply a few bytes at a time ---- */
var PIECE = 7;
var server = new net.Server();
server.on("connection", function (sock) {
    var got = "";
    sock.on("data", function (d) {
        got += sprintf("%s", d);
        if (got.indexOf("\r\n\r\n") < 0) return;           /* headers not all here */
        var first = got.substring(0, got.indexOf("\r\n"));
        if (/^GET /.test(first)) {                           /* /props and the like */
            sock.write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            sock.destroy();
            return;
        }
        var body = stringToBuffer(/\/messages /.test(first) ? anthropicBody() : openaiBody());
        sock.write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n" +
                   "Connection: close\r\n\r\n");
        var at = 0;
        (function next() {
            if (at >= body.length) { sock.destroy(); return; }
            sock.write(body.slice(at, at + PIECE));
            at += PIECE;
            setTimeout(next, 2);
        })();
    });
});
server.listen({ port: PORT, host: "127.0.0.1" });

var BASE = "http://127.0.0.1:" + PORT + "/v1";
var results = {};

/* 1. the test server really does split lines, or the rest proves nothing */
function checkSplits(done) {
    var chunks = 0, cut = 0;
    curl.fetchAsync(BASE + "/chat/completions", {
        postJSON: { stream: true },
        chunkCallback: function (r) {
            var b = sprintf("%s", r.body);
            chunks++;
            if (b.length && b.charAt(b.length - 1) !== "\n") cut++;
        }
    }, function () { results.split = { chunks: chunks, cut: cut }; done(); });
}

/* 2. the OpenAI-compatible path */
function runOpenAI(done) {
    /* Building the client probes urlbase + "/props" for the context size,
       with a blocking fetch our own server cannot answer from this thread.
       Aim the probe at a closed port so it fails at once. */
    var c = llm.providerFromConfig({ type: "openai-compat", baseURL: BASE, model: "m",
                                     urlbase: "http://127.0.0.1:1" });
    var streamed = "";
    c.query([{ role: "user", content: "hi" }],
        function (t) { if (t && t.token && !t.thinking) streamed += t.token; },
        function (resp) { results.openai = { resp: resp, streamed: streamed }; done(); });
}

/* 3. the Anthropic path */
function runAnthropic(done) {
    var c = llm.providerFromConfig({ type: "anthropic", baseURL: BASE, model: "claude-x",
                                     apiKey: "test" });
    var streamed = "";
    c.query([{ role: "user", content: "hi" }],
        function (t) { if (t && t.token && !t.thinking) streamed += t.token; },
        function (resp) { results.anthropic = { resp: resp, streamed: streamed }; done(); });
}

function report() {
    var want = TEXT.join("");
    var wantArgs = JSON.stringify(CALL_ARGS);

    testFeature("the server's chunks end mid-line", function () {
        return results.split && results.split.cut >= 5;
    });

    ["openai", "anthropic"].forEach(function (k) {
        var r = results[k] || {}, resp = r.resp || {};
        var tc = (resp.toolCalls || [])[0] || { function: {} };
        testFeature(k + " - no error", function () { return !resp.error; });
        testFeature(k + " - every character of the text arrives",
                    function () { return resp.fullText === want; });
        testFeature(k + " - and was streamed to the token callback",
                    function () { return r.streamed === want; });
        testFeature(k + " - the tool call keeps its id",
                    function () { return tc.id === CALL_ID; });
        testFeature(k + " - and its name",
                    function () { return tc.function.name === CALL_NAME; });
        testFeature(k + " - and whole arguments", function () {
            try { return JSON.stringify(JSON.parse(tc.function.arguments)) === wantArgs; }
            catch (e) { return false; }
        });
    });
    testFeature("openai - a last line with no newline is still read",
                function () { return results.openai.resp.finishReason === "tool_calls"; });
    testFeature("anthropic - stop reason arrives",
                function () { return results.anthropic.resp.finishReason === "tool_use"; });
    testFeature.exit();
}

setTimeout(function () {
    checkSplits(function () {
        runOpenAI(function () {
            runAnthropic(report);
        });
    });
}, 100);
