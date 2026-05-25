/* WebSocket client stress test
   Launches a rampart-server with WebSocket endpoints (plain and SSL),
   then uses net.wsConnect() to test echo, binary, rapid-fire messaging,
   large messages, concurrent connections, sequential stress, ping
   keepalive, and inactivity timeout over both ws:// and wss://. */

rampart.globalize(rampart.utils);

var server = require("rampart-server");
var net    = require("rampart-net");
var crypto = require("rampart-crypto");

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var server_pid = 0;
var ssl_server_pid = 0;
var testFeature = new (require('./test-feature.js'))({
    prefix: "ws-client",
    onFail: function() { do_cleanup(); process.exit(1); }
});

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
    if (server_pid) kill_server(server_pid);
    if (ssl_server_pid) kill_server(ssl_server_pid);
    try {
        rmFile(tmpdir + '/ws-client-test-alog');
        rmFile(tmpdir + '/ws-client-test-elog');
        rmFile(tmpdir + '/ws-client-test-ssl-alog');
        rmFile(tmpdir + '/ws-client-test-ssl-elog');
        rmFile(tmpdir + '/sample-cert.pem');
        rmFile(tmpdir + '/sample-key.pem');
        rmdir(tmpdir);
    } catch(e){}
}


/* *** WebSocket endpoint handlers (shared by both servers) *** */
var ws_map = {
    /* Echo endpoint: echoes back whatever the client sends */
    "ws:/wsecho": function(req) {
        if (!req.count) {
            req.wsOnDisconnect(function(){});
            return "welcome";
        }
        if (req.wsIsBin)
            return req.body;
        return sprintf("%s", req.body);
    },

    /* Counter endpoint: returns the message count */
    "ws:/wscounter": function(req) {
        if (!req.count) {
            req.wsOnDisconnect(function(){});
            return "ready";
        }
        return "msg:" + req.count;
    },

    /* Large message endpoint: echoes large messages back */
    "ws:/wslarge": function(req) {
        if (!req.count) {
            req.wsOnDisconnect(function(){});
            return "ready";
        }
        return sprintf("%s", req.body);
    },

    /* Prefix endpoint: sends back the message plus a prefix */
    "ws:/wsprefix": function(req) {
        if (!req.count) {
            req.wsOnDisconnect(function(){});
            return "ready";
        }
        return "PREFIX:" + sprintf("%s", req.body);
    }
};

/* *** Start plain server on port 8110 *** */
server_pid = server.start({
    bind: "127.0.0.1:8110",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/ws-client-test-alog',
    errorLog:  tmpdir + '/ws-client-test-elog',
    useThreads: true,
    map: ws_map
});

sleep(0.5);
testFeature("server is running", kill(server_pid, 0));

/* *** Generate self-signed certificate for SSL server *** */
var cert = tmpdir + '/sample-cert.pem';
var key  = tmpdir + '/sample-key.pem';

if (!(stat(cert) && stat(key))) {
    var r = crypto.gen_cert({
        country: "US",
        state: "Deleware",
        city: "Wilmington",
        organization: "Sample Co",
        organizationUnit: "Sample Department",
        email: "sample@sample.none",
        name: "sample.none",
        bits: 2048,
        days: 365,
        subjectAltName: ["localhost", "*.localhost"]
    });
    fprintf(key, '%s', r.key);
    fprintf(cert, '%s', r.cert);
}

/* *** Start SSL server on port 8111 *** */
ssl_server_pid = server.start({
    bind: "127.0.0.1:8111",
    daemon: true,
    log: true,
    secure: true,
    user: "nobody",
    sslKeyFile: key,
    sslCertFile: cert,
    accessLog: tmpdir + '/ws-client-test-ssl-alog',
    errorLog:  tmpdir + '/ws-client-test-ssl-elog',
    useThreads: true,
    map: ws_map
});

sleep(0.3);
testFeature("SSL server is running", kill(ssl_server_pid, 0));

/* *** Async test infrastructure *** */
var results = {};

