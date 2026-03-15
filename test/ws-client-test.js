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
var nfailed = 0;

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
    rmFile(tmpdir + '/ws-client-test-alog');
    rmFile(tmpdir + '/ws-client-test-elog');
    rmFile(tmpdir + '/ws-client-test-ssl-alog');
    rmFile(tmpdir + '/ws-client-test-ssl-elog');
    rmFile(tmpdir + '/sample-cert.pem');
    rmFile(tmpdir + '/sample-key.pem');
    rmdir(tmpdir);
}

function testFeature(name, test) {
    var error = false;
    if (typeof test == 'function') {
        try {
            test = test();
        } catch(e) {
            error = e;
            test = false;
        }
    }
    printf("testing ws-client- %-49s - ", name);
    if (test)
        printf("passed\n");
    else {
        nfailed++;
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        do_cleanup();
        process.exit(1);
    }
    if (error) console.log(error);
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

/* *** Run all 8 tests sequentially for a given URL base *** */
function run_test_suite(url_base, rp, insecure, callback) {
    run_echo_test(url_base, rp + "_echo", insecure, function() {
        run_binary_test(url_base, rp + "_binary", insecure, function() {
            run_rapid_test(url_base, rp + "_rapid", insecure, function() {
                run_large_test(url_base, rp + "_large", insecure, function() {
                    run_concurrent_test(url_base, rp + "_concurrent", insecure, function() {
                        run_sequential_stress(url_base, rp + "_sequential", insecure, function() {
                            run_ping_test(url_base, rp + "_ping", insecure, function() {
                                run_timeout_test(url_base, rp + "_timeout", insecure, function() {
                                    callback();
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
}

/* *** Run ws:// suite, then wss:// suite, then report all *** */
run_test_suite("ws://127.0.0.1:8110", "ws", false, function() {
    run_test_suite("wss://127.0.0.1:8111", "wss", true, function() {
        report_suite("ws://  ", "ws");
        report_suite("wss:// ", "wss");

        clearTimeout(safety_timer);
        do_cleanup();
        if (nfailed > 0) {
            printf("\n%d test(s) FAILED.\n", nfailed);
            process.exit(1);
        }
        printf("\nAll WebSocket client tests passed.\n");
    });
});

/* safety timeout */
var safety_timer = setTimeout(function() {
    printf("\nTIMEOUT: tests did not complete in time\n");
    do_cleanup();
    process.exit(1);
}, 120000);
