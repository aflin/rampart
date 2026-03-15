/* WebSocket proxy test using raw TCP for the WebSocket handshake */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
var net = require("rampart-net");
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

function do_cleanup() {
    if (proxy_pid) kill_server(proxy_pid);
    if (upstream_pid) kill_server(upstream_pid);
    rmFile(tmpdir + '/ws-proxy-test-upstream-alog');
    rmFile(tmpdir + '/ws-proxy-test-upstream-elog');
    rmFile(tmpdir + '/ws-proxy-test-proxy-alog');
    rmFile(tmpdir + '/ws-proxy-test-proxy-elog');
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
    printf("testing ws-proxy - %-49s - ", name);
    if (test)
        printf("passed\n");
    else
    {
        nfailed++;
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        do_cleanup();
        process.exit(1);
    }
    if (error) console.log(error);
}

/* *** Start upstream server on port 8100 with a WebSocket echo endpoint *** */
upstream_pid = server.start({
    bind: "127.0.0.1:8100",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/ws-proxy-test-upstream-alog',
    errorLog:  tmpdir + '/ws-proxy-test-upstream-elog',
    useThreads: true,

    map: {
        "/hello":       function(req) {
                            return { text: "hello from upstream" };
                        },

        /* WebSocket echo endpoint: echoes back whatever the client sends */
        "ws:/wsecho":   function(req) {
                            if (!req.count) {
                                /* first call on connect - send welcome */
                                req.wsOnDisconnect(function(){});
                                return "connected";
                            }
                            /* echo back the message */
                            return sprintf("%s", req.body);
                        }
    }
});

sleep(0.5);
testFeature("upstream server is running", kill(upstream_pid, 0));

/* *** Start proxy server on port 8101 *** */
proxy_pid = server.start({
    bind: "127.0.0.1:8101",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/ws-proxy-test-proxy-alog',
    errorLog:  tmpdir + '/ws-proxy-test-proxy-elog',
    useThreads: true,

    map: {
        "/": { proxy: "http://127.0.0.1:8100/" }
    }
});

sleep(0.5);
testFeature("proxy server is running", kill(proxy_pid, 0));

/* Test HTTP still works through proxy */
testFeature("HTTP through proxy works", function() {
    var res = curl.fetch("http://127.0.0.1:8101/hello");
    return res.status == 200 && res.text == "hello from upstream";
});

/* *** Binary-safe byte accumulator and WebSocket frame helpers *** */

/* Concatenate two Uint8Arrays */
function concat_bytes(a, b) {
    var result = new Uint8Array(a.length + b.length);
    result.set(a, 0);
    result.set(b, a.length);
    return result;
}

/* Find \r\n\r\n in a Uint8Array, return index or -1 */
function find_header_end(bytes) {
    for (var i = 0; i <= bytes.length - 4; i++) {
        if (bytes[i] == 0x0D && bytes[i+1] == 0x0A &&
            bytes[i+2] == 0x0D && bytes[i+3] == 0x0A)
            return i;
    }
    return -1;
}

/* Check if bytes contain the ASCII string "101" */
function has_101(bytes) {
    for (var i = 0; i <= bytes.length - 3; i++) {
        if (bytes[i] == 0x31 && bytes[i+1] == 0x30 && bytes[i+2] == 0x31)
            return true;
    }
    return false;
}

/* Extract ASCII text from Uint8Array */
function bytes_to_string(bytes, start, len) {
    var s = "";
    var end = (len !== undefined) ? start + len : bytes.length;
    for (var i = start; i < end; i++)
        s += String.fromCharCode(bytes[i]);
    return s;
}

/* Return total frame length from bytes starting at offset, or 0 if incomplete */
function ws_frame_len_b(bytes, offset) {
    if (!offset) offset = 0;
    if (bytes.length - offset < 2) return 0;
    var plen = bytes[offset + 1] & 0x7F;
    var hdr = 2;
    if (plen == 126) {
        if (bytes.length - offset < 4) return 0;
        plen = (bytes[offset + 2] << 8) | bytes[offset + 3];
        hdr = 4;
    }
    if (bytes.length - offset < hdr + plen) return 0;
    return hdr + plen;
}