/* *** Test 1: Basic echo with wsConnect *** */
function run_echo_test(url_base, rp, insecure, callback) {
    var got_connect = false;
    var got_welcome = false;
    var got_echo = false;
    var phase = "welcome";

    var ws = net.wsConnect({
        url: url_base + "/wsecho",
        timeout: 5000,
        pingInterval: 0,
        insecure: insecure,
        callbacks: {
            "wsConnect": function() {
                got_connect = true;
            },
            "message": function(ev) {
                var msg = sprintf("%s", ev.message);
                if (phase == "welcome") {
                    got_welcome = (msg == "welcome");
                    phase = "echo";
                    this.wsSend("hello world");
                } else if (phase == "echo") {
                    got_echo = (msg == "hello world");
                    this.wsClose();
                }
            },
            "close": function() {
                results[rp + "_connect"] = got_connect;
                results[rp + "_welcome"] = got_welcome;
                results[rp + "_echo"] = got_echo;
                callback();
            }
        }
    });

    ws.setTimeout(1000);
    ws.on("timeout", function() {
        results[rp + "_connect"] = got_connect;
        results[rp + "_welcome"] = got_welcome;
        results[rp + "_echo"] = got_echo;
        results[rp + "_timeout"] = true;
        ws.destroy();
        callback();
    });
}

/* *** Test 2: Binary data echo *** */
function run_binary_test(url_base, rp, insecure, callback) {
    var binary_ok = false;
    var phase = "welcome";

    /* Create a buffer with all byte values 0-255 */
    var test_buf = new Buffer(256);
    for (var i = 0; i < 256; i++)
        test_buf[i] = i;

    var ws = net.wsConnect({
        url: url_base + "/wsecho",
        timeout: 1000,
        pingInterval: 0,
        insecure: insecure,
        callbacks: {
            "message": function(ev) {
                if (phase == "welcome") {
                    phase = "binary";
                    this.wsSend(test_buf, true);
                } else if (phase == "binary") {
                    /* check that binary round-trips correctly */
                    if (ev.binary && ev.message.length == 256) {
                        var match = true;
                        var received = new Uint8Array(ev.message);
                        for (var j = 0; j < 256; j++) {
                            if (received[j] != j) {
                                match = false;
                                break;
                            }
                        }
                        binary_ok = match;
                    }
                    this.wsClose();
                }
            },
            "close": function() {
                results[rp + "_ok"] = binary_ok;
                callback();
            }
        }
    });

    ws.setTimeout(5000);
    ws.on("timeout", function() {
        results[rp + "_ok"] = false;
        results[rp + "_timeout"] = true;
        ws.destroy();
        callback();
    });
}

/* *** Test 3: Rapid-fire messages *** */
function run_rapid_test(url_base, rp, insecure, callback) {
    var target = 100;
    var received = 0;
    var all_correct = true;
    var phase = "welcome";

    var ws = net.wsConnect({
        url: url_base + "/wscounter",
        timeout: 10000,
        pingInterval: 0,
        insecure: insecure,
        callbacks: {
            "message": function(ev) {
                var msg = sprintf("%s", ev.message);
                if (phase == "welcome") {
                    phase = "sending";
                    /* blast out all messages at once */
                    for (var i = 0; i < target; i++)
                        this.wsSend("msg" + i);
                } else {
                    /* server replies with msg:N where N is req.count (1-based) */
                    var expected = "msg:" + (received + 1);
                    if (msg != expected)
                        all_correct = false;
                    received++;
                    if (received >= target)
                        this.wsClose();
                }
            },
            "close": function() {
                results[rp + "_count"] = received;
                results[rp + "_correct"] = all_correct;
                callback();
            }
        }
    });

    ws.setTimeout(10000);
    ws.on("timeout", function() {
        results[rp + "_count"] = received;
        results[rp + "_correct"] = false;
        results[rp + "_timeout"] = true;
        ws.destroy();
        callback();
    });
}

