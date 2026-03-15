/* make printf et. al. global */
rampart.globalize(rampart.utils);

var server = require("rampart-server");
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

function cleanup() {
    if (proxy_pid) kill_server(proxy_pid);
    if (upstream_pid) kill_server(upstream_pid);
    rmFile(tmpdir + '/proxy-test-upstream-alog');
    rmFile(tmpdir + '/proxy-test-upstream-elog');
    rmFile(tmpdir + '/proxy-test-proxy-alog');
    rmFile(tmpdir + '/proxy-test-proxy-elog');
}

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
    printf("testing proxy    - %-49s - ", name);
    if (test)
        printf("passed\n");
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
        cleanup();
        process.exit(1);
    }
    if (error) console.log(error);
}

/* *** Start upstream server on port 8060 *** */
upstream_pid = server.start({
    bind: "127.0.0.1:8060",
    daemon: true,
    log: true,
    user: 'nobody',
    accessLog: tmpdir + '/proxy-test-upstream-alog',
    errorLog:  tmpdir + '/proxy-test-upstream-elog',
    useThreads: true,

    map: {
        "/hello":       function(req) {
                            return { text: "hello from upstream" };
                        },
        "/echo":        function(req) {
                            return {
                                json: {
                                    method: req.method,
                                    path: req.path.path,
                                    query: req.query,
                                    body: req.body,
                                    headers: req.headers
                                }
                            };
                        },
        "/headers":     function(req) {
                            return {
                                json: {
                                    xff: req.headers["X-Forwarded-For"] || "",
                                    xfp: req.headers["X-Forwarded-Proto"] || "",
                                    xfh: req.headers["X-Forwarded-Host"] || "",
                                    custom: req.headers["X-Custom-Header"] || ""
                                }
                            };
                        },
        "/big":         function(req) {
                            var s = "";
                            for (var i = 0; i < 1000; i++)
                                s += "line " + i + " of big response data\n";
                            return { text: s };
                        },
        "/status":      function(req) {
                            var code = parseInt(req.query.code) || 200;
                            return { status: code, text: "status " + code };
                        },
        "/post":        function(req) {
                            return {
                                json: {
                                    method: req.method,
                                    body: req.body
                                }
                            };
                        }
    }
});

sleep(.5);
testFeature("upstream server is running", kill(upstream_pid, 0));

/* verify upstream directly */
testFeature("upstream direct request", function() {
    var res = curl.fetch("http://127.0.0.1:8060/hello");
    return res.status == 200 && res.text == "hello from upstream";
});

/* *** Start proxy server on port 8061 *** */
proxy_pid = server.start({
    bind: "127.0.0.1:8061",
    daemon: true,
    user: 'nobody',
    log: true,
    accessLog: tmpdir + '/proxy-test-proxy-alog',
    errorLog:  tmpdir + '/proxy-test-proxy-elog',
    useThreads: true,

    map: {
        /* proxy all /api/* to upstream */
        "/api/":        { proxy: "http://127.0.0.1:8060/" },

        /* proxy with custom headers */
        "/custom/":     {
                            proxy: "http://127.0.0.1:8060/",
                            headers: { "X-Custom-Header": "injected-value" }
                        },

        /* proxy with path rewrite: /backend/* -> upstream /api/* */
        /*"/backend/":    { proxy: "http://127.0.0.1:8060/api/" },*/

        /* a normal JS handler alongside proxy routes */
        "/local":       function(req) {
                            return { text: "local response" };
                        }
    }
});

sleep(0.5);
testFeature("proxy server is running", kill(proxy_pid, 0));

/* Test basic proxy GET */
testFeature("proxy GET request", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/hello");
    if (res.status == 200 && res.text == "hello from upstream")
        return true;
    printf("\nstatus=%d text='%s'\n", res.status, res.text);
    return false;
});

/* Test proxy preserves query string */
testFeature("proxy preserves query string", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/echo?foo=bar&baz=qux");
    if (res.status != 200) {
        printf("\nstatus=%d text='%s'\n", res.status, res.text);
        return false;
    }
    var j = JSON.parse(res.text);
    return j.query.foo == "bar" && j.query.baz == "qux";
});

/* Test proxy forwards X-Forwarded-For */
testFeature("proxy sets X-Forwarded-For", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/headers");
    if (res.status != 200) return false;
    var j = JSON.parse(res.text);
    return j.xff.length > 0;  /* should contain client IP */
});

/* Test proxy sets X-Forwarded-Proto */
testFeature("proxy sets X-Forwarded-Proto", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/headers");
    var j = JSON.parse(res.text);
    return j.xfp == "http";
});

/* Test proxy sets X-Forwarded-Host */
testFeature("proxy sets X-Forwarded-Host", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/headers");
    var j = JSON.parse(res.text);
    /* should contain the proxy's Host header */
    return j.xfh.length > 0;
});

/* Test proxy with custom headers */
testFeature("proxy injects custom headers", function() {
    var res = curl.fetch("http://127.0.0.1:8061/custom/headers");
    if (res.status != 200) {
        printf("\nstatus=%d text='%s'\n", res.status, res.text);
        return false;
    }
    var j = JSON.parse(res.text);
    return j.custom == "injected-value";
});

/* Test proxy POST with body */
testFeature("proxy POST with body", function() {
    var res = curl.fetch(
        { post: '{"key":"value"}', header: "Content-Type: application/json" },
        "http://127.0.0.1:8061/api/post"
    );
    if (res.status != 200) {
        printf("\nstatus=%d text='%s' errMsg='%s'\n", res.status, res.text, res.errMsg || "");
        return false;
    }
    var j = JSON.parse(res.text);
    return j.method == "POST";
});

/* Test proxy with large response */
testFeature("proxy large response", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/big");
    if (res.status != 200) {
        printf("\nstatus=%d\n", res.status);
        return false;
    }
    /* check that we got all 1000 lines */
    var lines = res.text.split("\n");
    /* last split element is empty string after trailing newline */
    return lines.length >= 1000;
});

/* Test proxy upstream error status codes */
testFeature("proxy upstream 404 status", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/status?code=404");
    return res.status == 404;
});

testFeature("proxy upstream 500 status", function() {
    var res = curl.fetch("http://127.0.0.1:8061/api/status?code=500");
    return res.status == 500;
});

/* Test that local JS routes still work alongside proxy */
testFeature("local route alongside proxy", function() {
    var res = curl.fetch("http://127.0.0.1:8061/local");
    return res.status == 200 && res.text == "local response";
});

/* Test proxy to non-existent upstream returns 502 */
testFeature("proxy to dead upstream returns 502", function() {
    /* stop the upstream server */
    kill_server(upstream_pid);
    upstream_pid = 0;
    sleep(0.3);

    var res = curl.fetch("http://127.0.0.1:8061/api/hello");
    return res.status == 502;
});

cleanup();
printf("\nAll proxy tests passed.\n");