/* Extract text payload from an unmasked server frame at offset */
function ws_decode_b(bytes, offset) {
    if (!offset) offset = 0;
    if (bytes.length - offset < 2) return null;
    var plen = bytes[offset + 1] & 0x7F;
    var hdr = 2;
    if (plen == 126) {
        if (bytes.length - offset < 4) return null;
        plen = (bytes[offset + 2] << 8) | bytes[offset + 3];
        hdr = 4;
    }
    if (bytes.length - offset < hdr + plen) return null;
    return bytes_to_string(bytes, offset + hdr, plen);
}

/* Build a masked WebSocket text frame as a binary string for socket.write() */
function ws_frame(text) {
    var plen = text.length;
    var header = [];

    /* FIN + opcode text */
    header.push(0x81);

    /* mask bit + length */
    if (plen < 126)
        header.push(0x80 | plen);
    else {
        header.push(0x80 | 126);
        header.push((plen >> 8) & 0xFF);
        header.push(plen & 0xFF);
    }

    /* mask key (all zeros for simplicity) */
    header.push(0, 0, 0, 0);

    var frame = new Uint8Array(header.length + plen);
    for (var i = 0; i < header.length; i++)
        frame[i] = header[i];
    for (var i = 0; i < plen; i++)
        frame[header.length + i] = text.charCodeAt(i);

    return bufferToString(frame.buffer);
}

/* *** Event-driven WebSocket test *** */

/* results object, filled by event callbacks */
var ws_results = {
    direct_connected: false,
    direct_welcome: "",
    direct_echo: "",
    direct_error: "",
    proxy_connected: false,
    proxy_welcome: "",
    proxy_echo: "",
    proxy_error: "",
    multi_ok: true,
    multi_count: 0,
    multi_target: 3
};

function run_ws_test(host, port, path, send_msg, prefix, callback)
{
    var phase = "handshake";
    var accum = new Uint8Array(0);
    var socket = new net.Socket();

    socket.on("connect", function() {
        var key = "dGVzdGtleXRlc3RrZXk9";
        var req = "GET " + path + " HTTP/1.1\r\n" +
                  "Host: " + host + ":" + port + "\r\n" +
                  "Upgrade: websocket\r\n" +
                  "Connection: Upgrade\r\n" +
                  "Sec-WebSocket-Key: " + key + "\r\n" +
                  "Sec-WebSocket-Version: 13\r\n" +
                  "\r\n";
        this.write(req);
    });

    socket.on("data", function(d) {
        accum = concat_bytes(accum, new Uint8Array(d));

        if (phase == "handshake") {
            var idx = find_header_end(accum);
            if (idx < 0) return;

            if (!has_101(accum)) {
                ws_results[prefix + "_error"] = "no 101: " + bytes_to_string(accum, 0, 80);
                socket.destroy();
                return;
            }
            ws_results[prefix + "_connected"] = true;
            accum = accum.subarray(idx + 4);
            phase = "welcome";
        }

        if (phase == "welcome") {
            var flen = ws_frame_len_b(accum);
            if (!flen) return;
            ws_results[prefix + "_welcome"] = ws_decode_b(accum);
            accum = accum.subarray(flen);
            phase = "echo";
            /* send test message */
            socket.write(ws_frame(send_msg));
        }

        if (phase == "echo") {
            var flen = ws_frame_len_b(accum);
            if (!flen) return;
            ws_results[prefix + "_echo"] = ws_decode_b(accum);
            phase = "done";
            socket.destroy();
        }
    });

    socket.on("error", function() {
        ws_results[prefix + "_error"] = "socket error";
    });

    socket.on("timeout", function() {
        ws_results[prefix + "_error"] = "timeout";
        socket.destroy();
    });

    socket.on("close", function() {
        if (callback) callback();
    });

    socket.setTimeout(5000);
    socket.connect(port, host);
}