/* *** Test 4: Large messages (>125 bytes, triggers 16-bit length encoding) *** */
function run_large_test(url_base, rp, insecure, callback) {
    var sizes = [200, 1000, 10000, 65535];
    var size_idx = 0;
    var all_ok = true;
    var phase = "welcome";

    var ws = net.wsConnect({
        url: url_base + "/wslarge",
        timeout: 10000,
        pingInterval: 0,
        insecure: insecure,
        callbacks: {
            "message": function(ev) {
                var msg = sprintf("%s", ev.message);
                if (phase == "welcome") {
                    phase = "testing";
                    /* send first large message */
                    var payload = "";
                    for (var i = 0; i < sizes[size_idx]; i++)
                        payload += String.fromCharCode(65 + (i % 26));
                    this.wsSend(payload);
                } else {
                    /* verify response size matches */
                    if (msg.length != sizes[size_idx]) {
                        all_ok = false;
                    } else {
                        /* verify content */
                        for (var i = 0; i < msg.length; i++) {
                            if (msg.charCodeAt(i) != 65 + (i % 26)) {
                                all_ok = false;
                                break;
                            }
                        }
                    }
                    size_idx++;
                    if (size_idx >= sizes.length) {
                        this.wsClose();
                    } else {
                        var payload = "";
                        for (var i = 0; i < sizes[size_idx]; i++)
                            payload += String.fromCharCode(65 + (i % 26));
                        this.wsSend(payload);
                    }
                }
            },
            "close": function() {
                results[rp + "_ok"] = all_ok;
                results[rp + "_sizes"] = size_idx;
                callback();
            }
        }
    });

    ws.setTimeout(10000);
    ws.on("timeout", function() {
        results[rp + "_ok"] = false;
        results[rp + "_timeout"] = true;
        ws.destroy();
        callback();
    });
}

/* *** Test 5: Concurrent connections *** */
function run_concurrent_test(url_base, rp, insecure, callback) {
    var num_connections = 10;
    var messages_per_conn = 20;
    var completed = 0;
    var all_ok = true;

    for (var c = 0; c < num_connections; c++) {
        (function(conn_id) {
            var recv_count = 0;
            var phase = "welcome";

            var ws = net.wsConnect({
                url: url_base + "/wsprefix",
                timeout: 10000,
                pingInterval: 0,
                insecure: insecure,
                callbacks: {
                    "message": function(ev) {
                        var msg = sprintf("%s", ev.message);
                        if (phase == "welcome") {
                            phase = "running";
                            for (var m = 0; m < messages_per_conn; m++)
                                this.wsSend("c" + conn_id + "_m" + m);
                        } else {
                            var expected = "PREFIX:c" + conn_id + "_m" + recv_count;
                            if (msg != expected)
                                all_ok = false;
                            recv_count++;
                            if (recv_count >= messages_per_conn)
                                this.wsClose();
                        }
                    },
                    "close": function() {
                        completed++;
                        if (completed >= num_connections) {
                            results[rp + "_ok"] = all_ok;
                            results[rp + "_count"] = completed;
                            callback();
                        }
                    }
                }
            });

            ws.setTimeout(10000);
            ws.on("timeout", function() {
                all_ok = false;
                completed++;
                ws.destroy();
                if (completed >= num_connections) {
                    results[rp + "_ok"] = false;
                    results[rp + "_timeout"] = true;
                    results[rp + "_count"] = completed;
                    callback();
                }
            });
        })(c);
    }
}

/* *** Test 6: Sequential connect/disconnect stress *** */
function run_sequential_stress(url_base, rp, insecure, callback) {
    var iterations = 20;
    var current = 0;
    var all_ok = true;

    function do_one() {
        if (current >= iterations) {
            results[rp + "_ok"] = all_ok;
            results[rp + "_count"] = current;
            callback();
            return;
        }

        var got_msg = false;
        var ws = net.wsConnect({
            url: url_base + "/wsecho",
            timeout: 5000,
            pingInterval: 0,
            insecure: insecure,
            callbacks: {
                "message": function(ev) {
                    var msg = sprintf("%s", ev.message);
                    if (!got_msg) {
                        got_msg = true;
                        this.wsSend("iter" + current);
                    } else {
                        if (msg != "iter" + current)
                            all_ok = false;
                        this.wsClose();
                    }
                },
                "close": function() {
                    current++;
                    do_one();
                }
            }
        });

        ws.setTimeout(5000);
        ws.on("timeout", function() {
            all_ok = false;
            current++;
            ws.destroy();
            do_one();
        });
    }

    do_one();
}

/* *** Test 7: Ping/pong keepalive *** */
function run_ping_test(url_base, rp, insecure, callback) {
    var got_connect = false;
    var stayed_alive = false;

    var ws = net.wsConnect({
        url: url_base + "/wsecho",
        timeout: 10000,
        pingInterval: 1,  /* ping every 1 second */
        insecure: insecure,
        callbacks: {
            "wsConnect": function() {
                got_connect = true;
                /* wait 3 seconds then send a message to confirm we're still connected */
                var sock = this;
                setTimeout(function() {
                    sock.wsSend("still alive");
                }, 3000);
            },
            "message": function(ev) {
                var msg = sprintf("%s", ev.message);
                if (msg == "still alive") {
                    stayed_alive = true;
                    this.wsClose();
                }
                /* ignore the welcome message */
            },
            "close": function() {
                results[rp + "_connect"] = got_connect;
                results[rp + "_alive"] = stayed_alive;
                callback();
            }
        }
    });

    ws.setTimeout(10000);
    ws.on("timeout", function() {
        results[rp + "_connect"] = got_connect;
        results[rp + "_alive"] = false;
        results[rp + "_timeout"] = true;
        ws.destroy();
        callback();
    });
}

/* *** Test 8: Inactivity timeout fires *** */
function run_timeout_test(url_base, rp, insecure, callback) {
    var got_connect = false;
    var got_timeout = false;
    var t_start = 0;
    var elapsed = 0;

    var ws = net.wsConnect({
        url: url_base + "/wsecho",
        timeout: 1000,      /* 1 second inactivity timeout */
        pingInterval: 0,     /* no pings, so the connection will go idle */
        insecure: insecure,
        callbacks: {
            "wsConnect": function() {
                got_connect = true;
                t_start = new Date().getTime();
                /* do nothing — let the connection sit idle */
            },
            "message": function(ev) {
                /* ignore the welcome message */
            },
            "error": function(err) {
                /* ignore errors from timeout-driven close */
            },
            "close": function() {
                results[rp + "_connect"] = got_connect;
                results[rp + "_fired"] = got_timeout;
                results[rp + "_elapsed"] = elapsed;
                callback();
            }
        }
    });

    ws.on("timeout", function() {
        got_timeout = true;
        elapsed = new Date().getTime() - t_start;
        ws.destroy();
    });
}

printf("Websocket tests running...\r");

/* *** Test 9: WHATWG WebSocket class (rampart-whatwg.so) round-trip
   Exercises the WHATWG-shape WebSocket against the same echo
   endpoint as the net.wsConnect tests.  Validates that the class
   wrapper correctly translates rampart-net's callback shape to
   open/message/close/error events, that binaryType='arraybuffer'
   yields ArrayBuffer payloads, default binaryType='blob' yields
   Blob payloads (with async .text()/.arrayBuffer() reads), and that
   close() drives the wasClean=true close path. */
function run_whatwg_test(url_base, rp, insecure, callback) {
    /* wss + insecure server certs: WHATWG WebSocket has no `insecure`
       option (spec doesn't expose cert-verification toggles).  Skip
       the wss leg for now — net.wsConnect's `insecure` flag is the
       rampart-net-only escape hatch. */
    if (insecure) {
        results[rp + "_skipped"] = true;
        callback();
        return;
    }

    /* --- 1. text echo with onevent handlers --- */
    var got_open = false, got_welcome = false, got_echo = false;
    var phase = "welcome";
    var ws = new WebSocket(url_base + "/wsecho");
    ws.binaryType = 'arraybuffer';   /* exercised in step 2 */

    ws.onopen = function() { got_open = true; };
    ws.onmessage = function(ev) {
        /* ev must be a MessageEvent */
        if (!(ev instanceof MessageEvent)) {
            results[rp + "_event_class_wrong"] = true;
        }
        if (phase === "welcome") {
            got_welcome = (typeof ev.data === 'string' && ev.data === 'welcome');
            phase = "echo";
            ws.send("hello world");
        } else if (phase === "echo") {
            got_echo = (typeof ev.data === 'string' && ev.data === 'hello world');
            phase = "binary";
            /* --- 2. binary echo with binaryType=arraybuffer --- */
            var u8 = new Uint8Array([0, 1, 127, 200, 255]);
            ws.send(u8.buffer);
        } else if (phase === "binary") {
            results[rp + "_binary_arraybuffer_isAB"] = (ev.data instanceof ArrayBuffer);
            if (ev.data instanceof ArrayBuffer) {
                var bb = new Uint8Array(ev.data);
                results[rp + "_binary_arraybuffer_bytes_ok"] =
                    (bb.length === 5 && bb[0] === 0 && bb[1] === 1
                     && bb[2] === 127 && bb[3] === 200 && bb[4] === 255);
            }
            phase = "done";
            ws.close();
        }
    };
    ws.onerror = function(ev) {
        results[rp + "_error"] = (ev && ev.message) || "(no message)";
    };
    ws.onclose = function(ev) {
        results[rp + "_open"]        = got_open;
        results[rp + "_welcome"]     = got_welcome;
        results[rp + "_echo"]        = got_echo;
        results[rp + "_close_class"] = (ev instanceof CloseEvent);
        results[rp + "_wasClean"]    = !!(ev && ev.wasClean);
        results[rp + "_readyState"]  = ws.readyState;  /* expect 3 (CLOSED) */
        /* --- 3. blob round-trip with default binaryType --- */
        run_whatwg_blob_subtest(url_base, rp, insecure, callback);
    };

    /* Safety: bail if connection never establishes */
    setTimeout(function() {
        if (!got_echo && ws.readyState !== WebSocket.CLOSED) {
            results[rp + "_timeout"] = true;
            try { ws.close(); } catch (_) {}
        }
    }, 5000);
}