/* Run multi-connection test: sequential WebSocket connections through proxy */
function run_multi_test(iteration) {
    if (iteration >= ws_results.multi_target) {
        finish_tests();
        return;
    }
    var msg = "multi_" + iteration;
    var prefix = "multi_" + iteration;
    ws_results[prefix + "_connected"] = false;
    ws_results[prefix + "_welcome"] = "";
    ws_results[prefix + "_echo"] = "";
    ws_results[prefix + "_error"] = "";

    var phase = "handshake";
    var accum = new Uint8Array(0);
    var socket = new net.Socket();

    socket.on("connect", function() {
        var key = "dGVzdGtleXRlc3RrZXk9";
        var req = "GET /wsecho HTTP/1.1\r\n" +
                  "Host: 127.0.0.1:8101\r\n" +
                  "Upgrade: websocket\r\n" +
                  "Connection: Upgrade\r\n" +
                  "Sec-WebSocket-Key: " + key + "\r\n" +
                  "Sec-WebSocket-Version: 13\r\n" +
                  "\r\n";
        this.write(req);
    });

    socket.on("data", function(d) {
        accum = concat_bytes(accum, new Uint8Array(d));

        if (phase == "handshake") {
            var idx = find_header_end(accum);
            if (idx < 0) return;
            if (!has_101(accum)) {
                ws_results.multi_ok = false;
                socket.destroy();
                return;
            }
            accum = accum.subarray(idx + 4);
            phase = "welcome";
        }

        if (phase == "welcome") {
            var flen = ws_frame_len_b(accum);
            if (!flen) return;
            accum = accum.subarray(flen);
            phase = "echo";
            socket.write(ws_frame(msg));
        }

        if (phase == "echo") {
            var flen = ws_frame_len_b(accum);
            if (!flen) return;
            var decoded = ws_decode_b(accum);
            if (decoded != msg)
                ws_results.multi_ok = false;
            ws_results.multi_count++;
            phase = "done";
            socket.destroy();
        }
    });

    socket.on("error", function() {
        ws_results.multi_ok = false;
    });

    socket.on("timeout", function() {
        ws_results.multi_ok = false;
        socket.destroy();
    });

    socket.on("close", function() {
        run_multi_test(iteration + 1);
    });

    socket.setTimeout(5000);
    socket.connect(8101, "127.0.0.1");
}

function finish_tests() {
    testFeature("WebSocket direct to upstream", function() {
        if (!ws_results.direct_connected) {
            printf("\nerror: %s\n", ws_results.direct_error);
            return false;
        }
        if (ws_results.direct_welcome != "connected") {
            printf("\nwelcome: '%s'\n", ws_results.direct_welcome);
            return false;
        }
        if (ws_results.direct_echo != "hello ws") {
            printf("\necho: '%s'\n", ws_results.direct_echo);
            return false;
        }
        return true;
    });

    testFeature("WebSocket through proxy", function() {
        if (!ws_results.proxy_connected) {
            printf("\nerror: %s\n", ws_results.proxy_error);
            return false;
        }
        if (ws_results.proxy_welcome != "connected") {
            printf("\nwelcome: '%s'\n", ws_results.proxy_welcome);
            return false;
        }
        if (ws_results.proxy_echo != "proxied msg") {
            printf("\necho: '%s'\n", ws_results.proxy_echo);
            return false;
        }
        return true;
    });

    testFeature("multiple WebSocket connections through proxy", function() {
        if (!ws_results.multi_ok) return false;
        return ws_results.multi_count == ws_results.multi_target;
    });

    clearTimeout(safety_timer);
    do_cleanup();
    if (nfailed > 0)
    {
        printf("\n%d test(s) FAILED.\n", nfailed);
        process.exit(1);
    }
    printf("\nAll WebSocket proxy tests passed.\n");
}

/* kick off the async tests: direct -> proxy -> multi -> finish */
run_ws_test("127.0.0.1", 8100, "/wsecho", "hello ws", "direct", function() {
    run_ws_test("127.0.0.1", 8101, "/wsecho", "proxied msg", "proxy", function() {
        run_multi_test(0);
    });
});

/* safety timeout in case something hangs */
var safety_timer = setTimeout(function() {
    printf("\nTIMEOUT: tests did not complete in time\n");
    do_cleanup();
    process.exit(1);
}, 15000);