function run_whatwg_blob_subtest(url_base, rp, insecure, callback) {
    var ws = new WebSocket(url_base + "/wsecho");
    /* default binaryType is 'blob' per spec */
    if (ws.binaryType !== 'blob') {
        results[rp + "_blob_default_wrong"] = ws.binaryType;
    }
    var phase = "welcome";
    ws.onmessage = function(ev) {
        if (phase === "welcome") {
            phase = "binary";
            ws.send(new Uint8Array([42, 99, 200]).buffer);
        } else if (phase === "binary") {
            if (!(ev.data instanceof Blob)) {
                results[rp + "_blob_is_blob"] = false;
                ws.close();
                return;
            }
            results[rp + "_blob_is_blob"] = true;
            results[rp + "_blob_size"]    = ev.data.size;
            /* Async read the Blob bytes and verify */
            ev.data.arrayBuffer().then(function(ab) {
                var b = new Uint8Array(ab);
                results[rp + "_blob_bytes_ok"] =
                    (b.length === 3 && b[0] === 42 && b[1] === 99 && b[2] === 200);
                ws.close();
            }, function() {
                results[rp + "_blob_bytes_ok"] = false;
                ws.close();
            });
        }
    };
    ws.onclose = function() { callback(); };
    setTimeout(function() {
        if (ws.readyState !== WebSocket.CLOSED) {
            results[rp + "_blob_timeout"] = true;
            try { ws.close(); } catch(_) {}
        }
    }, 5000);
}

/* *** Run all 9 tests sequentially for a given URL base *** */
function run_test_suite(url_base, rp, insecure, callback) {
    run_echo_test(url_base, rp + "_echo", insecure, function() {
        run_binary_test(url_base, rp + "_binary", insecure, function() {
            run_rapid_test(url_base, rp + "_rapid", insecure, function() {
                run_large_test(url_base, rp + "_large", insecure, function() {
                    run_concurrent_test(url_base, rp + "_concurrent", insecure, function() {
                        run_sequential_stress(url_base, rp + "_sequential", insecure, function() {
                            run_ping_test(url_base, rp + "_ping", insecure, function() {
                                run_timeout_test(url_base, rp + "_timeout", insecure, function() {
                                    run_whatwg_test(url_base, rp + "_whatwg", insecure, function() {
                                        callback();
                                    });
                                });
                            });
                        });
                    });
                });
            });
        });
    });
}

/* *** Report results for a suite *** */
function report_suite(label, rp) {
    testFeature(label + " basic echo", function() {
        if (results[rp + "_echo_timeout"]) { printf("\ntimed out\n"); return false; }
        return results[rp + "_echo_connect"] && results[rp + "_echo_welcome"] && results[rp + "_echo_echo"];
    });

    testFeature(label + " binary data round-trip", function() {
        if (results[rp + "_binary_timeout"]) { printf("\ntimed out\n"); return false; }
        return results[rp + "_binary_ok"];
    });

    testFeature(label + " rapid-fire 100 messages", function() {
        if (results[rp + "_rapid_timeout"]) {
            printf("\ntimed out after %d messages\n", results[rp + "_rapid_count"]);
            return false;
        }
        if (results[rp + "_rapid_count"] != 100) {
            printf("\nexpected 100, got %d\n", results[rp + "_rapid_count"]);
            return false;
        }
        return results[rp + "_rapid_correct"];
    });

    testFeature(label + " large messages (200-65535 bytes)", function() {
        if (results[rp + "_large_timeout"]) { printf("\ntimed out\n"); return false; }
        return results[rp + "_large_ok"] && results[rp + "_large_sizes"] == 4;
    });

    testFeature(label + " 10 concurrent x 20 msgs", function() {
        if (results[rp + "_concurrent_timeout"]) { printf("\ntimed out\n"); return false; }
        return results[rp + "_concurrent_ok"] && results[rp + "_concurrent_count"] == 10;
    });

    testFeature(label + " 20 sequential connect/disconnect", function() {
        return results[rp + "_sequential_ok"] && results[rp + "_sequential_count"] == 20;
    });

    testFeature(label + " ping keepalive over 3 seconds", function() {
        if (results[rp + "_ping_timeout"]) { printf("\ntimed out\n"); return false; }
        return results[rp + "_ping_connect"] && results[rp + "_ping_alive"];
    });

    testFeature(label + " 1 second inactivity timeout", function() {
        if (!results[rp + "_timeout_connect"]) { printf("\nnever connected\n"); return false; }
        if (!results[rp + "_timeout_fired"]) { printf("\ntimeout event not fired\n"); return false; }
        /* elapsed should be roughly 1000ms; accept 800-3000ms */
        if (results[rp + "_timeout_elapsed"] < 800 || results[rp + "_timeout_elapsed"] > 3000) {
            printf("\nunexpected elapsed time: %d ms\n", results[rp + "_timeout_elapsed"]);
            return false;
        }
        return true;
    });

    /* wss leg silently omits the WHATWG WS test: WHATWG WebSocket
       has no `insecure` flag for self-signed certs (cert-verification
       toggle isn't part of the spec), so we can't connect to our
       self-signed test server.  ws:// covers the wrapper logic. */
    if (results[rp + "_whatwg_skipped"]) return;
    testFeature(label + " WHATWG WS round-trip", function() {
        if (results[rp + "_whatwg_timeout"])
            { printf("\nWHATWG timed out before text echo completed\n"); return false; }
        if (results[rp + "_whatwg_blob_timeout"])
            { printf("\nWHATWG blob subtest timed out\n"); return false; }
        if (results[rp + "_whatwg_error"])
            { printf("\nWHATWG error: %s\n", results[rp + "_whatwg_error"]); return false; }
        if (results[rp + "_whatwg_event_class_wrong"])
            { printf("\nmessage event is not a MessageEvent\n"); return false; }
        if (!results[rp + "_whatwg_open"])
            { printf("\nonopen never fired\n"); return false; }
        if (!results[rp + "_whatwg_welcome"])
            { printf("\nwelcome text not received\n"); return false; }
        if (!results[rp + "_whatwg_echo"])
            { printf("\ntext echo failed\n"); return false; }
        if (!results[rp + "_whatwg_binary_arraybuffer_isAB"])
            { printf("\nbinary message (arraybuffer mode) was not an ArrayBuffer\n"); return false; }
        if (!results[rp + "_whatwg_binary_arraybuffer_bytes_ok"])
            { printf("\nbinary ArrayBuffer bytes wrong\n"); return false; }
        if (!results[rp + "_whatwg_close_class"])
            { printf("\nclose event is not a CloseEvent\n"); return false; }
        if (!results[rp + "_whatwg_wasClean"])
            { printf("\nclose was not wasClean=true\n"); return false; }
        if (results[rp + "_whatwg_readyState"] !== 3)
            { printf("\nclose readyState was %s, expected 3 (CLOSED)\n",
                     results[rp + "_whatwg_readyState"]); return false; }
        if (results[rp + "_whatwg_blob_default_wrong"])
            { printf("\ndefault binaryType was '%s', expected 'blob'\n",
                     results[rp + "_whatwg_blob_default_wrong"]); return false; }
        if (results[rp + "_whatwg_blob_is_blob"] !== true)
            { printf("\nbinary message (blob mode) was not a Blob\n"); return false; }
        if (results[rp + "_whatwg_blob_size"] !== 3)
            { printf("\nBlob.size was %s, expected 3\n", results[rp + "_whatwg_blob_size"]); return false; }
        if (!results[rp + "_whatwg_blob_bytes_ok"])
            { printf("\nBlob.arrayBuffer() bytes wrong\n"); return false; }
        return true;
    });
}

/* *** Run ws:// suite, then wss:// suite, then report all *** */
/* --- Optional wss:// test against a public echo server with a real
   cert.  Skipped silently (no test line emitted at all) when the
   public host isn't reachable — offline runs, restricted DNS,
   firewalled CI, service-down, etc.  Connectivity probed by a plain
   TCP connect with a 1s timeout before the real WebSocket attempt.

   ws.postman-echo.com is operated by Postman; the /raw path is a
   bidirectional echo (whatever the client sends, the server sends
   back as-is).  No welcome message. */
var PUBLIC_HOST = 'ws.postman-echo.com';
var PUBLIC_PATH = '/raw';
var PUBLIC_PORT = 443;

function probe_public(callback) {
    var done = false;
    var sock = new net.Socket();
    function finish(ok) {
        if (done) return; done = true;
        try { sock.destroy(); } catch(_) {}
        callback(ok);
    }
    sock.on("connect", function() { finish(true);  });
    sock.on("error",   function() { finish(false); });
    sock.on("timeout", function() { finish(false); });
    try {
        sock.connect({host: PUBLIC_HOST, port: PUBLIC_PORT, timeout: 1000});
    } catch (_) { finish(false); }
    /* belt-and-braces fallback */
    setTimeout(function() { finish(false); }, 1200);
}

function run_whatwg_public_test(callback) {
    var got_open    = false;
    var got_echo    = false;
    var url = 'wss://' + PUBLIC_HOST + PUBLIC_PATH;
    var test_msg = 'rampart whatwg websocket test ' + Date.now();
    var safety = setTimeout(function() {
        results.public_whatwg_timeout = true;
        try { ws.close(); } catch(_) {}
    }, 5000);
    var ws = new WebSocket(url);
    ws.onopen = function() {
        got_open = true;
        ws.send(test_msg);
    };
    ws.onmessage = function(ev) {
        /* Match on equality with our outgoing message.  Postman's
           /raw echo has no welcome; the first message we receive
           SHOULD be our own send echoed back. */
        if (typeof ev.data === 'string' && ev.data === test_msg) {
            got_echo = true;
            ws.close();
        }
    };
    ws.onerror = function(ev) {
        results.public_whatwg_error = (ev && ev.message) || 'unknown';
    };
    ws.onclose = function(ev) {
        clearTimeout(safety);
        results.public_whatwg_open      = got_open;
        results.public_whatwg_echo      = got_echo;
        results.public_whatwg_wasClean  = !!(ev && ev.wasClean);
        callback();
    };
}

run_test_suite("ws://127.0.0.1:8110", "ws", false, function() {
    run_test_suite("wss://127.0.0.1:8111", "wss", true, function() {
        report_suite("ws://  ", "ws");
        report_suite("wss:// ", "wss");

        probe_public(function(reachable) {
            function finish() {
                clearTimeout(safety_timer);
                do_cleanup();
                testFeature.exit();
            }
            if (!reachable) { finish(); return; }
            run_whatwg_public_test(function() {
                testFeature("public  WHATWG WS round-trip (" + PUBLIC_HOST + ")", function() {
                    if (results.public_whatwg_timeout) { printf("\ntimed out\n"); return false; }
                    if (results.public_whatwg_error)
                        { printf("\nerror: %s\n", results.public_whatwg_error); return false; }
                    if (!results.public_whatwg_open) { printf("\nonopen never fired\n"); return false; }
                    if (!results.public_whatwg_echo) { printf("\necho mismatch\n"); return false; }
                    if (!results.public_whatwg_wasClean) { printf("\nclose was not wasClean\n"); return false; }
                    return true;
                });
                finish();
            });
        });
    });
});

/* safety timeout */
var safety_timer = setTimeout(function() {
    printf("\nTIMEOUT: tests did not complete in time\n");
    do_cleanup();
    process.exit(1);
}, 120000);
