"noTranspile";
/* rampart-chromeview - Puppeteer-style Chrome control for Rampart
 *
 * Drives Chrome/Chromium via the Chrome DevTools Protocol (CDP) over
 * a rampart-net WebSocket client.  A dedicated worker thread owns the
 * socket + its own libevent loop so blocking-sync calls from the main
 * thread (which freeze the main libevent loop) can still receive CDP
 * responses and unblock via rampart.thread.waitfor/del.
 *
 * Public API: launch(opts), connect(opts).  Browser, BrowserContext,
 * Page, and Frame objects are returned from those.
 *
 * Each CDP-backed method has three calling conventions:
 *   - no callback, no transpiler  -> blocks, returns value (sync)
 *   - no callback, Promise in scope (transpiler active) -> returns Promise
 *   - callback provided           -> fires cb(value, err) asynchronously
 */

var net    = require("rampart-net");
var curl   = require("rampart-curl");
var utils  = rampart.utils;
var printf = utils.printf;
var sprintf= utils.sprintf;

/* Snapshot the `_TrN_Sp.load` function that was current when this
 * module was first required.  Under "use transpilerGlobally", any
 * file rampart transpiles installs its own per-file preamble:
 *      `_TrN_Sp.load = function () { ... }; _TrN_Sp.load();`
 * — and the LAST file processed wins, leaving `_TrN_Sp.load`
 * pointing at that file's installer (which may install only a tiny
 * subset of helpers, e.g. `_req` alone).  Worker-init in
 * rampart-thread.c does
 *      `var load = _TrN_Sp.load; _TrN_Sp = {}; load();`
 * — so whatever load is current at spawn time decides which helpers
 * the worker comes up with.  The transpiler runs our preamble before
 * this file's body, so by THIS line `_TrN_Sp.load` is our preamble's
 * installer (installs `_fs`, `_req`, `_origToString`, `_gp`,
 * `_origRegExp`).  Stash it so `_spinUpWorker()` can restore it
 * right before spawning the worker, regardless of what other
 * requires the caller has issued in the meantime. */
var _rchSavedTrNSpLoad = (typeof _TrN_Sp !== "undefined") ? _TrN_Sp.load : null;

/* ------------------------------------------------------------------ *
 *  Worker-thread body.  Runs in its own JS interpreter + event loop.
 *  Globals here are whatever was copied at thread-creation time; we
 *  do our own requires explicitly.
 * ------------------------------------------------------------------ */
function _workerMain(args)
{
    /* If the parent ran with the rampart transpiler globally on, our
     * own `require("rampart-net")` below gets rewritten to
     * `_TrN_Sp._req(module, "rampart-net")` at compile time.  Workers
     * inherit _TrN_Sp from the parent, but the `load()` they re-run is
     * whichever file the transpiler last processed in the parent — and
     * that file's `load()` may not install `_req` if it had no
     * require() calls of its own.  Install a fallback that delegates
     * to the native `require` so transpiled require() works in the
     * worker regardless of which file's load() ran last. */
    if (typeof _TrN_Sp !== "undefined" && typeof _TrN_Sp._req !== "function") {
        var _natReq = require;
        _TrN_Sp._req = function(m, s) { return _natReq(s); };
    }
    var wnet    = require("rampart-net");
    var thread  = rampart.thread;
    var event   = rampart.event;
    var sprintf = rampart.utils.sprintf;   /* don't rely on caller having globalized */
    var ch      = args.channel;     /* unique per-browser namespace prefix */
    var wsUrl   = args.wsUrl;

    /* id -> {mode, evMethod, evSession, done, replyResult, replyError} */
    var pending    = {};
    var idSeq      = 0;
    var outbox     = [];
    var wsReady    = false;
    var ws;

    /* Per-session execution-context tracking so frames can evaluate in
     * the right context.  frameCtxs[sessionId][frameId] = contextId. */
    var frameCtxs  = {};

    /* Tracing buffers, keyed by sessionId.  Each is an array of trace
     * events accumulated while tracing is active.  Lives in the worker
     * so sync-mode callers (whose main loop is blocked) still get
     * complete data via the worker's direct done() callback. */
    var traceBufs = {};

    function sendFrame(obj) {
        var s = JSON.stringify(obj);
        if (wsReady) ws.wsSend(s);
        else outbox.push(s);
    }

    /* Core CDP call (internal to worker).  onDone is (result, error).
     * If evCompleteMethod is set, onDone fires only after that event arrives
     * (still carries the original CDP reply result). */
    function cdpCall(method, params, sessionId, evCompleteMethod, evCompleteSession, onDone) {
        var id = ++idSeq;
        pending[id] = {
            mode: evCompleteMethod ? "event" : "reply",
            evMethod: evCompleteMethod,
            evSession: evCompleteSession || "",
            done: onDone
        };
        var frame = {id: id, method: method, params: params || {}};
        if (sessionId) frame.sessionId = sessionId;
        sendFrame(frame);
    }

    /* Handler table for multi-step procedures invoked from the main thread. */
    var procedures = {
        raw: function(a, done) {
            cdpCall(a.method, a.params, a.sessionId,
                    a.evMethod, a.evSession, done);
        },
        newPage: function(a, done) {
            var params = {url: a.url || "about:blank"};
            if (a.browserContextId) params.browserContextId = a.browserContextId;
            cdpCall("Target.createTarget", params, null, null, null, function(r1, e1) {
                if (e1) return done(null, e1);
                var targetId = r1.targetId;
                cdpCall("Target.attachToTarget",
                    {targetId: targetId, flatten: true}, null, null, null,
                    function(r2, e2) {
                        if (e2) return done(null, e2);
                        var sid = r2.sessionId;
                        var remaining = 3, err = null, mainFrameId = null;
                        function maybeDone() {
                            if (remaining > 0) return;
                            if (err) return done(null, err);
                            /* Fetch main frame id so setContent can use
                             * Page.setDocumentContent (atomic, avoids the
                             * frame-manager race that document.write has). */
                            cdpCall("Page.getFrameTree", {}, sid, null, null,
                                function(r3, e3) {
                                    if (e3) return done(null, e3);
                                    mainFrameId = r3.frameTree.frame.id;
                                    done({targetId: targetId, sessionId: sid,
                                          mainFrameId: mainFrameId}, null);
                                });
                        }
                        function step(_, e) {
                            if (e) err = e;
                            --remaining;
                            maybeDone();
                        }
                        cdpCall("Page.enable",    {}, sid, null, null, step);
                        cdpCall("Runtime.enable", {}, sid, null, null, step);
                        cdpCall("Network.enable", {}, sid, null, null, step);
                    });
            });
        },
        createBrowserContext: function(a, done) {
            cdpCall("Target.createBrowserContext", {}, null, null, null, done);
        },
        disposeBrowserContext: function(a, done) {
            cdpCall("Target.disposeBrowserContext",
                {browserContextId: a.browserContextId}, null, null, null, done);
        },
        /* Resolve frameId to the current contextId, send Runtime.evaluate. */
        frameEval: function(a, done) {
            var map = frameCtxs[a.sessionId];
            var ctxId = map && map[a.frameId];
            if (!ctxId) return done(null, {message: "frame has no execution context (frameId=" + a.frameId + ")"});
            var params = {
                expression:    a.expression,
                returnByValue: a.returnByValue !== false,
                awaitPromise:  a.awaitPromise !== false,
                userGesture:   true,
                contextId:     ctxId
            };
            cdpCall("Runtime.evaluate", params, a.sessionId, null, null, done);
        },
        /* Return frame tree + the current contextId map so main can
         * enumerate/build Frame objects. */
        getFrames: function(a, done) {
            cdpCall("Page.getFrameTree", {}, a.sessionId, null, null,
                function(r, err) {
                    if (err) return done(null, err);
                    done({
                        tree:       r.frameTree,
                        contextIds: frameCtxs[a.sessionId] || {}
                    }, null);
                });
        },
        /* Wait for the next occurrence of a named CDP event on a session.
         * Completes with null result when the event fires, or error on
         * timeout (enforced by main's thread.del timeout). */
        waitForEvent: function(a, done) {
            var id = ++idSeq;
            pending[id] = {
                mode:       "event",
                evMethod:   a.evMethod,
                evSession:  a.evSession || "",
                done:       done,
                replyResult: null,
                replyError:  null
            };
        },
        /* Wait for a Network.responseReceived whose URL matches urlMatch
         * (a substring, or a string starting with "/" and ending with "/"
         * treated as a regex).  Returns {requestId, response}. */
        waitForResponse: function(a, done) {
            var id = ++idSeq;
            pending[id] = {
                mode:      "responseWait",
                evSession: a.sessionId,
                urlMatch:  a.urlMatch,
                done:      done
            };
        },
        /* Browser-scope Target.getTargets — used by Browser.pages(). */
        listTargets: function(a, done) {
            cdpCall("Target.getTargets", {}, null, null, null, done);
        },
        /* Start tracing: init the buffer for this session and call
         * Tracing.start with the supplied config. */
        traceStart: function(a, done) {
            traceBufs[a.sessionId] = [];
            cdpCall("Tracing.start", a.params || {}, a.sessionId,
                null, null, done);
        },
        /* End tracing.  Wait for Tracing.tracingComplete on this
         * session, then return the accumulated buffer. */
        traceStop: function(a, done) {
            var id = ++idSeq;
            pending[id] = {
                mode:      "traceWait",
                evSession: a.sessionId,
                done:      done
            };
            cdpCall("Tracing.end", {}, a.sessionId, null, null,
                function(_, err) {
                    if (err) {
                        delete pending[id];
                        done(null, err);
                    }
                });
        },
        /* Press the left mouse button on `from`, drag toward `to` in
         * `steps` increments, and resolve with the DragData chrome emits
         * via Input.dragIntercepted.  Caller must have enabled
         * Input.setInterceptDrags first (typically via
         * page.setDragInterception(true)).  The mouse stays pressed at
         * `to` so the caller can dispatch dragEnter/dragOver/drop. */
        mouseDrag: function(a, done) {
            var sid   = a.sessionId;
            var from  = a.from, to = a.to;
            var steps = a.steps || 5;
            cdpCall("Input.dispatchMouseEvent", {
                type: "mouseMoved", x: from.x, y: from.y,
                button: "none", buttons: 0
            }, sid, null, null, function() {
                cdpCall("Input.dispatchMouseEvent", {
                    type: "mousePressed", x: from.x, y: from.y,
                    button: "left", buttons: 1, clickCount: 1
                }, sid, null, null, function() {
                    /* Register the dragWait pending *before* triggering
                     * the move that will fire Input.dragIntercepted. */
                    var id = ++idSeq;
                    pending[id] = {
                        mode:      "dragWait",
                        evSession: sid,
                        done:      done
                    };
                    /* Smooth-move to target. */
                    var i = 0;
                    function moveStep() {
                        if (i > steps) return;
                        var t = (++i) / steps;
                        var x = from.x + (to.x - from.x) * t;
                        var y = from.y + (to.y - from.y) * t;
                        cdpCall("Input.dispatchMouseEvent", {
                            type: "mouseMoved", x: x, y: y,
                            button: "left", buttons: 1
                        }, sid, null, null, function() {
                            if (i < steps) moveStep();
                        });
                    }
                    moveStep();
                });
            });
        },
        /* Attach to an existing targetId (one we didn't create ourselves)
         * and enable the same domains as newPage. */
        attachExisting: function(a, done) {
            cdpCall("Target.attachToTarget",
                {targetId: a.targetId, flatten: true}, null, null, null,
                function(r2, e2) {
                    if (e2) return done(null, e2);
                    var sid = r2.sessionId;
                    var remaining = 3, err = null;
                    function maybeDone() {
                        if (remaining > 0) return;
                        if (err) return done(null, err);
                        cdpCall("Page.getFrameTree", {}, sid, null, null,
                            function(r3, e3) {
                                if (e3) return done(null, e3);
                                done({targetId: a.targetId, sessionId: sid,
                                      mainFrameId: r3.frameTree.frame.id}, null);
                            });
                    }
                    function step(_, e) { if (e) err = e; --remaining; maybeDone(); }
                    cdpCall("Page.enable",    {}, sid, null, null, step);
                    cdpCall("Runtime.enable", {}, sid, null, null, step);
                    cdpCall("Network.enable", {}, sid, null, null, step);
                });
        }
    };

    /* Compile urlMatch to a predicate function. */
    function _matchUrl(urlMatch, url) {
        if (!url) return false;
        if (typeof urlMatch !== "string") return false;
        if (urlMatch.length >= 2
            && urlMatch.charAt(0) === "/"
            && urlMatch.charAt(urlMatch.length-1) === "/") {
            try { return new RegExp(urlMatch.slice(1, -1)).test(url); }
            catch(e) { return false; }
        }
        return url.indexOf(urlMatch) >= 0;
    }

    function onMessage(ev) {
        var msg;
        try { msg = JSON.parse(sprintf("%s", ev.message)); }
        catch (e) { return; }

        if (msg.id !== undefined) {
            var p = pending[msg.id];
            if (!p) return;

            if (p.mode === "event") {
                p.replyResult = msg.result;
                p.replyError  = msg.error;
                if (msg.error) {
                    if (p.done) p.done(null, msg.error);
                    delete pending[msg.id];
                }
                return;
            }

            if (p.done) p.done(msg.result, msg.error);
            delete pending[msg.id];
            return;
        }

        if (msg.method) {
            var sess = msg.sessionId || "";

            /* Tracing accumulation: data events stream while active. */
            if (msg.method === "Tracing.dataCollected" && traceBufs[sess]) {
                var v = msg.params && msg.params.value;
                if (Array.isArray(v)) {
                    for (var ti = 0; ti < v.length; ti++)
                        traceBufs[sess].push(v[ti]);
                }
            } else if (msg.method === "Tracing.tracingComplete") {
                var buf = traceBufs[sess] || [];
                delete traceBufs[sess];
                /* Resolve any pending traceWait for this session. */
                for (var pid in pending) {
                    var pt = pending[pid];
                    if (pt.mode === "traceWait" && pt.evSession === sess) {
                        if (pt.done) pt.done({events: buf}, null);
                        delete pending[pid];
                    }
                }
            } else if (msg.method === "Input.dragIntercepted") {
                /* Resolve any pending dragWait for this session. */
                var dd = msg.params && msg.params.data;
                for (var pid2 in pending) {
                    var pd = pending[pid2];
                    if (pd.mode === "dragWait" && pd.evSession === sess) {
                        if (pd.done) pd.done(dd, null);
                        delete pending[pid2];
                    }
                }
            }

            /* Track execution contexts for frame.evaluate routing. */
            if (msg.method === "Runtime.executionContextCreated") {
                var c = msg.params && msg.params.context;
                if (c && c.auxData && c.auxData.frameId) {
                    if (!frameCtxs[sess]) frameCtxs[sess] = {};
                    frameCtxs[sess][c.auxData.frameId] = c.id;
                }
            } else if (msg.method === "Runtime.executionContextDestroyed") {
                var cid = msg.params && msg.params.executionContextId;
                var m = frameCtxs[sess];
                if (m && cid) {
                    for (var fid in m) if (m[fid] === cid) delete m[fid];
                }
            } else if (msg.method === "Runtime.executionContextsCleared") {
                delete frameCtxs[sess];
            }

            /* complete any pending waiter on this event */
            for (var pid in pending) {
                var p2 = pending[pid];
                if (p2.mode === "event"
                    && p2.evMethod === msg.method
                    && p2.evSession === sess)
                {
                    if (p2.done) p2.done(p2.replyResult, p2.replyError);
                    delete pending[pid];
                }
                else if (p2.mode === "responseWait"
                    && msg.method === "Network.responseReceived"
                    && p2.evSession === sess)
                {
                    var rurl = msg.params && msg.params.response && msg.params.response.url;
                    if (_matchUrl(p2.urlMatch, rurl)) {
                        if (p2.done) p2.done({
                            requestId: msg.params.requestId,
                            response:  msg.params.response
                        }, null);
                        delete pending[pid];
                    }
                }
            }

            /* forward to subscribers in the main thread */
            event.trigger(ch + ".ev." + sess + "." + msg.method, {
                sessionId: sess,
                method:    msg.method,
                params:    msg.params
            });
        }
    }

    ws = wnet.wsConnect({
        url: wsUrl,
        /* Send a ping every 10s; rampart-net closes the socket after
         * 3 unanswered pings (~30s), so an ungracefully-exited chrome
         * surfaces as a `disconnected` event within ~30s. */
        pingInterval: 10,
        callbacks: {
            wsConnect: function() {
                wsReady = true;
                while (outbox.length) ws.wsSend(outbox.shift());
                thread.put(ch + ".ready", true);
            },
            message: onMessage,
            error: function(err) {
                thread.put(ch + ".fatal", String(err));
            },
            close: function() {
                /* Fire the browser-level 'disconnected' event before the
                 * clipboard signal so main-thread subscribers always see
                 * a single delivery whether the close was graceful
                 * (Browser.close) or driven by the ping-timeout path. */
                try { event.trigger(ch + ".disconnected", {}); } catch(e) {}
                thread.put(ch + ".closed", true);
            }
        }
    });

    /* One handler for all main-thread requests; picks a procedure and
     * replies once it finishes. */
    var reqEv = thread.onGet(ch + ".req.*", function(key, val) {
        thread.del(key);
        var req = val;
        var proc = procedures[req.proc];
        function reply(result, error) {
            if (req.syncToken) {
                thread.put(ch + ".res." + req.syncToken,
                           {result: result, error: error});
            }
            if (req.cbToken) {
                event.trigger(ch + ".cb." + req.cbToken,
                              {result: result, error: error});
            }
        }
        if (!proc) return reply(null, {message: "unknown proc: " + req.proc});
        try { proc(req.args || {}, reply); }
        catch (e) { reply(null, {message: String(e && e.message || e)}); }
    });

    var shutEv = thread.onGet(ch + ".shutdown", function(key) {
        thread.del(key);
        try { ws.wsClose(); } catch(e) {}
        try { reqEv.remove();  } catch(e) {}
        try { shutEv.remove(); } catch(e) {}
    });
}

/* ------------------------------------------------------------------ *
 *  Main-thread side
 * ------------------------------------------------------------------ */

/* Transpiler detection.  Marker for "the caller wants async/await /
 * Promise-flavored returns" — used by the tri-mode dispatcher to
 * pick between sync-return and Promise-return when no callback was
 * given.
 *
 * Two markers cover both source-transformation paths rampart
 * supports:
 *
 *   - `_TrN_Sp` is rampart's built-in transpiler helper namespace.
 *     It is created exactly when the transpiler is engaged via
 *     `-t`, `"use transpiler"`, or `"use transpilerGlobally"`.
 *
 *   - `global._babelPolyfill === true` is set by rampart's babel
 *     bootstrap (see `duk_rp_babelize` in `src/cmdline.c`) once the
 *     babel polyfill has loaded.  It is the equivalent marker for
 *     `-b`, `"use babel"`, or `"use babelGlobally"`.
 *
 * `typeof Promise === "function"` is not a usable signal: rampart
 * now installs `Promise` eagerly in plain ES5 contexts, so that
 * test would always be true. */
function _isTranspiled() {
    if (typeof _TrN_Sp !== "undefined") return true;
    if (typeof global !== "undefined" && global._babelPolyfill === true)
        return true;
    return false;
}

var _channelSeq = 0;
var _cdpIdSeq   = 0;
var _tokenSeq   = 0;

function _nextChannel() {
    return "chromeview-" + process.getpid() + "-" + (++_channelSeq);
}
function _nextId()    { return ++_cdpIdSeq; }
function _nextToken() { return ++_tokenSeq; }

/* Given a send function that schedules a CDP call and supplies
 * resolve/reject, pick the appropriate calling convention:
 *   cb given           -> register event.on, send, return undefined
 *   Promise available  -> return new Promise((res,rej)=>send(res,rej))
 *   else               -> register sync token, send, block on thread.del
 *
 * The sender gets (resolve, reject) where resolve/reject each take one
 * argument.  sender must actually kick off the CDP call.
 */
function _dispatch(ch, cb, sender) {
    if (typeof cb === "function") {
        var tok = _nextToken();
        var eventName = ch + ".cb." + tok;
        var funcName  = "cbfn-" + tok;
        rampart.event.on(eventName, funcName, function(uv, payload) {
            rampart.event.off(eventName, funcName);
            if (payload.error) cb(null, _cdpError(payload.error));
            else               cb(payload.result, null);
        });
        sender(
            function(r){ rampart.event.trigger(eventName, {result:r}); },
            function(e){ rampart.event.trigger(eventName, {error: _errToPlain(e)}); },
            {cbToken: tok}
        );
        return undefined;
    }

    if (_isTranspiled()) {
        return new Promise(function(resolve, reject) {
            var tok = _nextToken();
            var eventName = ch + ".cb." + tok;
            var funcName  = "pr-" + tok;
            rampart.event.on(eventName, funcName, function(uv, payload) {
                rampart.event.off(eventName, funcName);
                if (payload.error) reject(_cdpError(payload.error));
                else               resolve(payload.result);
            });
            sender(function(){}, function(){}, {cbToken: tok});
        });
    }

    /* sync */
    var syncTok = _nextToken();
    sender(function(){}, function(){}, {syncToken: syncTok});
    var payload = rampart.thread.del(ch + ".res." + syncTok, 60000);
    if (payload === undefined)
        throw new Error("rampart-chromeview: timed out waiting for CDP reply");
    if (payload.error) throw _cdpError(payload.error);
    return payload.result;
}

function _cdpError(err) {
    if (err && typeof err === "object" && err.message) {
        var e = new Error(err.message);
        e.code = err.code;
        return e;
    }
    return new Error(String(err));
}
function _errToPlain(e) {
    if (e instanceof Error) return {message: e.message};
    return {message: String(e)};
}

/* Send a procedure invocation to the worker.  Returns using whichever
 * calling convention _dispatch picks. */
function _sendProc(ch, procName, args, cb) {
    return _dispatch(ch, cb, function(resolve, reject, opts) {
        var id = _nextId();
        rampart.thread.put(ch + ".req." + id, {
            proc:      procName,
            args:      args || {},
            syncToken: opts.syncToken,
            cbToken:   opts.cbToken
        });
    });
}

/* Convenience: a raw single CDP call via the "raw" procedure. */
function _callCdp(ch, method, params, sessionId, evMethod, evSession, cb) {
    return _sendProc(ch, "raw", {
        method:    method,
        params:    params,
        sessionId: sessionId,
        evMethod:  evMethod,
        evSession: evSession
    }, cb);
}

/* ------------------------------------------------------------------ *
 *  Chrome launcher
 * ------------------------------------------------------------------ */

var _chromeCandidates = [
    /* Linux — distro packages */
    "/usr/bin/google-chrome-stable",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    /* Linux — Snap (default on modern Ubuntu) */
    "/snap/bin/chromium",
    /* FreeBSD — `pkg install chromium` lands here; the binary is
       named `chrome`, not `chromium`. */
    "/usr/local/bin/chrome",
    "/usr/local/bin/chromium",
    /* macOS — typical app-bundle locations */
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium"
];

function _locateChrome(override) {
    if (override) {
        if (!utils.stat(override))
            throw new Error("chrome executable not found: " + override);
        return override;
    }
    for (var i = 0; i < _chromeCandidates.length; i++) {
        if (utils.stat(_chromeCandidates[i])) return _chromeCandidates[i];
    }
    throw new Error("no chrome/chromium found; pass {executablePath: '...'}");
}

function _mkUserDataDir() {
    var path = "/tmp/rampart-chromeview-" + process.getpid() + "-" + (Date.now())
             + "-" + Math.floor(Math.random()*1e6);
    utils.mkdir(path);
    return path;
}

/* Spawn chrome as a child process, return {pid, wsEndpoint, userDataDir, cleanup}.
 * Chrome writes <user-data-dir>/DevToolsActivePort once the debugging socket
 * is bound: line 1 = port, line 2 = browser target ws path. */
function _launchChromeProc(opts) {
    var exe = _locateChrome(opts.executablePath);
    var userDataDir = opts.userDataDir || _mkUserDataDir();
    var ownedUserDataDir = !opts.userDataDir;

    var flags = [
        "--remote-debugging-port=0",
        "--user-data-dir=" + userDataDir,
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-features=Translate,BackForwardCache",
        "--disable-background-networking",
        "--disable-sync"
    ];
    if (opts.headless !== false) {
        flags.push("--headless=new");
        flags.push("--hide-scrollbars");
        flags.push("--mute-audio");
    }
    if (opts.args) flags = flags.concat(opts.args);

    var ret = utils.exec(exe, {background: true, args: flags});
    if (!ret || !ret.pid)
        throw new Error("failed to spawn chrome: " + exe);
    var pid = ret.pid;

    var portFile = userDataDir + "/DevToolsActivePort";
    var deadline = Date.now() + 15000;
    var portInfo = null;
    while (Date.now() < deadline) {
        try {
            var data = utils.readFile(portFile, true);
            if (data && data.indexOf("\n") > 0) {
                var lines = data.split("\n");
                var p = parseInt(lines[0], 10);
                if (p > 0 && lines[1]) {
                    portInfo = {port: p, wsPath: lines[1].replace(/[\r\n]+$/, "")};
                    break;
                }
            }
        } catch(e) {}
        /* Detect early exit of chrome */
        if (!utils.kill(pid, 0)) {
            throw new Error("chrome exited before debug port became ready");
        }
        utils.sleep(0.05);
    }
    if (!portInfo) {
        try { utils.kill(pid, 15); } catch(e) {}
        throw new Error("chrome failed to start (no DevToolsActivePort after 15s)");
    }

    return {
        pid:         pid,
        wsEndpoint:  "ws://127.0.0.1:" + portInfo.port + portInfo.wsPath,
        httpEndpoint:"http://127.0.0.1:" + portInfo.port,
        userDataDir: userDataDir,
        cleanup: function() {
            /* Ask chrome to exit, give its subprocesses a moment to go
             * away, then force-kill if needed.  Skipping the wait races
             * the rm -rf against chrome's zygote/renderer processes
             * still writing to the profile. */
            try { utils.kill(pid, 15); } catch(e) {}
            for (var i = 0; i < 20 && utils.kill(pid, 0); i++) utils.sleep(0.05);
            if (utils.kill(pid, 0)) {
                try { utils.kill(pid, 9); utils.sleep(0.1); } catch(e) {}
            }
            if (ownedUserDataDir && /\/tmp\/rampart-chromeview-/.test(userDataDir)) {
                try { utils.shell("rm -rf " + userDataDir); } catch(e) {}
            }
        }
    };
}

/* ------------------------------------------------------------------ *
 *  Browser / Page classes
 * ------------------------------------------------------------------ */

function Browser(opts) {
    this._channel = opts.channel;
    this._thread  = opts.thread;
    this._proc    = opts.proc;        /* null for connect() */
    this._browserSessionId = null;    /* attached after init */
    this._pages   = {};               /* targetId -> Page */
    /* targetId -> array of resolvers awaiting Page registration.
       Populated by Target.prototype.page() when called before the
       Page constructor has run for a targetId, drained by the
       Page constructor.  See the targetcreated race comment in
       Target.prototype.page below. */
    this._pagesPending = {};
    /* Promises returned by user-registered `targetcreated`/`targetchanged`/
       `targetdestroyed` event handlers that haven't settled yet.
       BrowserContext.newPage drains these before resolving so that
       puppeteer-extra-plugin's onPageCreated chain (which fires on
       targetcreated and registers stealth evasions, etc.) completes
       before the caller continues with goto/evaluate. */
    this._tcInflight = [];
    this._contextById = {};           /* contextId -> BrowserContext */
    this._defaultContext = new BrowserContext(this, "");   /* "" = default */

    /* Best-effort cleanup on script exit.  Duktape finalizers fire on
     * heap destruction, which `duk_rp_exit` invokes for process.exit(),
     * SIGTERM/SIGINT (via rampart's sigint_handler), parse errors, and
     * natural end-of-script.  We only register this for browsers we
     * launched ourselves — `connect()` leaves the chrome process alone.
     * Idempotent: skipped if browser.close() or disconnect() already ran. */
    if (this._proc && typeof Duktape !== "undefined"
        && typeof Duktape.fin === "function") {
        try {
            Duktape.fin(this, function(self) {
                if (self._proc && !self._closed && !self._disconnected) {
                    try { self._proc.cleanup(); } catch(_) {}
                }
            });
        } catch(_) {}
    }
}

Browser.prototype._call = function(method, params, sessionId, evMethod, evSession, cb) {
    return _callCdp(this._channel, method, params, sessionId, evMethod, evSession, cb);
};

Browser.prototype.newPage = function(cb) {
    return this._defaultContext.newPage(cb);
};

Browser.prototype.createBrowserContext = function(cb) {
    var self = this;
    function makeCtx(r) {
        var c = new BrowserContext(self, r.browserContextId);
        self._contextById[r.browserContextId] = c;
        return c;
    }
    var wrapped = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(makeCtx(r), null);
    } : null;
    var raw = _sendProc(self._channel, "createBrowserContext", {}, wrapped);
    if (wrapped) return;
    if (raw && typeof raw.then === "function") return raw.then(makeCtx);
    return makeCtx(raw);
};

Browser.prototype.defaultBrowserContext = function() {
    return this._defaultContext;
};

Browser.prototype.close = function(cb) {
    var self = this;
    self._closed = true;             /* suppress the finalizer's cleanup */
    rampart.thread.put(self._channel + ".shutdown", Date.now());
    rampart.thread.del(self._channel + ".closed", 1000);
    try { self._thread.close(); } catch(e) {}
    if (self._proc) self._proc.cleanup();

    if (cb) { cb(null, null); return; }
    if (_isTranspiled()) return Promise.resolve();
};

Browser.prototype.wsEndpoint = function() {
    return this._wsEndpoint;
};

/* ---------------- BrowserContext ---------------- */

function BrowserContext(browser, contextId) {
    this._browser   = browser;
    this._contextId = contextId;     /* "" for default */
}

BrowserContext.prototype.newPage = function(cb) {
    var self = this;
    var ch   = self._browser._channel;
    var args = {};
    if (self._contextId) args.browserContextId = self._contextId;

    var wrapped = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(new Page(self, r.targetId, r.sessionId, r.mainFrameId), null);
    } : null;
    var raw = _sendProc(ch, "newPage", args, wrapped);
    if (wrapped) return;
    if (raw && typeof raw.then === "function") {
        return raw.then(function(r) {
            var page = new Page(self, r.targetId, r.sessionId, r.mainFrameId);
            /* Drain in-flight targetcreated handlers so plugin chains
               (e.g. puppeteer-extra-plugin's onPageCreated → stealth
               evasions) finish before the caller's next goto/evaluate. */
            return _drainTargetCreated(self._browser).then(function () { return page; });
        });
    }
    return new Page(self, raw.targetId, raw.sessionId, raw.mainFrameId);
};

/* Give the event loop a tick so any pending targetcreated CDP event
   that has arrived but not yet been dispatched can fire and register
   its handler's Promise in browser._tcInflight, then await all of
   them.  Bounded by a small ceiling — plugin handlers that take
   longer than the wait are abandoned (the page returns from newPage
   and the handler resolves later, just possibly racing the first
   goto).  In practice every stealth evasion finishes well under
   the ceiling. */
function _drainTargetCreated(browser) {
    return new Promise(function (res) {
        setTimeout(function () {
            var inflight = browser._tcInflight;
            if (!inflight || !inflight.length) return res();
            Promise.all(inflight.slice()).then(function () { res(); },
                                               function () { res(); });
        }, 20);
    });
}

BrowserContext.prototype.close = function(cb) {
    if (!this._contextId) {
        if (cb) return cb(null, null);
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    return _sendProc(this._browser._channel, "disposeBrowserContext",
        {browserContextId: this._contextId}, cb);
};

/* ---------------- Page ---------------- */

function Page(context, targetId, sessionId, mainFrameId) {
    this._context   = context;
    this._browser   = context._browser;
    this._channel   = context._browser._channel;
    this._targetId  = targetId;
    this._sessionId = sessionId;
    this._mainFrameId = mainFrameId || null;
    this._handlers  = {};
    this._mouse     = null;
    this._keyboard  = null;
    this._interceptionOn = false;
    this._requestHandlers = [];
    /* Per-page state */
    this._defaultTimeout         = 30000;
    this._defaultNavigationTimeout = null;   /* null -> falls back to _defaultTimeout */
    /* Request tracking maps, networkId-keyed.  Populated by an always-on
     * internal listener so request.response()/response.request()/
     * request.failure() work regardless of whether interception is on. */
    this._reqByNetId  = {};
    this._respByNetId = {};
    _setupRequestTracking(this);
    if (this._browser && this._browser._pages)
        this._browser._pages[targetId] = this;
    /* Resolve any target.page() promises waiting for this Page to register.
       Used by puppeteer-extra-plugin's _onTargetCreated, which awaits
       `target.page()` to deliver `onPageCreated(page)` to each plugin. */
    if (this._browser && this._browser._pagesPending) {
        var pendingMap = this._browser._pagesPending;
        var resolvers = pendingMap[targetId];
        if (resolvers) {
            delete pendingMap[targetId];
            for (var __i = 0; __i < resolvers.length; __i++) {
                try { resolvers[__i](this); } catch (__e) {}
            }
        }
    }
}

/* Returns the navigation timeout if the call is navigation-flavored,
 * otherwise the general default.  opts.timeout, if provided, wins. */
function _timeoutOf(page, opts, navFlavored) {
    if (opts && opts.timeout !== undefined && opts.timeout !== null)
        return opts.timeout;
    if (navFlavored && page._defaultNavigationTimeout !== null)
        return page._defaultNavigationTimeout;
    return page._defaultTimeout;
}

/* Always-on tracker: builds Network-flavored Request objects so
 * page.on('requestfailed'/'requestfinished') and request.response() work
 * even when the user hasn't enabled Fetch-based interception. */
function _setupRequestTracking(page) {
    var ch = page._channel, sid = page._sessionId;
    function listen(method, tag, fn) {
        rampart.event.on(ch + ".ev." + sid + "." + method,
            "trk-" + tag + "-" + sid + "-" + (_tokenSeq++), fn);
    }
    listen("Network.requestWillBeSent", "wb", function(uv, payload) {
        var p = payload.params || {};
        if (page._reqByNetId[p.requestId]) return;
        var req = new Request(page, {
            requestId:    p.requestId,
            networkId:    p.requestId,
            request:      p.request,
            resourceType: p.type,
            frameId:      p.frameId
        });
        page._reqByNetId[p.requestId] = req;
    });
    listen("Network.responseReceived", "rr", function(uv, payload) {
        var p = payload.params || {};
        if (page._respByNetId[p.requestId]) return;
        var resp = new Response(page, p);
        page._respByNetId[p.requestId] = resp;
        var req = page._reqByNetId[p.requestId];
        if (req) { req._response = resp; resp._request = req; }
    });
    listen("Network.loadingFailed", "lf", function(uv, payload) {
        var p = payload.params || {};
        var req = page._reqByNetId[p.requestId];
        if (req) req._failure = {errorText: p.errorText || ""};
    });
}

Page.prototype._call = function(method, params, evMethod, cb) {
    return _callCdp(this._channel, method, params, this._sessionId,
                    evMethod, evMethod ? this._sessionId : null, cb);
};

Page.prototype.goto = function(url, optsOrCb, cb) {
    var opts;
    if (typeof optsOrCb === "function") { cb = optsOrCb; opts = {}; }
    else opts = optsOrCb || {};

    var waitUntil = opts.waitUntil || "load";
    var evMap = {
        "load":             "Page.loadEventFired",
        "domcontentloaded": "Page.domContentEventFired"
        /* networkidle0/2 deferred */
    };
    var evMethod = evMap[waitUntil] || null;
    var params = {url: url};
    if (opts.referrer) params.referrer = opts.referrer;
    return this._call("Page.navigate", params, evMethod, cb);
};

Page.prototype.reload = function(cb) {
    return this._call("Page.reload", {}, "Page.loadEventFired", cb);
};

/* Accept either a source string or a function.  Under "use transpiler"
 * fn.toString() returns real source; under plain Duktape it returns
 * the stub "function () { [ecmascript code] }" which we reject. */
function _requireSource(fnOrStr, what) {
    if (typeof fnOrStr === "string") return fnOrStr;
    if (typeof fnOrStr === "function") {
        var s = fnOrStr.toString();
        if (s.indexOf("[ecmascript code]") >= 0) {
            throw new Error("rampart-chromeview: " + what + " is a function"
                + " whose source is not available.  Pass a source string"
                + " (e.g. \"el => el.textContent\"), or add \"use transpiler\""
                + " at the top of your script so Rampart preserves function"
                + " source.");
        }
        return s;
    }
    throw new Error("rampart-chromeview: " + what + " must be a string or function");
}

function _toExprSource(fnOrStr, args) {
    var src = _requireSource(fnOrStr, "page expression");
    if (typeof fnOrStr === "function") {
        var argsJson = (args || []).map(function(a){ return JSON.stringify(a); }).join(",");
        return "(" + src + ")(" + argsJson + ")";
    }
    return src;
}

Page.prototype.evaluate = function(fnOrStr /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length - 1] === "function")
        cb = args.pop();
    var src = _toExprSource(fnOrStr, args);
    var self = this;
    var params = {
        expression:    src,
        returnByValue: true,
        awaitPromise:  true,
        userGesture:   true
    };
    function unwrap(result) {
        if (!result) return undefined;
        if (result.exceptionDetails) {
            var ed = result.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description)
                            || ed.text || "evaluate failed");
        }
        return result.result ? result.result.value : undefined;
    }
    var raw = _callCdp(this._channel, "Runtime.evaluate", params,
                       this._sessionId, null, null,
                       cb ? function(r, err) {
                           if (err) return cb(null, err);
                           try { cb(unwrap(r), null); }
                           catch(e) { cb(null, e); }
                       } : null);
    if (cb) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Page.prototype.content = function(cb) {
    return this.evaluate("document.documentElement.outerHTML", cb);
};

Page.prototype.setContent = function(html, cb) {
    if (this._mainFrameId) {
        return this._call("Page.setDocumentContent",
            {frameId: this._mainFrameId, html: html}, null, cb);
    }
    var src = "document.open();document.write(" + JSON.stringify(html)
            + ");document.close();";
    return this.evaluate(src, cb);
};

Page.prototype.title = function(cb) {
    return this.evaluate("document.title", cb);
};

Page.prototype.url = function(cb) {
    return this.evaluate("location.href", cb);
};

/* page.screencast({onFrame?, fps?, format?, quality?, maxWidth?, maxHeight?, crop?})
   Returns a recorder handle with a `stop()` method.  Each frame arrives via
   CDP `Page.screencastFrame`; we ack every frame so Chrome keeps streaming,
   and either fire the user's `onFrame(frame)` callback or accumulate
   `{timestamp, data}` entries until `recorder.stop()` returns them as a list.

   Higher-level libraries (`puppeteer-screen-recorder` et al) typically wrap
   this with ffmpeg or a WebM mux on top of the raw JPEG/PNG frame stream —
   that encoding is out of scope here.  We expose the CDP plumbing.

   Options:
     onFrame    function(frame): called for each frame; if absent, frames are
                stashed in the recorder and returned by stop().  `frame` is
                `{timestamp, data}` where data is a Buffer of the JPEG/PNG
                bytes (or the base64 string if opts.encoding === "base64").
     format     "jpeg" (default) or "png"
     quality    1..100 (JPEG only)
     fps        ~frames-per-second target.  CDP doesn't take fps directly;
                we translate to `everyNthFrame = max(1, round(60 / fps))`,
                approximating from chrome's ~60-fps capture cadence.
     maxWidth/maxHeight  passed through
     crop       {x, y, width, height} — CDP doesn't crop directly; we set
                maxWidth/maxHeight from crop.width/height as a hint.
                Post-process cropping is left to the user/library.
     encoding   "base64" leaves data as base64 string; default returns Buffer */
Page.prototype.screencast = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;

    var params = { format: opts.format || "jpeg" };
    if (opts.quality   !== undefined) params.quality   = opts.quality;
    if (opts.maxWidth  !== undefined) params.maxWidth  = opts.maxWidth;
    if (opts.maxHeight !== undefined) params.maxHeight = opts.maxHeight;
    if (opts.crop) {
        /* Best-effort: CDP startScreencast doesn't accept a rect, but
           limiting the captured frame size is the closest analog. */
        if (params.maxWidth  === undefined) params.maxWidth  = opts.crop.width;
        if (params.maxHeight === undefined) params.maxHeight = opts.crop.height;
    }
    if (opts.fps !== undefined) {
        var n = Math.max(1, Math.round(60 / opts.fps));
        params.everyNthFrame = n;
    }
    var base64 = opts.encoding === "base64";

    var ch  = self._channel;
    var sid = self._sessionId;

    /* Subscribe to screencastFrame BEFORE startScreencast so we don't drop
       the first frame.  Per-recorder handler — multiple concurrent
       recordings on the same page would multi-fire, but that's a
       pathological case (Chrome only streams one screencast at a time
       anyway and re-starting tears down the previous one). */
    var frames = [];
    var stopped = false;
    var onFrame = (typeof opts.onFrame === "function") ? opts.onFrame : null;
    var evName  = ch + ".ev." + sid + ".Page.screencastFrame";
    var fnName  = "screencast-" + sid + "-" + (_tokenSeq++);
    rampart.event.on(evName, fnName, function(uv, payload) {
        if (stopped) return;
        var p = payload.params || {};
        var data = p.data;
        var frame = {
            timestamp: p.metadata && p.metadata.timestamp,
            data:      base64 ? data : (data ? utils.bprintf("%!B", data) : null)
        };
        /* Ack first so the stream keeps flowing even if onFrame is slow. */
        if (p.sessionId !== undefined) {
            _callCdp(ch, "Page.screencastFrameAck",
                {sessionId: p.sessionId}, sid, null, null, function(){});
        }
        if (onFrame) {
            try { onFrame(frame); } catch (e) {
                utils.fprintf(utils.stderr,
                    "screencast onFrame threw: %s\n",
                    (e && e.message) || e);
            }
        } else {
            frames.push(frame);
        }
    });

    function buildRecorder() {
        return {
            stop: function (cb2) {
                if (stopped) {
                    if (cb2) { cb2(frames, null); return; }
                    if (_isTranspiled()) return Promise.resolve(frames);
                    return frames;
                }
                stopped = true;
                try { rampart.event.off(evName, fnName); } catch(_) {}
                function unwrap() { return frames; }
                var cbw2 = cb2 ? function(_, err) {
                    if (err) return cb2(null, err);
                    cb2(unwrap(), null);
                } : null;
                var raw = _callCdp(ch, "Page.stopScreencast", {},
                    sid, null, null, cbw2);
                if (cbw2) return;
                if (raw && typeof raw.then === "function") return raw.then(unwrap);
                return unwrap();
            },
            frames: function () { return frames.slice(); }
        };
    }

    if (typeof cb === "function") {
        _callCdp(ch, "Page.startScreencast", params, sid, null, null,
            function(_, err) {
                if (err) {
                    try { rampart.event.off(evName, fnName); } catch(_) {}
                    return cb(null, err);
                }
                cb(buildRecorder(), null);
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.screencast(opts, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    /* sync */
    try {
        _callCdp(ch, "Page.startScreencast", params, sid, null, null, null);
    } catch (e) {
        try { rampart.event.off(evName, fnName); } catch(_) {}
        throw e;
    }
    return buildRecorder();
};

Page.prototype.screenshot = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var params = {
        format: opts.type || "png",
        captureBeyondViewport: !!opts.fullPage
    };
    if (opts.quality !== undefined) params.quality = opts.quality;
    if (opts.clip) params.clip = {
        x: opts.clip.x, y: opts.clip.y,
        width: opts.clip.width, height: opts.clip.height,
        scale: opts.clip.scale || 1
    };
    function unwrap(r) {
        if (!r) return null;
        return (opts.encoding === "base64") ? r.data : utils.bprintf("%!B", r.data);
    }
    var raw = _callCdp(this._channel, "Page.captureScreenshot", params,
                       this._sessionId, null, null,
                       cb ? function(r, err) {
                           if (err) return cb(null, err);
                           cb(unwrap(r), null);
                       } : null);
    if (cb) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* Parse a Puppeteer-shaped length: number-of-inches, or a string with a
 * unit suffix (px / in / cm / mm).  Returns inches as a number. */
function _toInches(v, dflt) {
    if (v === undefined || v === null) return dflt;
    if (typeof v === "number") return v;
    var m = String(v).match(/^([\d.]+)\s*(px|in|cm|mm)?$/i);
    if (!m) return dflt;
    var n = parseFloat(m[1]);
    var u = (m[2] || "in").toLowerCase();
    if (u === "in") return n;
    if (u === "px") return n / 96;
    if (u === "cm") return n / 2.54;
    if (u === "mm") return n / 25.4;
    return dflt;
}

Page.prototype.pdf = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    /* Margin: accept Puppeteer's {top, bottom, left, right} object with
     * unit suffixes OR per-side marginTop/Bottom/Left/Right (legacy). */
    var m = opts.margin || {};
    var params = {
        landscape:        !!opts.landscape,
        printBackground:  !!opts.printBackground,
        scale:            opts.scale || 1,
        paperWidth:       _toInches(opts.width  || opts.paperWidth,  8.5),
        paperHeight:      _toInches(opts.height || opts.paperHeight, 11),
        marginTop:        _toInches(m.top    !== undefined ? m.top    : opts.marginTop,    0.4),
        marginBottom:     _toInches(m.bottom !== undefined ? m.bottom : opts.marginBottom, 0.4),
        marginLeft:       _toInches(m.left   !== undefined ? m.left   : opts.marginLeft,   0.4),
        marginRight:      _toInches(m.right  !== undefined ? m.right  : opts.marginRight,  0.4),
        preferCSSPageSize:!!opts.preferCSSPageSize
    };
    if (opts.format) {
        var fmt = _paperFormats[opts.format.toLowerCase()];
        if (fmt) { params.paperWidth = fmt.width; params.paperHeight = fmt.height; }
    }
    if (opts.displayHeaderFooter) {
        params.displayHeaderFooter = true;
        if (opts.headerTemplate !== undefined) params.headerTemplate = opts.headerTemplate;
        if (opts.footerTemplate !== undefined) params.footerTemplate = opts.footerTemplate;
    }
    if (opts.pageRanges) params.pageRanges = opts.pageRanges;
    var path = opts.path;
    function unwrap(r) {
        if (!r) return null;
        var buf = utils.bprintf("%!B", r.data);
        if (path) { try { utils.fprintf(path, "%s", buf); } catch(_) {} }
        return buf;
    }
    var raw = _callCdp(this._channel, "Page.printToPDF", params,
                       this._sessionId, null, null,
                       cb ? function(r, err) {
                           if (err) return cb(null, err);
                           cb(unwrap(r), null);
                       } : null);
    if (cb) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};
var _paperFormats = {
    "letter": {width: 8.5,  height: 11},
    "legal":  {width: 8.5,  height: 14},
    "tabloid":{width: 11,   height: 17},
    "a0":     {width: 33.1, height: 46.8},
    "a1":     {width: 23.4, height: 33.1},
    "a2":     {width: 16.54,height: 23.4},
    "a3":     {width: 11.69,height: 16.54},
    "a4":     {width: 8.27, height: 11.69},
    "a5":     {width: 5.83, height: 8.27},
    "a6":     {width: 4.13, height: 5.83}
};

Page.prototype.setViewport = function(vp, cb) {
    return this._call("Emulation.setDeviceMetricsOverride", {
        width:  vp.width,
        height: vp.height,
        deviceScaleFactor: vp.deviceScaleFactor || 1,
        mobile: !!vp.isMobile
    }, null, cb);
};

Page.prototype.setUserAgent = function(ua, cb) {
    return this._call("Network.setUserAgentOverride", {userAgent: ua}, null, cb);
};

Page.prototype.setExtraHTTPHeaders = function(headers, cb) {
    return this._call("Network.setExtraHTTPHeaders", {headers: headers}, null, cb);
};

Page.prototype.close = function(cb) {
    if (this._browser && this._browser._pages)
        delete this._browser._pages[this._targetId];
    return _callCdp(this._channel, "Target.closeTarget",
        {targetId: this._targetId}, null, null, null, cb);
};

Page.prototype.on = function(event, handler) {
    /* Map Puppeteer event names to CDP method names */
    var evMap = {
        "load":           "Page.loadEventFired",
        "domcontentloaded":"Page.domContentEventFired",
        "console":        "Runtime.consoleAPICalled",
        "pageerror":      "Runtime.exceptionThrown",
        "dialog":         "Page.javascriptDialogOpening",
        "response":       "Network.responseReceived",
        "framenavigated": "Page.frameNavigated",
        "request":        "Fetch.requestPaused",
        "requestfailed":  "Network.loadingFailed",
        "requestfinished":"Network.loadingFinished"
    };
    var cdpMethod = evMap[event];
    if (!cdpMethod)
        throw new Error("rampart-chromeview: unsupported event '" + event + "'");

    var self = this;
    var eventName = this._channel + ".ev." + this._sessionId + "." + cdpMethod;
    var funcName  = "h-" + this._sessionId + "-" + event + "-" + (_tokenSeq++);
    rampart.event.on(eventName, funcName, function(uv, payload) {
        try {
            _dispatchPageEvent(event, handler, payload, self);
        } catch(e) {
            utils.fprintf(utils.stderr, "rampart-chromeview: handler threw: %s\n", e);
        }
    });
    return this;
};

function _dispatchPageEvent(name, handler, payload, page) {
    var p = payload.params || {};
    switch (name) {
        case "load":
        case "domcontentloaded":
            handler();
            return;
        case "console":
            handler(new ConsoleMessage(page, p));
            return;
        case "pageerror":
            var ed = p.exceptionDetails || {};
            var msg = (ed.exception && ed.exception.description) || ed.text || "error";
            handler(new Error(msg));
            return;
        case "framenavigated":
            handler(p.frame);
            return;
        case "request":
            /* Fetch-flavored Request: has continue/abort/respond. */
            var freq = new Request(page, p);
            if (p.networkId && page._respByNetId[p.networkId]) {
                freq._response = page._respByNetId[p.networkId];
            }
            handler(freq);
            return;
        case "response":
            /* Prefer the cached Response from the tracker so request<->response
             * cross-linking is intact regardless of handler ordering. */
            var rid = p.requestId;
            var resp = page._respByNetId[rid];
            if (!resp) {
                resp = new Response(page, p);
                page._respByNetId[rid] = resp;
                var rreq = page._reqByNetId[rid];
                if (rreq) { rreq._response = resp; resp._request = rreq; }
            }
            handler(resp);
            return;
        case "requestfailed":
        case "requestfinished":
            var nreq = page._reqByNetId[p.requestId];
            if (!nreq) {
                /* No tracked request — synthesize a minimal one so the
                 * handler still receives something with a usable url().
                 * Routed through _trackReq so the cap eviction sees it. */
                nreq = new Request(page, {
                    requestId: p.requestId,
                    networkId: p.requestId,
                    request:   {url: "", method: ""}
                });
                _trackReq(page, p.requestId, nreq);
            }
            if (name === "requestfailed") {
                nreq._failure = {errorText: p.errorText || ""};
            } else {
                nreq._finished = true;
            }
            handler(nreq);
            return;
        case "dialog":
            handler(new Dialog(page, p));
            return;
        default:
            handler(p);
    }
}

/* ------------------------------------------------------------------ *
 *  Selectors: $, $$, $eval, $$eval
 *
 *  $ and $$ resolve elements to remote object ids (via Runtime.evaluate
 *  with returnByValue:false) and return ElementHandle wrappers that use
 *  Runtime.callFunctionOn for subsequent operations.  $eval / $$eval run
 *  entirely in-page — simpler and usually what you want for scraping.
 * ------------------------------------------------------------------ */

Page.prototype.$ = function(selector, cb) {
    var self = this;
    var params = {
        expression:    "document.querySelector(" + JSON.stringify(selector) + ")",
        returnByValue: false
    };
    function unwrap(r) {
        if (!r || !r.result) return null;
        var o = r.result;
        if (o.subtype === "null" || o.type === "undefined") return null;
        if (!o.objectId) return null;
        return new ElementHandle(self, o.objectId);
    }
    return _callCdpUnwrap(this, "Runtime.evaluate", params, unwrap, cb);
};

function _elemsFromProps(page, r) {
    if (!r || !r.result) return [];
    var out = [];
    for (var i = 0; i < r.result.length; i++) {
        var prop = r.result[i];
        if (!/^\d+$/.test(prop.name)) continue;
        if (prop.value && prop.value.objectId)
            out.push(new ElementHandle(page, prop.value.objectId));
    }
    return out;
}

Page.prototype.$$ = function(selector, cb) {
    var self = this;
    var evParams = {
        expression:    "Array.from(document.querySelectorAll(" + JSON.stringify(selector) + "))",
        returnByValue: false
    };
    if (typeof cb === "function") {
        _callCdp(self._channel, "Runtime.evaluate", evParams,
                 self._sessionId, null, null, function(r1, e1) {
            if (e1) return cb(null, e1);
            if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
            _callCdp(self._channel, "Runtime.getProperties",
                     {objectId: r1.result.objectId, ownProperties: true},
                     self._sessionId, null, null, function(r2, e2) {
                if (e2) return cb(null, e2);
                cb(_elemsFromProps(self, r2), null);
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$$(selector, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    /* sync */
    var r1 = _callCdp(self._channel, "Runtime.evaluate", evParams,
                      self._sessionId, null, null, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
                      {objectId: r1.result.objectId, ownProperties: true},
                      self._sessionId, null, null, null);
    return _elemsFromProps(self, r2);
};

Page.prototype.$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "$eval page function");
    var src = "(function(){"
            + "  var el = document.querySelector(" + JSON.stringify(selector) + ");"
            + "  if (!el) throw new Error('$eval: no element matched ' + " + JSON.stringify(selector) + ");"
            + "  return (" + fnSrc + ").apply(null, [el].concat(" + JSON.stringify(args) + "));"
            + "})()";
    return this.evaluate(src, cb);
};

Page.prototype.$$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "$$eval page function");
    var src = "(function(){"
            + "  var els = Array.from(document.querySelectorAll(" + JSON.stringify(selector) + "));"
            + "  return (" + fnSrc + ").apply(null, [els].concat(" + JSON.stringify(args) + "));"
            + "})()";
    return this.evaluate(src, cb);
};

/* ------------------------------------------------------------------ *
 *  JSHandle — Puppeteer's parent class for remote-object references.
 *  ElementHandle inherits from this.  Use page.evaluateHandle to get a
 *  JSHandle for arbitrary in-page values (functions, Window, etc.) that
 *  cannot be serialized over CDP.
 * ------------------------------------------------------------------ */

function JSHandle(page, remoteObject) {
    this._page     = page;
    this._channel  = page._channel;
    this._sessionId= page._sessionId;
    /* Accept either a raw RemoteObject from CDP or a bare objectId. */
    if (remoteObject && typeof remoteObject === "object") {
        this._objectId = remoteObject.objectId;
        this._remote   = remoteObject;
    } else {
        this._objectId = remoteObject;
        this._remote   = null;
    }
    this._disposed = false;
}

/* Run fn on the page side with this handle bound as the first argument
 * (Puppeteer convention).  Args are passed by value. */
JSHandle.prototype.evaluate = function(fn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(fn, "JSHandle.evaluate function");
    var self = this;
    function unwrap(r) {
        if (!r) return undefined;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "evaluate failed");
        }
        return r.result ? r.result.value : undefined;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(null,[this].concat(Array.prototype.slice.call(arguments)));}";
    var raw = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId:      self._objectId,
        functionDeclaration: wrapperSrc,
        arguments:     args.map(function(a){ return {value: a}; }),
        returnByValue: true,
        awaitPromise:  true
    }, self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* Same as evaluate, but the returned value is wrapped as a JSHandle
 * (or ElementHandle for DOM nodes) rather than serialized. */
JSHandle.prototype.evaluateHandle = function(fn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(fn, "JSHandle.evaluateHandle function");
    var self = this;
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(null,[this].concat(Array.prototype.slice.call(arguments)));}";
    function unwrap(r) {
        if (!r) return null;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "evaluateHandle failed");
        }
        return _wrapRemote(self._page, r.result);
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId:      self._objectId,
        functionDeclaration: wrapperSrc,
        arguments:     args.map(function(a){ return {value: a}; }),
        returnByValue: false,
        awaitPromise:  true
    }, self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

JSHandle.prototype.getProperty = function(name, cb) {
    return this.evaluateHandle(
        "function(obj, k){return obj[k];}", name, cb);
};

JSHandle.prototype.getProperties = function(cb) {
    var self = this;
    function unwrap(r) {
        var out = {};
        if (!r || !r.result) return out;
        for (var i = 0; i < r.result.length; i++) {
            var prop = r.result[i];
            if (prop.value) out[prop.name] = _wrapRemote(self._page, prop.value);
        }
        return out;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _callCdp(self._channel, "Runtime.getProperties",
        {objectId: self._objectId, ownProperties: true},
        self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* Serialize this handle's value (returns raw value). */
JSHandle.prototype.jsonValue = function(cb) {
    /* Primitive remote has no objectId — we already know the value. */
    if (this._remote && !this._objectId) {
        var v = this._remote.value;
        if (this._remote.type === "undefined") v = undefined;
        if (cb) { cb(v, null); return; }
        if (_isTranspiled()) return Promise.resolve(v);
        return v;
    }
    return this.evaluate("function(o){return o;}", cb);
};

/* Return this as an ElementHandle if it's a DOM node, else null. */
JSHandle.prototype.asElement = function() {
    if (this instanceof ElementHandle) return this;
    return null;
};

JSHandle.prototype.dispose = function(cb) {
    if (this._disposed || !this._objectId) {
        this._disposed = true;
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._disposed = true;
    return _callCdp(this._channel, "Runtime.releaseObject",
        {objectId: this._objectId}, this._sessionId, null, null, cb);
};

JSHandle.prototype.toString = function() {
    if (!this._remote) return "JSHandle@" + this._objectId;
    if (this._remote.subtype) return "JSHandle:" + this._remote.subtype;
    return "JSHandle:" + (this._remote.type || "object");
};

/* Wrap a CDP RemoteObject into the appropriate JS-side handle.
 * DOM nodes become ElementHandle; everything else becomes JSHandle
 * (including primitives, which Puppeteer also wraps so .jsonValue()
 * works uniformly). */
function _wrapRemote(page, remote) {
    if (!remote) return null;
    if (remote.subtype === "node") return new ElementHandle(page, remote);
    return new JSHandle(page, remote);
}

/* ------------------------------------------------------------------ *
 *  ElementHandle — JSHandle for a DOM node.  Adds DOM-specific helpers.
 * ------------------------------------------------------------------ */

function ElementHandle(page, objectId) {
    /* Accept either a raw RemoteObject or a bare objectId for back-compat
     * with code that does `new ElementHandle(page, "{...}")`. */
    JSHandle.call(this, page, objectId);
}
ElementHandle.prototype = Object.create(JSHandle.prototype);
ElementHandle.prototype.constructor = ElementHandle;
/* asElement: ElementHandles ARE elements. */
ElementHandle.prototype.asElement = function() { return this; };

ElementHandle.prototype.evaluate = function(fn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(fn, "ElementHandle.evaluate function");
    var self = this;
    function unwrap(r) {
        if (!r) return undefined;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "element evaluate failed");
        }
        return r.result ? r.result.value : undefined;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    /* CDP passes the element as `this`; wrap so the user's fn sees it as
     * the first argument (Puppeteer convention). */
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(null,[this].concat(Array.prototype.slice.call(arguments)));}";
    var raw = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId:      self._objectId,
        functionDeclaration: wrapperSrc,
        arguments:     args.map(function(a){ return {value: a}; }),
        returnByValue: true,
        awaitPromise:  true
    }, self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

ElementHandle.prototype.click = function(cb) {
    return this.evaluate(
        "function(el){el.scrollIntoView({block:'center',inline:'center'});el.click();}",
        cb);
};

ElementHandle.prototype.focus = function(cb) {
    return this.evaluate("function(el){el.focus();}", cb);
};

ElementHandle.prototype.type = function(text, cb) {
    return this.evaluate(
        "function(el,t){"
        + "el.focus();"
        + "var p=Object.getPrototypeOf(el);"
        + "var d=Object.getOwnPropertyDescriptor(p,'value');"
        + "var s=d&&d.set;"
        + "if(s)s.call(el,(el.value||'')+t);else el.value=(el.value||'')+t;"
        + "el.dispatchEvent(new Event('input',{bubbles:true}));"
        + "el.dispatchEvent(new Event('change',{bubbles:true}));"
        + "}",
        text, cb);
};

ElementHandle.prototype.hover = function(cb) {
    return this.evaluate(
        "function(el){"
        + "el.scrollIntoView({block:'center',inline:'center'});"
        + "var r=el.getBoundingClientRect();"
        + "['mouseover','mouseenter','mousemove'].forEach(function(n){"
        + "el.dispatchEvent(new MouseEvent(n,{bubbles:true,cancelable:true,"
        + "clientX:r.left+r.width/2,clientY:r.top+r.height/2}));});"
        + "}",
        cb);
};

ElementHandle.prototype.boundingBox = function(cb) {
    return this.evaluate(
        "function(el){var r=el.getBoundingClientRect();"
        + "if(!r.width||!r.height)return null;"
        + "return {x:r.left,y:r.top,width:r.width,height:r.height};}",
        cb);
};

ElementHandle.prototype.textContent = function(cb) {
    return this.evaluate("function(el){return el.textContent;}", cb);
};

ElementHandle.prototype.getAttribute = function(name, cb) {
    return this.evaluate("function(el,n){return el.getAttribute(n);}", name, cb);
};

ElementHandle.prototype.dispose = function(cb) {
    if (this._disposed) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._disposed = true;
    return _callCdp(this._channel, "Runtime.releaseObject",
        {objectId: this._objectId}, this._sessionId, null, null, cb);
};

/* ------------------------------------------------------------------ *
 *  Page-level conveniences that build on selectors
 * ------------------------------------------------------------------ */

/* Real mouse-event click: resolve element, scrollIntoView, mouse at center.
 * opts: {button, clickCount, delay}.  Use {jsClick:true} to bypass the
 * mouse dispatch and invoke the element's .click() via JS instead
 * (useful when the target has no bounding box, e.g. display:none). */
Page.prototype.click = function(selector, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;

    if (opts.jsClick) {
        return this.$eval(selector,
            "function(el){el.scrollIntoView({block:'center',inline:'center'});el.click();}",
            cb);
    }

    function doChain(finish) {
        self.$(selector, function(handle, err) {
            if (err) return finish(err);
            if (!handle) return finish(new Error("click: no element matches " + selector));
            handle.evaluate(
                "function(el){el.scrollIntoView({block:'center',inline:'center'});}",
                function(_, e1) {
                    if (e1) { handle.dispose(function(){ finish(e1); }); return; }
                    handle.boundingBox(function(box, e2) {
                        if (e2 || !box) {
                            handle.dispose(function(){
                                finish(e2 || new Error("click: element has no bounding box"));
                            });
                            return;
                        }
                        var cx = box.x + box.width/2;
                        var cy = box.y + box.height/2;
                        self.mouse.click(cx, cy, opts, function(_, e3) {
                            handle.dispose(function(){ finish(e3); });
                        });
                    });
                });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    /* sync */
    var handle = self.$(selector);
    if (!handle) throw new Error("click: no element matches " + selector);
    try {
        handle.evaluate("function(el){el.scrollIntoView({block:'center',inline:'center'});}");
        var box = handle.boundingBox();
        if (!box) throw new Error("click: element has no bounding box");
        self.mouse.click(box.x + box.width/2, box.y + box.height/2, opts);
    } finally {
        handle.dispose();
    }
};

Page.prototype.focus = function(selector, cb) {
    return this.$eval(selector, "function(el){el.focus();}", cb);
};

Page.prototype.hover = function(selector, cb) {
    return this.$eval(selector,
        "function(el){"
        + "el.scrollIntoView({block:'center',inline:'center'});"
        + "var r=el.getBoundingClientRect();"
        + "['mouseover','mouseenter','mousemove'].forEach(function(n){"
        + "el.dispatchEvent(new MouseEvent(n,{bubbles:true,cancelable:true,"
        + "clientX:r.left+r.width/2,clientY:r.top+r.height/2}));});}", cb);
};

Page.prototype.type = function(selector, text, cb) {
    return this.$eval(selector,
        "function(el,t){"
        + "el.focus();"
        + "var p=Object.getPrototypeOf(el);"
        + "var d=Object.getOwnPropertyDescriptor(p,'value');"
        + "var s=d&&d.set;"
        + "if(s)s.call(el,(el.value||'')+t);else el.value=(el.value||'')+t;"
        + "el.dispatchEvent(new Event('input',{bubbles:true}));"
        + "el.dispatchEvent(new Event('change',{bubbles:true}));}", text, cb);
};

/* ------------------------------------------------------------------ *
 *  waitForSelector / waitForFunction — poll in-page using Promise.
 *  awaitPromise:true in Runtime.evaluate makes this transparently async.
 * ------------------------------------------------------------------ */

Page.prototype.waitForSelector = function(selector, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this, opts, false);
    var polling = opts.polling || 50;
    var src =
        "new Promise(function(resolve, reject) {"
        + "  var sel = " + JSON.stringify(selector) + ";"
        + "  var start = Date.now(), t = " + (+timeout) + ", poll = " + (+polling) + ";"
        + "  (function check() {"
        + "    var el = document.querySelector(sel);"
        + "    if (el) return resolve(true);"
        + "    if (t > 0 && Date.now() - start > t) return reject(new Error('waitForSelector: timeout waiting for ' + sel));"
        + "    setTimeout(check, poll);"
        + "  })();"
        + "})";
    return this.evaluate(src, cb);
};

Page.prototype.waitForFunction = function(pageFn, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this, opts, false);
    var polling = opts.polling || 50;
    var fnSrc = _requireSource(pageFn, "waitForFunction page function");
    var src =
        "new Promise(function(resolve, reject) {"
        + "  var fn = (" + fnSrc + ");"
        + "  var start = Date.now(), t = " + (+timeout) + ", poll = " + (+polling) + ";"
        + "  (function check() {"
        /* resolve with true (not r) -- r may be a non-serializable DOM/Window
         * object which would blow up CDP's value serialization. */
        + "    try { if (fn()) return resolve(true); } catch(e) { return reject(e); }"
        + "    if (t > 0 && Date.now() - start > t) return reject(new Error('waitForFunction: timeout'));"
        + "    setTimeout(check, poll);"
        + "  })();"
        + "})";
    return this.evaluate(src, cb);
};

Page.prototype.waitForTimeout = function(ms, cb) {
    return this.evaluate("new Promise(r => setTimeout(r, " + (+ms) + "))", cb);
};

/* ------------------------------------------------------------------ *
 *  Cookies
 * ------------------------------------------------------------------ */

Page.prototype.setCookie = function(/* ...cookies or array */) {
    var args = Array.prototype.slice.call(arguments);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var cookies = (args.length === 1 && Array.isArray(args[0])) ? args[0] : args;
    return this._call("Network.setCookies", {cookies: cookies}, null, cb);
};

Page.prototype.cookies = function(/* ...urls */) {
    var args = Array.prototype.slice.call(arguments);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var urls = args.length ? args : undefined;
    function unwrap(r) { return r ? (r.cookies || []) : []; }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _callCdp(this._channel, "Network.getCookies",
        urls ? {urls: urls} : {}, this._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Page.prototype.deleteCookie = function(/* ...cookies */) {
    var args = Array.prototype.slice.call(arguments);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var self = this;
    /* Network.deleteCookies takes one at a time; chain them. */
    function doOne(i, out) {
        if (i >= args.length) { out(null, null); return; }
        var c = args[i];
        _callCdp(self._channel, "Network.deleteCookies", c, self._sessionId, null, null,
            function(_, err) {
                if (err) return out(null, err);
                doOne(i+1, out);
            });
    }
    if (cb) { doOne(0, cb); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doOne(0, function(_, err) { err ? rej(err) : res(); });
        });
    }
    /* sync mode: just do each sync */
    for (var i = 0; i < args.length; i++) {
        this._call("Network.deleteCookies", args[i], null, null);
    }
};

/* ------------------------------------------------------------------ *
 *  exposeFunction — make a Rampart function callable from page JS as
 *  window.<name>(...args), returning a Promise.
 *
 *  Pipeline:
 *    1. Runtime.addBinding registers window.<name> as a stub that takes
 *       a single string and fires Runtime.bindingCalled.
 *    2. Page.addScriptToEvaluateOnNewDocument wraps the stub with a JSON
 *       envelope + a pending-call map so the page side can await replies.
 *       We also run the same wrapper in the current document so the
 *       binding is usable immediately.
 *    3. Worker forwards Runtime.bindingCalled events to the main thread.
 *    4. Main runs the user fn, ships the result back via Runtime.evaluate
 *       calling the page-side dispatcher window.__cvReply_<name>.
 *
 *  Main's event loop must be running for the user fn to fire (it will
 *  not be during sync blocking calls from top-level script).
 * ------------------------------------------------------------------ */

Page.prototype.exposeFunction = function(name, fn, cb) {
    if (!/^[a-zA-Z_$][a-zA-Z0-9_$]*$/.test(name))
        throw new Error("exposeFunction: invalid name: " + name);

    var self = this;
    var bindingInit =
        "(function(){"
        + "  var n = " + JSON.stringify(name) + ";"
        + "  var stub = window[n];"                   /* the addBinding stub */
        + "  if (typeof stub !== 'function') return;"
        + "  if (window['__cv_wrapped_' + n]) return;" /* idempotent */
        + "  window['__cv_wrapped_' + n] = true;"
        + "  var pending = new Map();"
        + "  var seq = 0;"
        + "  window[n] = function() {"
        + "    var args = Array.prototype.slice.call(arguments);"
        + "    return new Promise(function(res, rej) {"
        + "      var id = ++seq;"
        + "      pending.set(id, {res: res, rej: rej});"
        + "      stub(JSON.stringify({id: id, args: args}));"
        + "    });"
        + "  };"
        + "  window['__cvReply_' + n] = function(id, ok, val) {"
        + "    var p = pending.get(id);"
        + "    if (!p) return;"
        + "    pending.delete(id);"
        + "    ok ? p.res(val) : p.rej(new Error(val));"
        + "  };"
        + "})()";

    /* Register a main-side event handler for binding calls. */
    var evName  = self._channel + ".ev." + self._sessionId + ".Runtime.bindingCalled";
    var fnName  = "exposeFn-" + self._sessionId + "-" + name + "-" + (_tokenSeq++);
    rampart.event.on(evName, fnName, function(uv, payload) {
        var p = payload.params || {};
        if (p.name !== name) return;
        var env;
        try { env = JSON.parse(p.payload); } catch(e) { return; }
        var reply = function(ok, val) {
            var js = "window.__cvReply_" + name + "("
                   + env.id + ", " + (ok ? "true" : "false") + ", "
                   + JSON.stringify(val) + ")";
            /* fire-and-forget callback form */
            _callCdp(self._channel, "Runtime.evaluate",
                {expression: js, returnByValue: true},
                self._sessionId, null, null, function(){});
        };
        try {
            var r = fn.apply(null, env.args);
            if (r && typeof r.then === "function") {
                r.then(function(v) { reply(true, v); },
                       function(e) { reply(false, String(e && e.message || e)); });
            } else {
                reply(true, r);
            }
        } catch (e) {
            reply(false, String(e && e.message || e));
        }
    });
    /* Remember so future close() can clean up if we add that. */
    if (!self._exposedBindings) self._exposedBindings = [];
    self._exposedBindings.push({name: name, evName: evName, fnName: fnName});

    /* Three CDP calls in sequence: addBinding, install-on-new-doc, and
     * install-now.  Branch on calling convention. */
    var sid = self._sessionId;
    var ch  = self._channel;
    function step1(next)      { _callCdp(ch, "Runtime.addBinding",               {name: name},            sid, null, null, next); }
    function step2(_, err, next) {
        if (err) return next(null, err);
        _callCdp(ch, "Page.addScriptToEvaluateOnNewDocument", {source: bindingInit}, sid, null, null, next);
    }
    function step3(_, err, next) {
        if (err) return next(null, err);
        _callCdp(ch, "Runtime.evaluate", {expression: bindingInit, returnByValue: true}, sid, null, null, next);
    }

    if (typeof cb === "function") {
        step1(function(_, e1) {
            step2(_, e1, function(_, e2) {
                step3(_, e2, function(_, e3) { cb(null, e3); });
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.exposeFunction(name, fn, function(_, err) { err ? rej(err) : res(); });
        });
    }
    /* sync */
    _callCdp(ch, "Runtime.addBinding", {name: name}, sid, null, null, null);
    _callCdp(ch, "Page.addScriptToEvaluateOnNewDocument", {source: bindingInit}, sid, null, null, null);
    _callCdp(ch, "Runtime.evaluate", {expression: bindingInit, returnByValue: true}, sid, null, null, null);
};

/* ------------------------------------------------------------------ *
 *  exposeBinding — newer than exposeFunction.  The page-side wrapper
 *  invokes the rampart fn with a "source" descriptor as its first
 *  arg: `{page, frame}`.  With `{handle: true}`, the second user arg
 *  arrives as a JSHandle wrapping the page-side value (instead of
 *  the JSON-roundtripped value that exposeFunction returns).  The
 *  handle path stashes the raw value on the page in a per-binding
 *  Map, then materializes it back to main as a remote-object handle
 *  via Runtime.callFunctionOn.
 * ------------------------------------------------------------------ */

Page.prototype.exposeBinding = function(name, fn, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var handleMode = !!opts.handle;

    if (!/^[a-zA-Z_$][a-zA-Z0-9_$]*$/.test(name))
        throw new Error("exposeBinding: invalid name: " + name);

    var self = this;

    var bindingInit;
    if (handleMode) {
        /* Stash the single arg in a per-binding pageMap, send the
           seq id (a string) through the binding stub.  Main pulls
           the arg back via Runtime.callFunctionOn keyed by seq. */
        bindingInit =
            "(function(){"
            + "  var n = " + JSON.stringify(name) + ";"
            + "  var stub = window[n];"
            + "  if (typeof stub !== 'function') return;"
            + "  if (window['__cv_wrapped_' + n]) return;"
            + "  window['__cv_wrapped_' + n] = true;"
            + "  var pending = new Map();"
            + "  var store   = new Map();"
            + "  window['__cvStore_' + n] = store;"
            + "  var seq = 0;"
            + "  window[n] = function(arg) {"
            + "    return new Promise(function(res, rej) {"
            + "      var id = ++seq;"
            + "      store.set(id, arg);"
            + "      pending.set(id, {res: res, rej: rej});"
            + "      stub(JSON.stringify({id: id}));"
            + "    });"
            + "  };"
            + "  window['__cvReply_' + n] = function(id, ok, val) {"
            + "    var p = pending.get(id);"
            + "    if (!p) return;"
            + "    pending['delete'](id);"
            + "    store['delete'](id);"
            + "    ok ? p.res(val) : p.rej(new Error(val));"
            + "  };"
            + "})()";
    } else {
        bindingInit =
            "(function(){"
            + "  var n = " + JSON.stringify(name) + ";"
            + "  var stub = window[n];"
            + "  if (typeof stub !== 'function') return;"
            + "  if (window['__cv_wrapped_' + n]) return;"
            + "  window['__cv_wrapped_' + n] = true;"
            + "  var pending = new Map();"
            + "  var seq = 0;"
            + "  window[n] = function() {"
            + "    var args = Array.prototype.slice.call(arguments);"
            + "    return new Promise(function(res, rej) {"
            + "      var id = ++seq;"
            + "      pending.set(id, {res: res, rej: rej});"
            + "      stub(JSON.stringify({id: id, args: args}));"
            + "    });"
            + "  };"
            + "  window['__cvReply_' + n] = function(id, ok, val) {"
            + "    var p = pending.get(id);"
            + "    if (!p) return;"
            + "    pending['delete'](id);"
            + "    ok ? p.res(val) : p.rej(new Error(val));"
            + "  };"
            + "})()";
    }

    var sid = self._sessionId;
    var ch  = self._channel;

    /* Source object passed as the user fn's first arg.  Best-effort
       frame resolution — we don't have a main-side ctxId→frame map,
       so default to the page's main frame; users who need the actual
       sub-frame can branch on payload.executionContextId, also
       passed through. */
    function sourceFor(payload) {
        /* Construct a Frame from the cached main-frame id synchronously
           (avoids an async `mainFrame()` roundtrip per binding call).
           Sub-frame discrimination via `payload.executionContextId` is
           left to the user — we don't keep a ctxId→frame map on main. */
        var frame = self._mainFrameId
                  ? new Frame(self, self._mainFrameId, null, "")
                  : null;
        return {
            page:                 self,
            frame:                frame,
            executionContextId:   payload.executionContextId
        };
    }

    var evName  = ch + ".ev." + sid + ".Runtime.bindingCalled";
    var fnName  = "exposeBind-" + sid + "-" + name + "-" + (_tokenSeq++);
    rampart.event.on(evName, fnName, function(uv, payload) {
        var p = payload.params || {};
        if (p.name !== name) return;
        var env;
        try { env = JSON.parse(p.payload); } catch(e) { return; }
        var reply = function(ok, val) {
            var js = "window.__cvReply_" + name + "("
                   + env.id + ", " + (ok ? "true" : "false") + ", "
                   + JSON.stringify(val) + ")";
            _callCdp(ch, "Runtime.evaluate",
                {expression: js, returnByValue: true},
                sid, null, null, function(){});
        };
        var source = sourceFor(p);
        if (handleMode) {
            /* Pull the stashed arg back as a remote object — wrap as
               JSHandle.  callFunctionOn with returnByValue:false
               keeps object identity; the resulting objectId is what
               our JSHandle wraps. */
            var pickFn = "function(){ return window['__cvStore_" + name + "'].get(" + env.id + "); }";
            _callCdp(ch, "Runtime.callFunctionOn",
                {
                    functionDeclaration: pickFn,
                    executionContextId:  p.executionContextId,
                    returnByValue:       false
                },
                sid, null, null, function(r, err) {
                    if (err) return reply(false, "exposeBinding(handle): "
                        + (err.message || String(err)));
                    var ro = r && r.result;
                    var jshandle = _wrapRemote(self, ro);
                    var res;
                    try {
                        res = fn(source, jshandle);
                    } catch (e) {
                        return reply(false, String(e && e.message || e));
                    }
                    if (res && typeof res.then === "function")
                        res.then(function(v){ reply(true, v); },
                                 function(e){ reply(false, String(e && e.message || e)); });
                    else
                        reply(true, res);
                });
            return;
        }
        /* Non-handle mode: args are JSON-roundtripped. */
        try {
            var args = [source].concat(env.args || []);
            var r2 = fn.apply(null, args);
            if (r2 && typeof r2.then === "function")
                r2.then(function(v){ reply(true, v); },
                        function(e){ reply(false, String(e && e.message || e)); });
            else
                reply(true, r2);
        } catch (e) {
            reply(false, String(e && e.message || e));
        }
    });
    if (!self._exposedBindings) self._exposedBindings = [];
    self._exposedBindings.push({name: name, evName: evName, fnName: fnName});

    function step1(next)         { _callCdp(ch, "Runtime.addBinding",                  {name: name},            sid, null, null, next); }
    function step2(_, err, next) { if (err) return next(null, err);
        _callCdp(ch, "Page.addScriptToEvaluateOnNewDocument", {source: bindingInit}, sid, null, null, next); }
    function step3(_, err, next) { if (err) return next(null, err);
        _callCdp(ch, "Runtime.evaluate", {expression: bindingInit, returnByValue: true}, sid, null, null, next); }

    if (typeof cb === "function") {
        step1(function(_, e1) {
            step2(_, e1, function(_, e2) {
                step3(_, e2, function(_, e3) { cb(null, e3); });
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.exposeBinding(name, fn, opts, function(_, err) { err ? rej(err) : res(); });
        });
    }
    _callCdp(ch, "Runtime.addBinding",                  {name: name},                                        sid, null, null, null);
    _callCdp(ch, "Page.addScriptToEvaluateOnNewDocument", {source: bindingInit},                             sid, null, null, null);
    _callCdp(ch, "Runtime.evaluate",                    {expression: bindingInit, returnByValue: true},      sid, null, null, null);
};

/* ------------------------------------------------------------------ *
 *  Helper: one CDP call + post-process, honoring all three modes.
 * ------------------------------------------------------------------ */

function _callCdpUnwrap(page, method, params, unwrap, cb) {
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _callCdp(page._channel, method, params, page._sessionId,
                       null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
}

/* ------------------------------------------------------------------ *
 *  Frames
 *
 *  Each Frame is identified by a (sessionId, frameId) pair.  The worker
 *  tracks frameId -> contextId from Runtime.executionContextCreated, and
 *  Frame.evaluate sends via the "frameEval" proc which looks up the
 *  contextId at send time.  ElementHandles created inside a frame are
 *  interchangeable with page-level handles (they're just remote objects).
 * ------------------------------------------------------------------ */

function Frame(page, frameId, parentFrame, url) {
    this._page        = page;
    this._channel     = page._channel;
    this._sessionId   = page._sessionId;
    this._frameId     = frameId;
    this._parentFrame = parentFrame || null;
    this._url         = url || "";
    this._childFrames = [];
}

Frame.prototype.url        = function() { return this._url; };
Frame.prototype.name       = function() { return this._name || ""; };
Frame.prototype.frameId    = function() { return this._frameId; };
Frame.prototype.parentFrame= function() { return this._parentFrame; };
Frame.prototype.childFrames= function() { return this._childFrames.slice(); };
Frame.prototype.isDetached = function() { return this._detached === true; };
Frame.prototype.page       = function() { return this._page; };

Frame.prototype.evaluate = function(srcOrFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var src = _toExprSource(srcOrFn, args);
    var self = this;
    function unwrap(r) {
        if (!r) return undefined;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "frame.evaluate failed");
        }
        return r.result ? r.result.value : undefined;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _sendProc(self._channel, "frameEval", {
        sessionId:  self._sessionId,
        frameId:    self._frameId,
        expression: src
    }, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Frame.prototype.content  = function(cb) { return this.evaluate("document.documentElement.outerHTML", cb); };
Frame.prototype.title    = function(cb) { return this.evaluate("document.title",  cb); };

Frame.prototype.$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "frame.$eval page function");
    var src = "(function(){"
            + "  var el=document.querySelector(" + JSON.stringify(selector) + ");"
            + "  if(!el) throw new Error('$eval: no element matched ' + " + JSON.stringify(selector) + ");"
            + "  return (" + fnSrc + ").apply(null,[el].concat(" + JSON.stringify(args) + "));"
            + "})()";
    return this.evaluate(src, cb);
};

Frame.prototype.$$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "frame.$$eval page function");
    var src = "(function(){"
            + "  var els=Array.from(document.querySelectorAll(" + JSON.stringify(selector) + "));"
            + "  return (" + fnSrc + ").apply(null,[els].concat(" + JSON.stringify(args) + "));"
            + "})()";
    return this.evaluate(src, cb);
};

Frame.prototype.waitForSelector = function(selector, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this._page, opts, false);
    var polling = opts.polling || 50;
    var src = "new Promise(function(resolve,reject){"
            + "var sel=" + JSON.stringify(selector) + ";"
            + "var start=Date.now(),t=" + (+timeout) + ",poll=" + (+polling) + ";"
            + "(function c(){"
            + "var el=document.querySelector(sel);"
            + "if(el)return resolve(true);"
            + "if(t>0&&Date.now()-start>t)return reject(new Error('waitForSelector: timeout'));"
            + "setTimeout(c,poll);})();})";
    return this.evaluate(src, cb);
};

/* Build a Page.frames() array from the worker's frame tree. */
function _buildFrames(page, tree, parent, out) {
    var info = tree.frame;
    var f = new Frame(page, info.id, parent, info.url);
    f._name = info.name || "";
    out.push(f);
    if (tree.childFrames) {
        for (var i = 0; i < tree.childFrames.length; i++) {
            var child = _buildFrames(page, tree.childFrames[i], f, out);
            f._childFrames.push(child);
        }
    }
    return f;
}

Page.prototype.frames = function(cb) {
    var self = this;
    function unwrap(r) {
        var out = [];
        if (r && r.tree) _buildFrames(self, r.tree, null, out);
        return out;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _sendProc(self._channel, "getFrames", {sessionId: self._sessionId}, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Page.prototype.mainFrame = function(cb) {
    function pickMain(arr) { return arr.length ? arr[0] : null; }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(pickMain(r), null);
    } : null;
    var arr = this.frames(cbw);
    if (cbw) return;
    if (arr && typeof arr.then === "function") return arr.then(pickMain);
    return pickMain(arr);
};

/* ------------------------------------------------------------------ *
 *  Input: Mouse and Keyboard via CDP's Input domain.
 *
 *  These dispatch real mouse/key events (vs. the synthetic DOM events
 *  used by page.click/page.type's JS fallback).  Needed for sites that
 *  listen for mousedown/keydown rather than click/input.
 * ------------------------------------------------------------------ */

function Mouse(page) {
    this._page = page;
    this._x = 0;
    this._y = 0;
    this._buttons = 0;   /* CDP mouse button modifier bitmask */
}

Mouse.prototype.move = function(x, y, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = undefined; }
    this._x = x; this._y = y;
    return this._page._call("Input.dispatchMouseEvent", {
        type: "mouseMoved", x: x, y: y, button: "none",
        buttons: this._buttons
    }, null, cb);
};

Mouse.prototype.down = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var button = opts.button || "left";
    this._buttons |= _mouseBtnMask(button);
    return this._page._call("Input.dispatchMouseEvent", {
        type: "mousePressed", x: this._x, y: this._y,
        button: button, buttons: this._buttons,
        clickCount: opts.clickCount || 1
    }, null, cb);
};

Mouse.prototype.up = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var button = opts.button || "left";
    this._buttons &= ~_mouseBtnMask(button);
    return this._page._call("Input.dispatchMouseEvent", {
        type: "mouseReleased", x: this._x, y: this._y,
        button: button, buttons: this._buttons,
        clickCount: opts.clickCount || 1
    }, null, cb);
};

Mouse.prototype.wheel = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    return this._page._call("Input.dispatchMouseEvent", {
        type: "mouseWheel", x: this._x, y: this._y,
        button: "none", buttons: this._buttons,
        deltaX: opts.deltaX || 0, deltaY: opts.deltaY || 0
    }, null, cb);
};

/* Multi-step click: move, down, delay, up. */
Mouse.prototype.click = function(x, y, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    var delay = opts.delay || 0;
    var clickCount = opts.clickCount || 1;
    var button = opts.button || "left";

    function doChain(finish) {
        self.move(x, y, function(_, e1) {
            if (e1) return finish(e1);
            self.down({button: button, clickCount: clickCount}, function(_, e2) {
                if (e2) return finish(e2);
                function afterDelay() {
                    self.up({button: button, clickCount: clickCount}, function(_, e3) {
                        finish(e3);
                    });
                }
                if (delay > 0) setTimeout(afterDelay, delay);
                else           afterDelay();
            });
        });
    }
    if (typeof cb === "function") {
        doChain(function(err) { cb(null, err); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err) { err ? rej(err) : res(); });
        });
    }
    /* sync — delay via rampart.utils.sleep */
    this.move(x, y);
    this.down({button: button, clickCount: clickCount});
    if (delay > 0) utils.sleep(delay / 1000);
    this.up({button: button, clickCount: clickCount});
};

function _mouseBtnMask(name) {
    return name === "left"   ? 1
         : name === "right"  ? 2
         : name === "middle" ? 4
         : 0;
}

/* ---------------- Keyboard ---------------- */

function Keyboard(page) {
    this._page = page;
    this._modifiers = 0;     /* alt=1, ctrl=2, meta=4, shift=8 */
}

var _keyDefs = {
    "Enter":    {code:"Enter", key:"Enter", kc:13, text:"\r"},
    "Tab":      {code:"Tab", key:"Tab", kc:9, text:"\t"},
    "Backspace":{code:"Backspace", key:"Backspace", kc:8},
    "Delete":   {code:"Delete", key:"Delete", kc:46},
    "Escape":   {code:"Escape", key:"Escape", kc:27},
    "ArrowLeft":{code:"ArrowLeft", key:"ArrowLeft", kc:37},
    "ArrowRight":{code:"ArrowRight", key:"ArrowRight", kc:39},
    "ArrowUp":  {code:"ArrowUp", key:"ArrowUp", kc:38},
    "ArrowDown":{code:"ArrowDown", key:"ArrowDown", kc:40},
    "Home":     {code:"Home", key:"Home", kc:36},
    "End":      {code:"End", key:"End", kc:35},
    "PageUp":   {code:"PageUp", key:"PageUp", kc:33},
    "PageDown": {code:"PageDown", key:"PageDown", kc:34},
    "Space":    {code:"Space", key:" ", kc:32, text:" "},
    "Shift":    {code:"ShiftLeft", key:"Shift", kc:16, mod:8},
    "Control":  {code:"ControlLeft", key:"Control", kc:17, mod:2},
    "Alt":      {code:"AltLeft", key:"Alt", kc:18, mod:1},
    "Meta":     {code:"MetaLeft", key:"Meta", kc:91, mod:4}
};

function _resolveKey(key) {
    if (_keyDefs[key]) return _keyDefs[key];
    if (typeof key === "string" && key.length === 1) {
        var upper = key.toUpperCase();
        var kc = upper.charCodeAt(0);
        return {code: /[A-Z]/.test(upper) ? "Key" + upper
                    : /[0-9]/.test(upper) ? "Digit" + upper
                    : "Unidentified",
                key: key, kc: kc, text: key};
    }
    throw new Error("rampart-chromeview: unknown key: " + key);
}

Keyboard.prototype.down = function(key, cb) {
    var def = _resolveKey(key);
    if (def.mod) this._modifiers |= def.mod;
    return this._page._call("Input.dispatchKeyEvent", {
        type:                  def.text ? "keyDown" : "rawKeyDown",
        modifiers:             this._modifiers,
        windowsVirtualKeyCode: def.kc,
        code:                  def.code,
        key:                   def.key,
        text:                  def.text || ""
    }, null, cb);
};

Keyboard.prototype.up = function(key, cb) {
    var def = _resolveKey(key);
    if (def.mod) this._modifiers &= ~def.mod;
    return this._page._call("Input.dispatchKeyEvent", {
        type:                  "keyUp",
        modifiers:             this._modifiers,
        windowsVirtualKeyCode: def.kc,
        code:                  def.code,
        key:                   def.key
    }, null, cb);
};

Keyboard.prototype.press = function(key, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var delay = opts.delay || 0;
    var self = this;
    function doChain(finish) {
        self.down(key, function(_, e1) {
            if (e1) return finish(e1);
            function goUp() { self.up(key, function(_, e2) { finish(e2); }); }
            if (delay > 0) setTimeout(goUp, delay);
            else           goUp();
        });
    }
    if (typeof cb === "function") {
        doChain(function(err){ cb(null, err); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    this.down(key);
    if (delay > 0) utils.sleep(delay / 1000);
    this.up(key);
};

Keyboard.prototype.sendCharacter = function(ch, cb) {
    return this._page._call("Input.insertText", {text: ch}, null, cb);
};

Keyboard.prototype.type = function(text, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var delay = opts.delay || 0;
    var self = this;
    /* Short path: no delay, whole string at once via insertText */
    if (!delay) {
        return this._page._call("Input.insertText", {text: text}, null, cb);
    }
    /* Char-by-char so delays interleave */
    var chars = text.split("");
    function doChain(finish) {
        var i = 0;
        function next() {
            if (i >= chars.length) return finish(null);
            self._page._call("Input.insertText", {text: chars[i++]}, null,
                function(_, err) {
                    if (err) return finish(err);
                    setTimeout(next, delay);
                });
        }
        next();
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    for (var i = 0; i < chars.length; i++) {
        this._page._call("Input.insertText", {text: chars[i]}, null, null);
        if (delay > 0) utils.sleep(delay / 1000);
    }
};

/* Lazy accessors on Page */
Object.defineProperty(Page.prototype, "mouse", {
    get: function() { return this._mouse || (this._mouse = new Mouse(this)); }
});
Object.defineProperty(Page.prototype, "keyboard", {
    get: function() { return this._keyboard || (this._keyboard = new Keyboard(this)); }
});

/* ------------------------------------------------------------------ *
 *  Request interception via CDP Fetch domain.
 *
 *  After page.setRequestInterception(true), every request pauses; the
 *  user handler registered with page.on("request", fn) must call
 *  req.continue() / req.abort() / req.respond() to resume.
 * ------------------------------------------------------------------ */

function Request(page, params) {
    this._page         = page;
    this._channel      = page._channel;
    this._sessionId    = page._sessionId;
    this._requestId    = params.requestId;
    this._request      = params.request || {};
    this._resourceType = params.resourceType;
    this._frameId      = params.frameId;
    this._networkId    = params.networkId;
    this._responseStatusCode = params.responseStatusCode;
    this._responseHeaders    = params.responseHeaders;
    this._handled      = false;
}

Request.prototype.url          = function() { return this._request.url; };
Request.prototype.method       = function() { return this._request.method; };
Request.prototype.headers      = function() { return this._request.headers || {}; };
Request.prototype.postData     = function() { return this._request.postData; };
Request.prototype.resourceType = function() { return this._resourceType; };
Request.prototype.response     = function() { return this._response || null; };
Request.prototype.failure      = function() { return this._failure || null; };
Request.prototype.frame        = function() {
    var self = this;
    var frames = this._page.frames();
    if (frames && typeof frames.then === "function") {
        return frames.then(function(fs) {
            for (var i = 0; i < fs.length; i++)
                if (fs[i]._frameId === self._frameId) return fs[i];
            return null;
        });
    }
    for (var i = 0; i < frames.length; i++)
        if (frames[i]._frameId === this._frameId) return frames[i];
    return null;
};

function _headersToArr(obj) {
    var arr = [];
    if (!obj) return arr;
    for (var k in obj) arr.push({name: k, value: String(obj[k])});
    return arr;
}

Request.prototype.continue = function(overrides, cb) {
    if (typeof overrides === "function") { cb = overrides; overrides = null; }
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    var params = {requestId: this._requestId};
    if (overrides) {
        if (overrides.url)      params.url = overrides.url;
        if (overrides.method)   params.method = overrides.method;
        if (overrides.postData) params.postData = overrides.postData;
        if (overrides.headers)  params.headers = _headersToArr(overrides.headers);
    }
    return _callCdp(this._channel, "Fetch.continueRequest",
        params, this._sessionId, null, null, cb);
};

Request.prototype.abort = function(errorReason, cb) {
    if (typeof errorReason === "function") { cb = errorReason; errorReason = null; }
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    return _callCdp(this._channel, "Fetch.failRequest",
        {requestId: this._requestId, errorReason: errorReason || "Failed"},
        this._sessionId, null, null, cb);
};

Request.prototype.respond = function(response, cb) {
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    var body = response.body;
    /* Fetch.fulfillRequest wants base64-encoded body as a string. */
    if (body !== undefined && body !== null) {
        body = sprintf("%B", body);
    }
    var params = {
        requestId:       this._requestId,
        responseCode:    response.status || 200,
        responseHeaders: _headersToArr(response.headers),
        body:            body
    };
    return _callCdp(this._channel, "Fetch.fulfillRequest",
        params, this._sessionId, null, null, cb);
};

Page.prototype.setRequestInterception = function(enabled, cb) {
    this._interceptionOn = !!enabled;
    if (enabled) {
        return this._call("Fetch.enable",
            {patterns: [{urlPattern: "*"}]}, null, cb);
    }
    return this._call("Fetch.disable", {}, null, cb);
};

/* ------------------------------------------------------------------ *
 *  Response (Puppeteer: the observer-side object for a fetched
 *  resource).  Passed to page.on("response", fn) and returned from
 *  page.waitForResponse().
 * ------------------------------------------------------------------ */

function Response(page, params) {
    this._page      = page;
    this._channel   = page._channel;
    this._sessionId = page._sessionId;
    this._requestId = params.requestId;
    this._response  = params.response || {};
}

Response.prototype.url         = function() { return this._response.url; };
Response.prototype.status      = function() { return this._response.status; };
Response.prototype.statusText  = function() { return this._response.statusText; };
Response.prototype.headers     = function() { return this._response.headers || {}; };
Response.prototype.mimeType    = function() { return this._response.mimeType; };
Response.prototype.request     = function() { return this._request || null; };
Response.prototype.ok          = function() {
    var s = this._response.status;
    return s === 0 || (s >= 200 && s <= 299);
};

Response.prototype.buffer = function(cb) {
    function unwrap(r) {
        if (!r) return null;
        if (r.base64Encoded) return utils.bprintf("%!B", r.body);
        return Buffer.from(r.body || "");
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _callCdp(this._channel, "Network.getResponseBody",
        {requestId: this._requestId}, this._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Response.prototype.text = function(cb) {
    function unwrap(r) {
        if (!r) return "";
        if (r.base64Encoded) return sprintf("%!B", r.body);
        return r.body || "";
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _callCdp(this._channel, "Network.getResponseBody",
        {requestId: this._requestId}, this._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Response.prototype.json = function(cb) {
    if (cb) {
        this.text(function(t, err) {
            if (err) return cb(null, err);
            try { cb(JSON.parse(t), null); } catch(e) { cb(null, e); }
        });
        return;
    }
    var t = this.text();
    if (t && typeof t.then === "function") return t.then(function(s) { return JSON.parse(s); });
    return JSON.parse(t);
};

Page.prototype.waitForResponse = function(urlMatch, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    function unwrap(r) {
        if (!r) return null;
        var rid = r.requestId;
        var resp = self._respByNetId[rid];
        if (!resp) {
            resp = new Response(self, r);
            self._respByNetId[rid] = resp;
        }
        var req = self._reqByNetId[rid];
        if (req && !resp._request) { resp._request = req; req._response = resp; }
        return resp;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _sendProc(this._channel, "waitForResponse", {
        sessionId: this._sessionId,
        urlMatch:  urlMatch
    }, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* ------------------------------------------------------------------ *
 *  Dialog (alert / confirm / prompt / beforeunload).  Passed to
 *  page.on("dialog", fn).  The handler MUST call .accept() or
 *  .dismiss() or the page hangs.
 * ------------------------------------------------------------------ */

function Dialog(page, params) {
    this._page         = page;
    this._channel      = page._channel;
    this._sessionId    = page._sessionId;
    this._type         = params.type;
    this._message      = params.message;
    this._defaultPrompt= params.defaultPrompt || "";
    this._handled      = false;
}

Dialog.prototype.type         = function() { return this._type; };
Dialog.prototype.message      = function() { return this._message; };
Dialog.prototype.defaultValue = function() { return this._defaultPrompt; };

Dialog.prototype.accept = function(promptText, cb) {
    if (typeof promptText === "function") { cb = promptText; promptText = undefined; }
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    var params = {accept: true};
    if (promptText !== undefined) params.promptText = promptText;
    return _callCdp(this._channel, "Page.handleJavaScriptDialog",
        params, this._sessionId, null, null, cb);
};

Dialog.prototype.dismiss = function(cb) {
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    return _callCdp(this._channel, "Page.handleJavaScriptDialog",
        {accept: false}, this._sessionId, null, null, cb);
};

/* ------------------------------------------------------------------ *
 *  page.waitForNavigation — block until the next Page.loadEventFired
 *  (or domcontentloaded) on this session.  Call AFTER triggering the
 *  navigation (click, form submit, etc.); there's a small race window
 *  between the trigger and the waiter being installed, which is
 *  usually fine because the network round-trip takes longer.
 * ------------------------------------------------------------------ */

Page.prototype.waitForNavigation = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var evMap = {
        "load":             "Page.loadEventFired",
        "domcontentloaded": "Page.domContentEventFired"
    };
    var evMethod = evMap[opts.waitUntil || "load"] || "Page.loadEventFired";
    return _sendProc(this._channel, "waitForEvent", {
        evMethod:  evMethod,
        evSession: this._sessionId
    }, cb);
};

/* ------------------------------------------------------------------ *
 *  page.authenticate — provide HTTP Basic/Digest credentials.  Uses
 *  Fetch.enable with handleAuthRequests so chrome routes
 *  401/407 challenges back to us as Fetch.authRequired events.
 * ------------------------------------------------------------------ */

Page.prototype.authenticate = function(creds, cb) {
    var self = this;
    this._authCreds = creds;
    if (!this._authHandlerInstalled) {
        this._authHandlerInstalled = true;
        var evName = this._channel + ".ev." + this._sessionId + ".Fetch.authRequired";
        rampart.event.on(evName, "auth-" + this._sessionId + "-" + (_tokenSeq++),
            function(uv, payload) {
                var p = payload.params || {};
                var c = self._authCreds;
                var resp = c
                    ? {response: "ProvideCredentials",
                       username: c.username, password: c.password}
                    : {response: "CancelAuth"};
                _callCdp(self._channel, "Fetch.continueWithAuth",
                    {requestId: p.requestId, authChallengeResponse: resp},
                    self._sessionId, null, null, function(){});
            });
        /* Also need to let the paused non-auth requests through. */
        rampart.event.on(this._channel + ".ev." + this._sessionId + ".Fetch.requestPaused",
            "auth-req-" + this._sessionId + "-" + (_tokenSeq++),
            function(uv, payload) {
                if (self._interceptionOn) return; /* user handler will deal */
                var p = payload.params || {};
                _callCdp(self._channel, "Fetch.continueRequest",
                    {requestId: p.requestId}, self._sessionId, null, null, function(){});
            });
    }
    if (creds) {
        return this._call("Fetch.enable",
            {patterns: [{urlPattern: "*"}], handleAuthRequests: true}, null, cb);
    }
    if (cb) { cb(null, null); return; }
    if (_isTranspiled()) return Promise.resolve();
};

/* ------------------------------------------------------------------ *
 *  ElementHandle.screenshot — clip page.screenshot to the element's
 *  bounding box.
 * ------------------------------------------------------------------ */

ElementHandle.prototype.screenshot = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;

    function withBox(box, userOpts) {
        if (!box) throw new Error("ElementHandle.screenshot: element has no bounding box");
        var so = {};
        for (var k in userOpts) so[k] = userOpts[k];
        so.clip = {x: box.x, y: box.y, width: box.width, height: box.height, scale: 1};
        return so;
    }

    if (typeof cb === "function") {
        self.boundingBox(function(box, err) {
            if (err) return cb(null, err);
            if (!box) return cb(null, new Error("element has no bounding box"));
            self._page.screenshot(withBox(box, opts), cb);
        });
        return;
    }
    if (_isTranspiled()) {
        return self.boundingBox().then(function(box) {
            if (!box) throw new Error("element has no bounding box");
            return self._page.screenshot(withBox(box, opts));
        });
    }
    var box = self.boundingBox();
    return self._page.screenshot(withBox(box, opts));
};

/* ------------------------------------------------------------------ *
 *  page.select — set a <select> element's value(s).  Returns the
 *  array of values that ended up selected.
 * ------------------------------------------------------------------ */

Page.prototype.select = function(selector /* , ...values */) {
    var values = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (values.length && typeof values[values.length-1] === "function") cb = values.pop();
    /* Puppeteer's implementation: clear via element.value = "" first,
     * then toggle option.selected.  This works for both single- and
     * multi-value <select>s (setting option.selected=false alone on the
     * currently-selected option is a no-op for single-value selects). */
    return this.$eval(selector,
        "function(el, values) {"
        + "  if (el.nodeName !== 'SELECT')"
        + "    throw new Error('page.select: not a <select> element');"
        + "  el.value = undefined;"
        + "  var opts = Array.from(el.options);"
        + "  for (var i=0; i<opts.length; i++) {"
        + "    var opt = opts[i];"
        + "    opt.selected = values.indexOf(opt.value) >= 0;"
        + "    if (opt.selected && !el.multiple) break;"
        + "  }"
        + "  el.dispatchEvent(new Event('input',  {bubbles:true}));"
        + "  el.dispatchEvent(new Event('change', {bubbles:true}));"
        + "  return opts.filter(function(o){return o.selected;}).map(function(o){return o.value;});"
        + "}", values, cb);
};

/* ------------------------------------------------------------------ *
 *  XPath selectors: page.$x(expr), page.waitForXPath(expr, opts).
 * ------------------------------------------------------------------ */

function _xpathExpr(selector) {
    return "(function(){"
         + "  var r = document.evaluate(" + JSON.stringify(selector)
         + ", document, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);"
         + "  var a = [];"
         + "  for (var i=0; i<r.snapshotLength; i++) a.push(r.snapshotItem(i));"
         + "  return a;"
         + "})()";
}

Page.prototype.$x = function(xpath, cb) {
    var self = this;
    var evParams = {expression: _xpathExpr(xpath), returnByValue: false};
    if (typeof cb === "function") {
        _callCdp(self._channel, "Runtime.evaluate", evParams,
                 self._sessionId, null, null, function(r1, e1) {
            if (e1) return cb(null, e1);
            if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
            _callCdp(self._channel, "Runtime.getProperties",
                     {objectId: r1.result.objectId, ownProperties: true},
                     self._sessionId, null, null, function(r2, e2) {
                if (e2) return cb(null, e2);
                cb(_elemsFromProps(self, r2), null);
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$x(xpath, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    var r1 = _callCdp(self._channel, "Runtime.evaluate", evParams,
                      self._sessionId, null, null, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
                      {objectId: r1.result.objectId, ownProperties: true},
                      self._sessionId, null, null, null);
    return _elemsFromProps(self, r2);
};

Page.prototype.waitForXPath = function(xpath, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this, opts, false);
    var polling = opts.polling || 50;
    var src = "new Promise(function(resolve, reject) {"
            + "  var xp = " + JSON.stringify(xpath) + ";"
            + "  var start = Date.now(), t = " + (+timeout) + ", poll = " + (+polling) + ";"
            + "  (function c() {"
            + "    var r = document.evaluate(xp, document, null,"
            + "        XPathResult.FIRST_ORDERED_NODE_TYPE, null);"
            + "    if (r.singleNodeValue) return resolve(true);"
            + "    if (t > 0 && Date.now() - start > t) return reject(new Error('waitForXPath: timeout'));"
            + "    setTimeout(c, poll);"
            + "  })();"
            + "})";
    return this.evaluate(src, cb);
};

/* ------------------------------------------------------------------ *
 *  Frame DOM/interaction methods that mirror the Page equivalents,
 *  scoped to the frame's execution context.
 * ------------------------------------------------------------------ */

Frame.prototype.$ = function(selector, cb) {
    var self = this;
    function unwrap(r) {
        if (!r || !r.result) return null;
        var o = r.result;
        if (o.subtype === "null" || o.type === "undefined") return null;
        if (!o.objectId) return null;
        return new ElementHandle(self._page, o.objectId);
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _sendProc(self._channel, "frameEval", {
        sessionId:     self._sessionId,
        frameId:       self._frameId,
        expression:    "document.querySelector(" + JSON.stringify(selector) + ")",
        returnByValue: false
    }, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Frame.prototype.$$ = function(selector, cb) {
    var self = this;
    var expr = "Array.from(document.querySelectorAll(" + JSON.stringify(selector) + "))";
    if (typeof cb === "function") {
        _sendProc(self._channel, "frameEval",
            {sessionId:self._sessionId, frameId:self._frameId,
             expression: expr, returnByValue: false},
            function(r1, e1) {
                if (e1) return cb(null, e1);
                if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
                _callCdp(self._channel, "Runtime.getProperties",
                    {objectId: r1.result.objectId, ownProperties: true},
                    self._sessionId, null, null, function(r2, e2) {
                        if (e2) return cb(null, e2);
                        cb(_elemsFromProps(self._page, r2), null);
                    });
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$$(selector, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    var r1 = _sendProc(self._channel, "frameEval",
        {sessionId:self._sessionId, frameId:self._frameId,
         expression: expr, returnByValue: false}, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
        {objectId: r1.result.objectId, ownProperties: true},
        self._sessionId, null, null, null);
    return _elemsFromProps(self._page, r2);
};

Frame.prototype.click = function(selector, cb) {
    return this.$eval(selector,
        "function(el){el.scrollIntoView({block:'center',inline:'center'});el.click();}", cb);
};

Frame.prototype.focus = function(selector, cb) {
    return this.$eval(selector, "function(el){el.focus();}", cb);
};

Frame.prototype.hover = function(selector, cb) {
    return this.$eval(selector,
        "function(el){"
        + "el.scrollIntoView({block:'center',inline:'center'});"
        + "var r=el.getBoundingClientRect();"
        + "['mouseover','mouseenter','mousemove'].forEach(function(n){"
        + "el.dispatchEvent(new MouseEvent(n,{bubbles:true,cancelable:true,"
        + "clientX:r.left+r.width/2,clientY:r.top+r.height/2}));});}", cb);
};

Frame.prototype.type = function(selector, text, cb) {
    return this.$eval(selector,
        "function(el,t){"
        + "el.focus();"
        + "var p=Object.getPrototypeOf(el);"
        + "var d=Object.getOwnPropertyDescriptor(p,'value');"
        + "var s=d&&d.set;"
        + "if(s)s.call(el,(el.value||'')+t);else el.value=(el.value||'')+t;"
        + "el.dispatchEvent(new Event('input',{bubbles:true}));"
        + "el.dispatchEvent(new Event('change',{bubbles:true}));}", text, cb);
};

Frame.prototype.waitForFunction = function(pageFn, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this._page, opts, false);
    var polling = opts.polling || 50;
    var fnSrc = _requireSource(pageFn, "frame.waitForFunction page function");
    var src = "new Promise(function(resolve,reject){"
            + "var fn=(" + fnSrc + ");"
            + "var start=Date.now(),t=" + (+timeout) + ",poll=" + (+polling) + ";"
            + "(function c(){"
            + "try{if(fn())return resolve(true);}catch(e){return reject(e);}"
            + "if(t>0&&Date.now()-start>t)return reject(new Error('waitForFunction: timeout'));"
            + "setTimeout(c,poll);})();})";
    return this.evaluate(src, cb);
};

Frame.prototype.waitForTimeout = function(ms, cb) {
    return this.evaluate("new Promise(r => setTimeout(r, " + (+ms) + "))", cb);
};

/* ------------------------------------------------------------------ *
 *  Public entry points
 * ------------------------------------------------------------------ */

function _spinUpWorker(wsUrl) {
    var channel = _nextChannel();
    /* Restore the helper-installer that we saved at module-load time.
     * The worker-thread copy path runs whatever `_TrN_Sp.load` is
     * current as soon as the thread is created, on a wiped `_TrN_Sp`.
     * Putting our preamble's load back here means the worker comes up
     * with `_fs`, `_req`, etc. installed — which our own
     * `_workerMain` body uses transitively through the transpiler's
     * `_TrN_Sp._fs(fn, "source")` function-tag wraps. */
    if (_rchSavedTrNSpLoad && typeof _TrN_Sp !== "undefined") {
        _TrN_Sp.load = _rchSavedTrNSpLoad;
    }
    var thr = new rampart.thread(true);
    thr.exec(_workerMain, {channel: channel, wsUrl: wsUrl});
    /* Wait for the worker to signal the ws handshake is done. */
    var ready = rampart.thread.del(channel + ".ready", 15000);
    if (ready === undefined) {
        var err = rampart.thread.del(channel + ".fatal", 0);
        try { thr.close(); } catch(e) {}
        throw new Error("rampart-chromeview: failed to connect to Chrome"
                        + (err ? " ("+err+")" : ""));
    }
    return {channel: channel, thread: thr};
}

function launch(opts) {
    opts = opts || {};
    var proc = _launchChromeProc(opts);
    var w = _spinUpWorker(proc.wsEndpoint);
    var b = new Browser({
        channel: w.channel,
        thread:  w.thread,
        proc:    proc
    });
    b._wsEndpoint = proc.wsEndpoint;
    b._httpEndpoint = proc.httpEndpoint;
    return b;
}

function connect(opts) {
    opts = opts || {};
    var wsUrl = opts.browserWSEndpoint;
    if (!wsUrl && opts.browserURL) {
        var res = curl.fetch(opts.browserURL.replace(/\/$/, "") + "/json/version",
                             {maxTime: 5});
        if (res.status !== 200)
            throw new Error("failed to query " + opts.browserURL + "/json/version");
        var info = JSON.parse(res.text);
        wsUrl = info.webSocketDebuggerUrl;
    }
    if (!wsUrl)
        throw new Error("rampart-chromeview.connect: need browserWSEndpoint or browserURL");

    var w = _spinUpWorker(wsUrl);
    var b = new Browser({
        channel: w.channel,
        thread:  w.thread,
        proc:    null
    });
    b._wsEndpoint = wsUrl;
    return b;
}

/* ================================================================== *
 *  Tier 1 additions — Puppeteer parity for common scripts:
 *    devices registry, emulate, emulateMediaType, setGeolocation,
 *    evaluateOnNewDocument, addScriptTag, addStyleTag, goBack/goForward,
 *    setting toggles (offline/cache/JS/CSP, default timeouts),
 *    Browser.pages/BrowserContext.pages, ConsoleMessage,
 *    ElementHandle.contentFrame/uploadFile/select/press.
 * ================================================================== */

/* ---------------- Device descriptors ----------------
 * A small registry mirroring puppeteer.devices.  Each entry is
 * {name, userAgent, viewport: {width, height, deviceScaleFactor, isMobile,
 * hasTouch, isLandscape}}.  Use chrome.devices['iPhone 12'] etc. */
var _devices = (function() {
    var d = {};
    function add(name, ua, w, h, dpr, mobile, touch, landscape) {
        d[name] = {
            name: name,
            userAgent: ua,
            viewport: {
                width: w, height: h,
                deviceScaleFactor: dpr,
                isMobile: !!mobile,
                hasTouch: !!touch,
                isLandscape: !!landscape
            }
        };
    }
    var iosUa = "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X)"
              + " AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6"
              + " Mobile/15E148 Safari/604.1";
    var androidUa = "Mozilla/5.0 (Linux; Android 13; Pixel 7)"
                  + " AppleWebKit/537.36 (KHTML, like Gecko)"
                  + " Chrome/120.0.0.0 Mobile Safari/537.36";
    var ipadUa = "Mozilla/5.0 (iPad; CPU OS 16_6 like Mac OS X)"
               + " AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6"
               + " Mobile/15E148 Safari/604.1";
    add("iPhone SE",          iosUa,     375, 667, 2,   1, 1, 0);
    add("iPhone 12",          iosUa,     390, 844, 3,   1, 1, 0);
    add("iPhone 12 Pro",      iosUa,     390, 844, 3,   1, 1, 0);
    add("iPhone 13 Pro Max",  iosUa,     428, 926, 3,   1, 1, 0);
    add("iPhone 14",          iosUa,     390, 844, 3,   1, 1, 0);
    add("iPad",               ipadUa,    810,1080, 2,   1, 1, 0);
    add("iPad Pro",           ipadUa,   1024,1366, 2,   1, 1, 0);
    add("Pixel 5",            androidUa, 393, 851, 2.75,1, 1, 0);
    add("Pixel 7",            androidUa, 412, 915, 2.625,1, 1, 0);
    add("Galaxy S20",         androidUa, 360, 800, 3,   1, 1, 0);
    add("Galaxy S20 Ultra",   androidUa, 412, 915, 3.5, 1, 1, 0);
    return d;
})();

Page.prototype.emulate = function(device, cb) {
    if (typeof device === "string") {
        var found = _devices[device];
        if (!found) throw new Error("emulate: unknown device '" + device + "'");
        device = found;
    }
    if (!device || !device.viewport)
        throw new Error("emulate: expected {viewport, userAgent} or device name");
    var self = this;
    function doChain(finish) {
        var vp = device.viewport;
        self._call("Emulation.setDeviceMetricsOverride", {
            width: vp.width, height: vp.height,
            deviceScaleFactor: vp.deviceScaleFactor || 1,
            mobile: !!vp.isMobile,
            screenOrientation: vp.isLandscape
                ? {angle: 90, type: "landscapePrimary"}
                : {angle:  0, type: "portraitPrimary"}
        }, null, function(_, e1) {
            if (e1) return finish(e1);
            self._call("Emulation.setTouchEmulationEnabled",
                {enabled: !!vp.hasTouch}, null, function(_, e2) {
                    if (e2) return finish(e2);
                    if (!device.userAgent) return finish(null);
                    self._call("Network.setUserAgentOverride",
                        {userAgent: device.userAgent}, null, function(_, e3) {
                            finish(e3);
                        });
                });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    /* sync */
    var vp = device.viewport;
    this._call("Emulation.setDeviceMetricsOverride", {
        width: vp.width, height: vp.height,
        deviceScaleFactor: vp.deviceScaleFactor || 1,
        mobile: !!vp.isMobile,
        screenOrientation: vp.isLandscape
            ? {angle: 90, type: "landscapePrimary"}
            : {angle:  0, type: "portraitPrimary"}
    }, null);
    this._call("Emulation.setTouchEmulationEnabled",
        {enabled: !!vp.hasTouch}, null);
    if (device.userAgent)
        this._call("Network.setUserAgentOverride", {userAgent: device.userAgent}, null);
};

Page.prototype.emulateMediaType = function(type, cb) {
    if (type !== null && type !== undefined
        && type !== "screen" && type !== "print")
        throw new Error("emulateMediaType: must be 'screen', 'print', or null");
    return this._call("Emulation.setEmulatedMedia",
        {media: type === null ? "" : (type || "")}, null, cb);
};

Page.prototype.setGeolocation = function(loc, cb) {
    if (!loc) throw new Error("setGeolocation: missing {latitude, longitude}");
    var params = {
        latitude:  loc.latitude,
        longitude: loc.longitude,
        accuracy:  loc.accuracy !== undefined ? loc.accuracy : 100
    };
    return this._call("Emulation.setGeolocationOverride", params, null, cb);
};

/* ---------------- evaluateOnNewDocument ----------------
 * Runs source/fn before any page script on every new document
 * (including iframes).  Returns the identifier from CDP (handy if you
 * later want to remove it via Page.removeScriptToEvaluateOnNewDocument). */
Page.prototype.evaluateOnNewDocument = function(fnOrStr /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var src;
    if (typeof fnOrStr === "function") {
        var fnSrc = _requireSource(fnOrStr, "evaluateOnNewDocument function");
        var argsJson = args.map(function(a){ return JSON.stringify(a); }).join(",");
        src = "(" + fnSrc + ")(" + argsJson + ")";
    } else {
        src = String(fnOrStr);
    }
    function unwrap(r) { return r ? r.identifier : null; }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        cb(unwrap(r), null);
    } : null;
    var raw = _callCdp(this._channel, "Page.addScriptToEvaluateOnNewDocument",
        {source: src}, this._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* ---------------- addScriptTag / addStyleTag ----------------
 * Inject a <script> or <link>/<style> into the current document.
 * Options: {url}, {path}, or {content}.  For scripts: {type} too. */
Page.prototype.addScriptTag = function(opts, cb) {
    opts = opts || {};
    var src;
    if (opts.url) {
        src = "new Promise(function(res, rej){"
            + "  var s=document.createElement('script');"
            + "  s.src=" + JSON.stringify(opts.url) + ";"
            + (opts.type ? "  s.type=" + JSON.stringify(opts.type) + ";" : "")
            + "  s.onload=function(){res(true);};"
            + "  s.onerror=function(){rej(new Error('addScriptTag: load failed: '+" + JSON.stringify(opts.url) + "));};"
            + "  document.head.appendChild(s);"
            + "})";
    } else {
        var content = opts.content;
        if (!content && opts.path) content = utils.readFile(opts.path, true);
        if (!content) throw new Error("addScriptTag: need {url}, {path}, or {content}");
        if (opts.path)
            content += "\n//# sourceURL=" + opts.path.replace(/\n/g, '');
        src = "(function(){"
            + "  var s=document.createElement('script');"
            + (opts.type ? "  s.type=" + JSON.stringify(opts.type) + ";" : "")
            + "  s.text=" + JSON.stringify(content) + ";"
            + "  document.head.appendChild(s);"
            + "  return true;"
            + "})()";
    }
    return this.evaluate(src, cb);
};

Page.prototype.addStyleTag = function(opts, cb) {
    opts = opts || {};
    var src;
    if (opts.url) {
        src = "new Promise(function(res, rej){"
            + "  var l=document.createElement('link');"
            + "  l.rel='stylesheet';"
            + "  l.href=" + JSON.stringify(opts.url) + ";"
            + "  l.onload=function(){res(true);};"
            + "  l.onerror=function(){rej(new Error('addStyleTag: load failed: '+" + JSON.stringify(opts.url) + "));};"
            + "  document.head.appendChild(l);"
            + "})";
    } else {
        var content = opts.content;
        if (!content && opts.path) content = utils.readFile(opts.path, true);
        if (!content) throw new Error("addStyleTag: need {url}, {path}, or {content}");
        if (opts.path)
            content += "\n/*# sourceURL=" + opts.path.replace(/\*\//g, '') + " */";
        src = "(function(){"
            + "  var s=document.createElement('style');"
            + "  s.appendChild(document.createTextNode(" + JSON.stringify(content) + "));"
            + "  document.head.appendChild(s);"
            + "  return true;"
            + "})()";
    }
    return this.evaluate(src, cb);
};

/* ---------------- goBack / goForward ----------------
 * Use Page.getNavigationHistory to walk relative; navigateToHistoryEntry
 * + wait for load.  Returns null when there's no entry in that direction. */
function _historyStep(page, delta, cb) {
    var self = page;
    function unwrap(history) {
        var idx = history.currentIndex + delta;
        if (idx < 0 || idx >= history.entries.length) return null;
        var entry = history.entries[idx];
        return {entry: entry, history: history};
    }
    function step2(picked, finish) {
        if (!picked) return finish(null, null);
        _callCdp(self._channel, "Page.navigateToHistoryEntry",
            {entryId: picked.entry.id},
            self._sessionId, "Page.loadEventFired", self._sessionId, finish);
    }
    if (typeof cb === "function") {
        _callCdp(self._channel, "Page.getNavigationHistory", {}, self._sessionId,
            null, null, function(history, err) {
                if (err) return cb(null, err);
                var picked = unwrap(history);
                step2(picked, function(_, e2) {
                    if (e2) return cb(null, e2);
                    cb(picked, null);
                });
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            _historyStep(page, delta, function(picked, err) {
                err ? rej(err) : res(picked);
            });
        });
    }
    var history = _callCdp(self._channel, "Page.getNavigationHistory", {},
                           self._sessionId, null, null, null);
    var picked = unwrap(history);
    if (!picked) return null;
    _callCdp(self._channel, "Page.navigateToHistoryEntry",
        {entryId: picked.entry.id},
        self._sessionId, "Page.loadEventFired", self._sessionId, null);
    return picked;
}

Page.prototype.goBack    = function(cb) { return _historyStep(this, -1, cb); };
Page.prototype.goForward = function(cb) { return _historyStep(this, +1, cb); };

/* ---------------- Setting toggles ---------------- */

Page.prototype.setDefaultTimeout = function(ms) {
    this._defaultTimeout = +ms;
    return this;
};
Page.prototype.setDefaultNavigationTimeout = function(ms) {
    this._defaultNavigationTimeout = +ms;
    return this;
};

Page.prototype.setOfflineMode = function(offline, cb) {
    /* Network.emulateNetworkConditions: when offline=true everything fails. */
    return this._call("Network.emulateNetworkConditions", {
        offline: !!offline,
        latency: 0,
        downloadThroughput: -1,
        uploadThroughput:   -1
    }, null, cb);
};

Page.prototype.setJavaScriptEnabled = function(enabled, cb) {
    return this._call("Emulation.setScriptExecutionDisabled",
        {value: !enabled}, null, cb);
};

Page.prototype.setBypassCSP = function(enabled, cb) {
    return this._call("Page.setBypassCSP", {enabled: !!enabled}, null, cb);
};

/* Tell Chrome's network layer to route every fetch around any
   registered service worker.  Useful for tests that need to hit the
   real backend regardless of any caching/intercepting SW the site
   installed.  Wraps CDP `Network.setBypassServiceWorker` — same
   signature shape as setBypassCSP. */
Page.prototype.setBypassServiceWorker = function(bypass, cb) {
    return this._call("Network.setBypassServiceWorker",
        {bypass: !!bypass}, null, cb);
};

Page.prototype.setCacheEnabled = function(enabled, cb) {
    return this._call("Network.setCacheDisabled",
        {cacheDisabled: !enabled}, null, cb);
};

Page.prototype.viewport = function() {
    /* Returns the most recent {width, height, ...} we set via setViewport/emulate.
     * Falls back to chrome's current viewport via JS query. */
    if (this._lastViewport) return this._lastViewport;
    return this.evaluate("({width:innerWidth, height:innerHeight,"
                       + " deviceScaleFactor:devicePixelRatio || 1,"
                       + " isMobile:false, hasTouch:'ontouchstart' in window})");
};

/* Hook setViewport / emulate to record the viewport for the getter. */
(function() {
    var origSetViewport = Page.prototype.setViewport;
    Page.prototype.setViewport = function(vp, cb) {
        this._lastViewport = {
            width: vp.width, height: vp.height,
            deviceScaleFactor: vp.deviceScaleFactor || 1,
            isMobile: !!vp.isMobile,
            hasTouch: !!vp.hasTouch
        };
        return origSetViewport.call(this, vp, cb);
    };
})();

/* ---------------- Browser.pages / BrowserContext.pages ----------------
 * Enumerate all page-type targets and attach to any we don't have yet.
 * Pages we created already live in browser._pages; this surfaces popups,
 * about:blank-on-startup, etc. */
function _listPages(browser, contextFilter, cb) {
    function chooseCtx(t) {
        if (!t.browserContextId) return browser._defaultContext;
        return browser._contextById[t.browserContextId] || browser._defaultContext;
    }
    function applyFilter(targets) {
        if (!contextFilter) return targets;
        return targets.filter(function(t) {
            if (contextFilter === browser._defaultContext) {
                /* Default context: chrome assigns a real contextId to its
                 * default targets too.  Anything not belonging to a context
                 * we explicitly created is "default". */
                if (!t.browserContextId) return true;
                return !browser._contextById[t.browserContextId];
            }
            return t.browserContextId === contextFilter._contextId;
        });
    }
    function attachOne(target, finish) {
        var existing = browser._pages[target.targetId];
        if (existing) return finish(existing, null);
        _sendProc(browser._channel, "attachExisting",
            {targetId: target.targetId}, function(r, err) {
                if (err) return finish(null, err);
                var ctx = chooseCtx(target);
                var p = new Page(ctx, r.targetId, r.sessionId, r.mainFrameId);
                /* Page ctor already registered it in browser._pages */
                finish(p, null);
            });
    }
    if (typeof cb === "function") {
        _sendProc(browser._channel, "listTargets", {}, function(r, err) {
            if (err) return cb(null, err);
            var targets = applyFilter((r.targetInfos || []).filter(function(t){return t.type==="page";}));
            var out = new Array(targets.length);
            var pending = targets.length;
            if (pending === 0) return cb(out, null);
            targets.forEach(function(t, i) {
                attachOne(t, function(p, e) {
                    if (e) { cb(null, e); return; }
                    out[i] = p;
                    if (--pending === 0) cb(out, null);
                });
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            _listPages(browser, contextFilter, function(r, err) {
                err ? rej(err) : res(r);
            });
        });
    }
    /* sync */
    var r = _sendProc(browser._channel, "listTargets", {});
    var targets = applyFilter((r.targetInfos || []).filter(function(t){return t.type==="page";}));
    var out = [];
    for (var i = 0; i < targets.length; i++) {
        var t = targets[i];
        var existing = browser._pages[t.targetId];
        if (existing) { out.push(existing); continue; }
        var r2 = _sendProc(browser._channel, "attachExisting", {targetId: t.targetId});
        var ctx = chooseCtx(t);
        var p = new Page(ctx, r2.targetId, r2.sessionId, r2.mainFrameId);
        out.push(p);
    }
    return out;
}

Browser.prototype.pages = function(cb) { return _listPages(this, null, cb); };
BrowserContext.prototype.pages = function(cb) {
    return _listPages(this._browser, this, cb);
};

/* ---------------- ConsoleMessage ---------------- */

function ConsoleMessage(page, p) {
    this._page = page;
    this._type = p.type || "log";
    /* args: array of CDP RemoteObjects; expose .args() as a richer shape
     * but keep .text() as the joined value/description string. */
    this._args = p.args || [];
    this._stackTrace = p.stackTrace || null;
    this._timestamp = p.timestamp || null;
    var loc = (p.stackTrace && p.stackTrace.callFrames && p.stackTrace.callFrames[0]) || {};
    this._location = {
        url:        loc.url || "",
        lineNumber: loc.lineNumber !== undefined ? loc.lineNumber : null,
        columnNumber: loc.columnNumber !== undefined ? loc.columnNumber : null
    };
}
ConsoleMessage.prototype.type     = function() { return this._type; };
ConsoleMessage.prototype.args     = function() { return this._args.slice(); };
ConsoleMessage.prototype.location = function() { return this._location; };
ConsoleMessage.prototype.text     = function() {
    return this._args.map(function(a) {
        if (a.value !== undefined) return String(a.value);
        if (a.description !== undefined) return a.description;
        return "";
    }).join(" ");
};

/* ---------------- ElementHandle.contentFrame ----------------
 * Resolve the Frame inside an <iframe>/<frame> element.  Uses
 * DOM.describeNode to get the frameId, then matches against page.frames(). */
ElementHandle.prototype.contentFrame = function(cb) {
    var self = this;
    function findFrameId(r) {
        return r && r.node && r.node.frameId ? r.node.frameId : null;
    }
    function lookupInFrames(frameId, frames) {
        if (!frameId) return null;
        for (var i = 0; i < frames.length; i++)
            if (frames[i]._frameId === frameId) return frames[i];
        return null;
    }
    if (typeof cb === "function") {
        _callCdp(self._channel, "DOM.describeNode",
            {objectId: self._objectId}, self._sessionId, null, null,
            function(r, err) {
                if (err) return cb(null, err);
                var fid = findFrameId(r);
                if (!fid) return cb(null, null);
                self._page.frames(function(frames, e2) {
                    if (e2) return cb(null, e2);
                    cb(lookupInFrames(fid, frames), null);
                });
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.contentFrame(function(f, err) { err ? rej(err) : res(f); });
        });
    }
    var r = _callCdp(self._channel, "DOM.describeNode",
        {objectId: self._objectId}, self._sessionId, null, null, null);
    var fid = findFrameId(r);
    if (!fid) return null;
    return lookupInFrames(fid, self._page.frames());
};

/* ---------------- ElementHandle.uploadFile ----------------
 * Set <input type=file> contents.  Files must exist and be readable. */
ElementHandle.prototype.uploadFile = function(/* ...paths */) {
    var paths = Array.prototype.slice.call(arguments);
    var cb = null;
    if (paths.length && typeof paths[paths.length-1] === "function") cb = paths.pop();
    var self = this;
    function findBackend(r) {
        return r && r.node ? r.node.backendNodeId : null;
    }
    if (typeof cb === "function") {
        _callCdp(self._channel, "DOM.describeNode",
            {objectId: self._objectId}, self._sessionId, null, null,
            function(r, err) {
                if (err) return cb(null, err);
                var bn = findBackend(r);
                if (!bn) return cb(null, new Error("uploadFile: not an Element"));
                _callCdp(self._channel, "DOM.setFileInputFiles",
                    {files: paths, backendNodeId: bn},
                    self._sessionId, null, null, function(_, e2) { cb(null, e2); });
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.uploadFile.apply(self, paths.concat(function(_, err) {
                err ? rej(err) : res();
            }));
        });
    }
    var r = _callCdp(self._channel, "DOM.describeNode",
        {objectId: self._objectId}, self._sessionId, null, null, null);
    var bn = findBackend(r);
    if (!bn) throw new Error("uploadFile: not an Element");
    _callCdp(self._channel, "DOM.setFileInputFiles",
        {files: paths, backendNodeId: bn},
        self._sessionId, null, null, null);
};

/* ---------------- ElementHandle.select / press ---------------- */

ElementHandle.prototype.select = function(/* ...values */) {
    var values = Array.prototype.slice.call(arguments);
    var cb = null;
    if (values.length && typeof values[values.length-1] === "function") cb = values.pop();
    return this.evaluate(
        "function(el, values) {"
        + "  if (el.nodeName !== 'SELECT')"
        + "    throw new Error('select: not a <select> element');"
        + "  el.value = undefined;"
        + "  var opts = Array.from(el.options);"
        + "  for (var i=0; i<opts.length; i++) {"
        + "    var opt = opts[i];"
        + "    opt.selected = values.indexOf(opt.value) >= 0;"
        + "    if (opt.selected && !el.multiple) break;"
        + "  }"
        + "  el.dispatchEvent(new Event('input',  {bubbles:true}));"
        + "  el.dispatchEvent(new Event('change', {bubbles:true}));"
        + "  return opts.filter(function(o){return o.selected;}).map(function(o){return o.value;});"
        + "}", values, cb);
};

ElementHandle.prototype.press = function(key, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    function doChain(finish) {
        self.focus(function(_, e1) {
            if (e1) return finish(e1);
            self._page.keyboard.press(key, opts, function(_, e2) { finish(e2); });
        });
    }
    if (typeof cb === "function") {
        doChain(function(err) { cb(null, err); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err) { err ? rej(err) : res(); });
        });
    }
    this.focus();
    this._page.keyboard.press(key, opts);
};

/* ================================================================== *
 *  Tier 2 additions — broader Puppeteer parity:
 *    evaluateHandle (Page/Frame),
 *    page.metrics, isClosed, bringToFront,
 *    page.waitForRequest,
 *    BrowserContext.overridePermissions / clearPermissionOverrides,
 *    Mouse.reset, Mouse.dragAndDrop, Keyboard.reset,
 *    page.accessibility.snapshot,
 *    page.waitForFileChooser + FileChooser.
 * ================================================================== */

/* ---------------- Page.evaluateHandle / Frame.evaluateHandle ----------------
 * Returns a JSHandle (or ElementHandle if the result is a DOM node) for
 * the page-side expression's result, instead of serializing the value. */
Page.prototype.evaluateHandle = function(fnOrStr /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var src = _toExprSource(fnOrStr, args);
    var self = this;
    function unwrap(r) {
        if (!r) return null;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "evaluateHandle failed");
        }
        return _wrapRemote(self, r.result);
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _callCdp(this._channel, "Runtime.evaluate", {
        expression: src,
        returnByValue: false,
        awaitPromise: true,
        userGesture: true
    }, this._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Frame.prototype.evaluateHandle = function(fnOrStr /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var src = _toExprSource(fnOrStr, args);
    var self = this;
    function unwrap(r) {
        if (!r) return null;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "evaluateHandle failed");
        }
        return _wrapRemote(self._page, r.result);
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _sendProc(self._channel, "frameEval", {
        sessionId:     self._sessionId,
        frameId:       self._frameId,
        expression:    src,
        returnByValue: false
    }, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* ---------------- Page.metrics / isClosed / bringToFront ---------------- */

Page.prototype.metrics = function(cb) {
    var self = this;
    function ensureEnabled(finish) {
        if (self._metricsEnabled) return finish(null);
        _callCdp(self._channel, "Performance.enable", {},
            self._sessionId, null, null, function(_, err) {
                if (!err) self._metricsEnabled = true;
                finish(err);
            });
    }
    function fetch(cb2) {
        _callCdp(self._channel, "Performance.getMetrics", {},
            self._sessionId, null, null, cb2);
    }
    function toObj(r) {
        var out = {};
        if (!r || !r.metrics) return out;
        for (var i = 0; i < r.metrics.length; i++)
            out[r.metrics[i].name] = r.metrics[i].value;
        return out;
    }
    if (typeof cb === "function") {
        ensureEnabled(function(e1) {
            if (e1) return cb(null, e1);
            fetch(function(r, e2) {
                if (e2) return cb(null, e2);
                cb(toObj(r), null);
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.metrics(function(r, err) { err ? rej(err) : res(r); });
        });
    }
    if (!this._metricsEnabled) {
        this._call("Performance.enable", {}, null);
        this._metricsEnabled = true;
    }
    var r = this._call("Performance.getMetrics", {}, null);
    return toObj(r);
};

Page.prototype.isClosed = function() {
    return !!this._closed;
};

Page.prototype.bringToFront = function(cb) {
    return this._call("Page.bringToFront", {}, null, cb);
};

/* Hook Page.close to record _closed and fire any 'close' listeners. */
(function() {
    var origClose = Page.prototype.close;
    Page.prototype.close = function(cb) {
        var self = this;
        var was = this._closed;
        this._closed = true;
        if (!was) {
            /* fire 'close' listeners (a string event the user can subscribe to
             * via page.on('close', fn) — no CDP equivalent, this is a JS-side
             * synthetic event). */
            var listeners = this._closeListeners || [];
            for (var i = 0; i < listeners.length; i++) {
                try { listeners[i](); } catch(e) {}
            }
        }
        return origClose.call(this, cb);
    };
})();

/* Extend Page.on with the synthetic 'close' event. */
(function() {
    var origOn = Page.prototype.on;
    Page.prototype.on = function(event, handler) {
        if (event === "close") {
            if (!this._closeListeners) this._closeListeners = [];
            this._closeListeners.push(handler);
            return this;
        }
        return origOn.call(this, event, handler);
    };
})();

/* ---------------- Page.waitForRequest ----------------
 * Worker proc: wait for a Network.requestWillBeSent whose URL matches. */
(function() {
    /* Patch in a new worker procedure name by piggy-backing on _sendProc
     * — the worker already accepts arbitrary proc names from `procedures`.
     * For waitForRequest we need to register a pending entry in the worker.
     * We do that by reusing the waitForResponse flow but on a different
     * CDP event.  Since the existing worker doesn't have a "requestWait"
     * mode, we ship a different match against the same event-loop pump:
     * use waitForEvent (no urlMatch).  Then on the main side, filter by url
     * once the event fires — by repeatedly waiting until a match. */
    /* Simpler approach: on main, register an event handler for
     * Network.requestWillBeSent and resolve on the first matching url. */
})();

Page.prototype.waitForRequest = function(urlMatch, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this, opts, false);
    var self = this;
    var ch = self._channel, sid = self._sessionId;
    var evName  = ch + ".ev." + sid + ".Network.requestWillBeSent";
    function match(url) {
        if (typeof urlMatch !== "string") return false;
        if (urlMatch.length >= 2
            && urlMatch.charAt(0) === "/"
            && urlMatch.charAt(urlMatch.length-1) === "/") {
            try { return new RegExp(urlMatch.slice(1, -1)).test(url); }
            catch(e) { return false; }
        }
        return url.indexOf(urlMatch) >= 0;
    }
    function build(finish) {
        var fnName = "wfr-" + sid + "-" + (_tokenSeq++);
        var timer = null;
        rampart.event.on(evName, fnName, function(uv, payload) {
            var p = payload.params || {};
            var url = p.request && p.request.url;
            if (!match(url)) return;
            rampart.event.off(evName, fnName);
            if (timer) clearTimeout(timer);
            var existing = self._reqByNetId[p.requestId];
            if (existing) return finish(existing, null);
            var req = new Request(self, {
                requestId:    p.requestId,
                networkId:    p.requestId,
                request:      p.request,
                resourceType: p.type,
                frameId:      p.frameId
            });
            _trackReq(self, p.requestId, req);
            finish(req, null);
        });
        if (timeout > 0) timer = setTimeout(function() {
            rampart.event.off(evName, fnName);
            finish(null, new Error("waitForRequest: timeout"));
        }, timeout);
    }
    if (typeof cb === "function") {
        build(function(r, err) { err ? cb(null, err) : cb(r, null); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            build(function(r, err) { err ? rej(err) : res(r); });
        });
    }
    /* sync — block on a thread.del with timeout */
    var tok = "wfr-sync-" + (_tokenSeq++);
    var key = ch + ".wfr." + tok;
    var fnName = "wfr-syncfn-" + sid + "-" + (_tokenSeq++);
    rampart.event.on(evName, fnName, function(uv, payload) {
        var p = payload.params || {};
        var url = p.request && p.request.url;
        if (!match(url)) return;
        rampart.event.off(evName, fnName);
        rampart.thread.put(key, p);
    });
    var got = rampart.thread.del(key, timeout > 0 ? timeout : 60000);
    rampart.event.off(evName, fnName);
    if (!got) throw new Error("waitForRequest: timeout");
    var existing = self._reqByNetId[got.requestId];
    if (existing) return existing;
    var req = new Request(self, {
        requestId: got.requestId, networkId: got.requestId,
        request: got.request, resourceType: got.type, frameId: got.frameId
    });
    self._reqByNetId[got.requestId] = req;
    return req;
};

/* ---------------- BrowserContext.overridePermissions ---------------- */

BrowserContext.prototype.overridePermissions = function(origin, perms, cb) {
    var ch = this._browser._channel;
    var params = {origin: origin, permissions: perms};
    if (this._contextId) params.browserContextId = this._contextId;
    return _callCdp(ch, "Browser.grantPermissions", params, null, null, null, cb);
};

BrowserContext.prototype.clearPermissionOverrides = function(cb) {
    var ch = this._browser._channel;
    var params = {};
    if (this._contextId) params.browserContextId = this._contextId;
    return _callCdp(ch, "Browser.resetPermissions", params, null, null, null, cb);
};

/* ---------------- Mouse.reset / dragAndDrop, Keyboard.reset ---------------- */

Mouse.prototype.reset = function(cb) {
    var self = this;
    var btns = [];
    if (this._buttons & 1) btns.push("left");
    if (this._buttons & 2) btns.push("right");
    if (this._buttons & 4) btns.push("middle");
    if (btns.length === 0) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    function doChain(finish, i) {
        if (i >= btns.length) {
            self._buttons = 0;
            return finish(null);
        }
        self.up({button: btns[i]}, function(_, err) {
            if (err) return finish(err);
            doChain(finish, i + 1);
        });
    }
    if (typeof cb === "function") {
        doChain(function(err){ cb(null, err); }, 0);
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); }, 0);
        });
    }
    for (var i = 0; i < btns.length; i++) this.up({button: btns[i]});
    this._buttons = 0;
};

/* Click-and-drag using mouse events (mousemove + buttons).  Sites that
 * use HTML5 drag events (dragstart/dragover/drop) need a different path
 * that we don't implement here. */
/* Click-and-drag (the original, mouse-event style).  Use this for UIs
 * that listen for mousedown/mousemove/mouseup rather than HTML5 drag
 * events.  See mouse.dragAndDrop below for HTML5 drag. */
Mouse.prototype.clickAndDrag = function(from, to, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    var steps = opts.steps || 5;
    var delay = opts.delay || 0;
    function doChain(finish) {
        self.move(from.x, from.y, function(_, e1) {
            if (e1) return finish(e1);
            self.down({button: "left"}, function(_, e2) {
                if (e2) return finish(e2);
                function moveStep(i) {
                    if (i > steps) {
                        return self.up({button: "left"}, function(_, e4) {
                            finish(e4);
                        });
                    }
                    var t = i / steps;
                    var x = from.x + (to.x - from.x) * t;
                    var y = from.y + (to.y - from.y) * t;
                    self.move(x, y, function(_, e3) {
                        if (e3) return finish(e3);
                        if (delay > 0) setTimeout(function(){ moveStep(i+1); }, delay);
                        else moveStep(i+1);
                    });
                }
                moveStep(1);
            });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    /* sync */
    this.move(from.x, from.y);
    this.down({button: "left"});
    for (var i = 1; i <= steps; i++) {
        var t = i / steps;
        this.move(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        if (delay > 0) utils.sleep(delay / 1000);
    }
    this.up({button: "left"});
};

/* ---------------- HTML5 drag-and-drop ----------------
 *
 * Chrome's CDP supports intercepting drag operations so we can drive
 * HTML5 dragstart/dragenter/dragover/drop events programmatically.
 * Prerequisite: page.setDragInterception(true).
 *
 *   var data = page.mouse.drag(from, to);
 *   page.mouse.dragEnter(target, data);
 *   page.mouse.dragOver(target, data);
 *   page.mouse.drop(target, data);
 *
 * Or use page.mouse.dragAndDrop(from, target) which chains all of these.
 *
 * Without interception, dragAndDrop falls back to clickAndDrag (mouse
 * events only) so sites that use mouse-event-based drag still work. */

Mouse.prototype.drag = function(from, to, cb) {
    var self = this;
    var page = self._page;
    if (!page._dragInterceptionOn)
        throw new Error("mouse.drag: page.setDragInterception(true) is required");
    return _sendProc(page._channel, "mouseDrag", {
        sessionId: page._sessionId,
        from:      from,
        to:        to,
        steps:     5
    }, cb);
};

Mouse.prototype.dragEnter = function(target, data, cb) {
    return this._page._call("Input.dispatchDragEvent", {
        type: "dragEnter", x: target.x, y: target.y,
        data: data || {items: [], dragOperationsMask: 0}
    }, null, cb);
};

Mouse.prototype.dragOver = function(target, data, cb) {
    return this._page._call("Input.dispatchDragEvent", {
        type: "dragOver", x: target.x, y: target.y,
        data: data || {items: [], dragOperationsMask: 0}
    }, null, cb);
};

Mouse.prototype.drop = function(target, data, cb) {
    return this._page._call("Input.dispatchDragEvent", {
        type: "drop", x: target.x, y: target.y,
        data: data || {items: [], dragOperationsMask: 0}
    }, null, cb);
};

/* Full drag-and-drop sequence.  Uses HTML5 drag events when drag
 * interception is enabled on this page; otherwise falls back to
 * mouse.clickAndDrag (mouse events only). */
Mouse.prototype.dragAndDrop = function(from, to, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    var page = self._page;

    if (!page._dragInterceptionOn) {
        return self.clickAndDrag(from, to, opts, cb);
    }

    var delay = opts.delay || 0;
    function html5(finish) {
        self.drag(from, to, function(data, e1) {
            if (e1) return finish(e1);
            self.dragEnter(to, data, function(_, e2) {
                if (e2) return finish(e2);
                self.dragOver(to, data, function(_, e3) {
                    if (e3) return finish(e3);
                    function afterDelay() {
                        self.drop(to, data, function(_, e4) {
                            if (e4) return finish(e4);
                            self.up({button: "left"}, function(_, e5) { finish(e5); });
                        });
                    }
                    if (delay > 0) setTimeout(afterDelay, delay);
                    else           afterDelay();
                });
            });
        });
    }

    if (typeof cb === "function") {
        html5(function(err){ cb(null, err); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            html5(function(err){ err ? rej(err) : res(); });
        });
    }
    /* sync */
    var data = self.drag(from, to);
    self.dragEnter(to, data);
    self.dragOver(to, data);
    if (delay > 0) utils.sleep(delay / 1000);
    self.drop(to, data);
    self.up({button: "left"});
};

Page.prototype.setDragInterception = function(enabled, cb) {
    this._dragInterceptionOn = !!enabled;
    return this._call("Input.setInterceptDrags",
        {enabled: !!enabled}, null, cb);
};

/* ---------------- ElementHandle drag convenience ----------------
 *
 * elementHandle.drag(targetHandleOrPoint) — pick this element's center
 * for the start and the target element's center (or the supplied
 * point) for the destination.  Requires page.setDragInterception(true). */

function _centerOf(handleOrPoint) {
    if (!handleOrPoint) throw new Error("drag target missing");
    if (handleOrPoint instanceof ElementHandle) {
        var box = handleOrPoint.boundingBox();
        if (!box) throw new Error("drag target element has no bounding box");
        return {x: box.x + box.width/2, y: box.y + box.height/2};
    }
    return {x: handleOrPoint.x, y: handleOrPoint.y};
}

ElementHandle.prototype.drag = function(target, cb) {
    var self = this;
    var page = self._page;
    if (!page._dragInterceptionOn)
        throw new Error("ElementHandle.drag: page.setDragInterception(true) is required");
    function doChain(finish) {
        self.boundingBox(function(box, e1) {
            if (e1) return finish(null, e1);
            if (!box) return finish(null, new Error("drag source has no bounding box"));
            var from = {x: box.x + box.width/2, y: box.y + box.height/2};
            var to;
            if (target instanceof ElementHandle) {
                target.boundingBox(function(b2, e2) {
                    if (e2) return finish(null, e2);
                    if (!b2) return finish(null, new Error("drag target has no bounding box"));
                    to = {x: b2.x + b2.width/2, y: b2.y + b2.height/2};
                    page.mouse.drag(from, to, function(data, e3) { finish(data, e3); });
                });
            } else {
                to = {x: target.x, y: target.y};
                page.mouse.drag(from, to, function(data, e3) { finish(data, e3); });
            }
        });
    }
    if (typeof cb === "function") {
        doChain(function(data, err) { err ? cb(null, err) : cb(data, null); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(data, err) { err ? rej(err) : res(data); });
        });
    }
    var box = self.boundingBox();
    if (!box) throw new Error("drag source has no bounding box");
    var from = {x: box.x + box.width/2, y: box.y + box.height/2};
    var to = _centerOf(target);
    return page.mouse.drag(from, to);
};

ElementHandle.prototype.dragEnter = function(data, cb) {
    var c = _centerOf(this);
    return this._page.mouse.dragEnter(c, data, cb);
};
ElementHandle.prototype.dragOver = function(data, cb) {
    var c = _centerOf(this);
    return this._page.mouse.dragOver(c, data, cb);
};
ElementHandle.prototype.drop = function(data, cb) {
    var c = _centerOf(this);
    return this._page.mouse.drop(c, data, cb);
};

ElementHandle.prototype.dragAndDrop = function(target, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    var page = self._page;
    if (!page._dragInterceptionOn)
        throw new Error("ElementHandle.dragAndDrop: page.setDragInterception(true) is required");
    function doChain(finish) {
        self.boundingBox(function(b1, e1) {
            if (e1) return finish(e1);
            if (!b1) return finish(new Error("drag source has no bounding box"));
            var from = {x: b1.x + b1.width/2, y: b1.y + b1.height/2};
            function withTo(to) {
                page.mouse.dragAndDrop(from, to, opts, function(_, err) { finish(err); });
            }
            if (target instanceof ElementHandle) {
                target.boundingBox(function(b2, e2) {
                    if (e2) return finish(e2);
                    if (!b2) return finish(new Error("drag target has no bounding box"));
                    withTo({x: b2.x + b2.width/2, y: b2.y + b2.height/2});
                });
            } else {
                withTo({x: target.x, y: target.y});
            }
        });
    }
    if (typeof cb === "function") {
        doChain(function(err){ cb(null, err); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    var box = self.boundingBox();
    if (!box) throw new Error("drag source has no bounding box");
    var from = {x: box.x + box.width/2, y: box.y + box.height/2};
    var to = _centerOf(target);
    page.mouse.dragAndDrop(from, to, opts);
};

Keyboard.prototype.reset = function(cb) {
    var modKeys = [];
    if (this._modifiers & 1) modKeys.push("Alt");
    if (this._modifiers & 2) modKeys.push("Control");
    if (this._modifiers & 4) modKeys.push("Meta");
    if (this._modifiers & 8) modKeys.push("Shift");
    var self = this;
    if (modKeys.length === 0) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    function doChain(finish, i) {
        if (i >= modKeys.length) {
            self._modifiers = 0;
            return finish(null);
        }
        self.up(modKeys[i], function(_, err) {
            if (err) return finish(err);
            doChain(finish, i + 1);
        });
    }
    if (typeof cb === "function") {
        doChain(function(err){ cb(null, err); }, 0);
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); }, 0);
        });
    }
    for (var i = 0; i < modKeys.length; i++) this.up(modKeys[i]);
    this._modifiers = 0;
};

/* ---------------- page.accessibility.snapshot ----------------
 * Returns the a11y tree as Puppeteer's "AXNode" shape:
 *   {role, name, value, children, ...}
 * Uses Accessibility.getFullAXTree which returns a flat list keyed by
 * nodeId; we reconstitute the tree via the parent/children fields. */
function _ax_unwrapVal(v) {
    if (!v) return undefined;
    if (v.value !== undefined) return v.value;
    return undefined;
}
function _ax_simplify(nodes, rootBackendId) {
    if (!Array.isArray(nodes) || nodes.length === 0) return null;
    var byId = {};
    nodes.forEach(function(n) { byId[n.nodeId] = n; });
    function build(n) {
        var role  = _ax_unwrapVal(n.role)  || "";
        var name  = _ax_unwrapVal(n.name)  || "";
        var value = _ax_unwrapVal(n.value);
        var props = {};
        if (n.properties) {
            n.properties.forEach(function(p) {
                props[p.name] = _ax_unwrapVal(p.value);
            });
        }
        var out = {role: role};
        if (name) out.name = name;
        if (value !== undefined) out.value = value;
        var pkeys = Object.keys(props);
        if (pkeys.length) {
            for (var i = 0; i < pkeys.length; i++)
                out[pkeys[i]] = props[pkeys[i]];
        }
        if (n.childIds && n.childIds.length) {
            out.children = [];
            for (var i = 0; i < n.childIds.length; i++) {
                var c = byId[n.childIds[i]];
                if (c) out.children.push(build(c));
            }
        }
        return out;
    }
    /* If a specific root was requested, build from the node that
       describes it.  `Accessibility.getPartialAXTree` may return a
       few ancestors (with `fetchRelatives: true`) plus the requested
       node and its subtree; pick the one whose backendDOMNodeId
       matches.  Fall back to the orphan-root search. */
    if (rootBackendId) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].backendDOMNodeId === rootBackendId)
                return build(nodes[i]);
        }
    }
    /* Root is the first node with no parent in the byId map. */
    for (var i = 0; i < nodes.length; i++) {
        if (!nodes[i].parentId || !byId[nodes[i].parentId])
            return build(nodes[i]);
    }
    return build(nodes[0]);
}

/* `interestingOnly` mode pruning, defaulted on in Puppeteer.  A node
   is "interesting" if it carries non-trivial role/name/value or has
   any interactive state property — i.e., something a screen reader
   would announce.  Containers like generic divs that exist only as
   layout boxes are dropped; their children get lifted into the
   parent's child list so the visible structure is preserved.  The
   root is always kept regardless of its own interest level — same
   convention as Puppeteer. */
var _AX_INTERESTING_PROPS = [
    "focusable","focused","editable","checked","expanded","selected",
    "required","invalid","disabled","modal","multiselectable","pressed",
    "readonly","level","valuemin","valuemax","valuetext","autocomplete",
    "haspopup","hidden","keyshortcuts","live","relevant","atomic","busy",
    "orientation","placeholder","roledescription","sort"
];
function _ax_isInteresting(node) {
    if (node.role && node.role !== "none" && node.role !== "presentation"
        && node.role !== "generic" && node.role !== "InlineTextBox")
        return true;
    if (node.name) return true;
    if (node.value !== undefined && node.value !== "") return true;
    for (var i = 0; i < _AX_INTERESTING_PROPS.length; i++) {
        if (node[_AX_INTERESTING_PROPS[i]] !== undefined) return true;
    }
    return false;
}
function _ax_pruneSubtree(node) {
    var oldChildren = node.children || [];
    var newChildren = [];
    for (var i = 0; i < oldChildren.length; i++) {
        var r = _ax_pruneSubtree(oldChildren[i]);
        if (Array.isArray(r))      newChildren = newChildren.concat(r);
        else if (r)                newChildren.push(r);
    }
    if (newChildren.length) node.children = newChildren;
    else                    delete node.children;
    return node;
}
function _ax_pruneChild(node) {
    var oldChildren = node.children || [];
    var newChildren = [];
    for (var i = 0; i < oldChildren.length; i++) {
        var r = _ax_pruneChild(oldChildren[i]);
        if (Array.isArray(r))      newChildren = newChildren.concat(r);
        else if (r)                newChildren.push(r);
    }
    if (_ax_isInteresting(node)) {
        if (newChildren.length) node.children = newChildren;
        else                    delete node.children;
        return node;
    }
    /* Uninteresting — lift our children into our parent's slot. */
    return newChildren.length ? newChildren : null;
}
function _ax_prune(rootNode) {
    if (!rootNode) return rootNode;
    /* Always keep the root; only prune below. */
    var oldChildren = rootNode.children || [];
    var newChildren = [];
    for (var i = 0; i < oldChildren.length; i++) {
        var r = _ax_pruneChild(oldChildren[i]);
        if (Array.isArray(r))      newChildren = newChildren.concat(r);
        else if (r)                newChildren.push(r);
    }
    if (newChildren.length) rootNode.children = newChildren;
    else                    delete rootNode.children;
    return rootNode;
}

function Accessibility(page) {
    this._page = page;
}
Accessibility.prototype.snapshot = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    /* `interestingOnly` defaults true in Puppeteer.  `root` is an
       ElementHandle to re-root the snapshot at. */
    var interestingOnly = opts.interestingOnly === undefined
                        ? true : !!opts.interestingOnly;
    var rootHandle = opts.root || null;
    function ensureEnabled(finish) {
        if (self._page._a11yEnabled) return finish(null);
        _callCdp(self._page._channel, "Accessibility.enable", {},
            self._page._sessionId, null, null, function(_, err) {
                if (!err) self._page._a11yEnabled = true;
                finish(err);
            });
    }
    /* Resolve the root ElementHandle to a backendNodeId so we can use
       Accessibility.getPartialAXTree.  Returns (err, backendNodeId);
       null backendNodeId means whole-document snapshot. */
    function resolveRoot(finish) {
        if (!rootHandle) return finish(null, null);
        if (!rootHandle._objectId)
            return finish(new Error("snapshot: root has no objectId"));
        _callCdp(self._page._channel, "DOM.describeNode",
            {objectId: rootHandle._objectId},
            self._page._sessionId, null, null,
            function(r, err) {
                if (err) return finish(err);
                var bn = r && r.node ? r.node.backendNodeId : null;
                if (!bn)
                    return finish(new Error("snapshot: root has no backendNodeId"));
                finish(null, bn);
            });
    }
    function fetch(backendNodeId, cb2) {
        if (backendNodeId) {
            _callCdp(self._page._channel, "Accessibility.getPartialAXTree",
                {backendNodeId: backendNodeId, fetchRelatives: true},
                self._page._sessionId, null, null, cb2);
        } else {
            _callCdp(self._page._channel, "Accessibility.getFullAXTree", {},
                self._page._sessionId, null, null, cb2);
        }
    }
    function shape(r, backendNodeId) {
        var built = _ax_simplify(r && r.nodes, backendNodeId);
        if (interestingOnly) built = _ax_prune(built);
        return built;
    }
    if (typeof cb === "function") {
        ensureEnabled(function(e1) {
            if (e1) return cb(null, e1);
            resolveRoot(function(e2, bn) {
                if (e2) return cb(null, e2);
                fetch(bn, function(r, e3) {
                    if (e3) return cb(null, e3);
                    cb(shape(r, bn), null);
                });
            });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.snapshot(opts, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    if (!this._page._a11yEnabled) {
        this._page._call("Accessibility.enable", {}, null);
        this._page._a11yEnabled = true;
    }
    /* Sync mode: resolve root inline. */
    var bn = null;
    if (rootHandle && rootHandle._objectId) {
        var dn = _callCdp(this._page._channel, "DOM.describeNode",
            {objectId: rootHandle._objectId},
            this._page._sessionId, null, null, null);
        bn = dn && dn.node ? dn.node.backendNodeId : null;
        if (!bn) throw new Error("snapshot: root has no backendNodeId");
    }
    var r;
    if (bn) {
        r = this._page._call("Accessibility.getPartialAXTree",
            {backendNodeId: bn, fetchRelatives: true}, null);
    } else {
        r = this._page._call("Accessibility.getFullAXTree", {}, null);
    }
    return shape(r, bn);
};
Object.defineProperty(Page.prototype, "accessibility", {
    get: function() {
        if (!this._accessibility) this._accessibility = new Accessibility(this);
        return this._accessibility;
    }
});

/* ---------------- FileChooser + waitForFileChooser ----------------
 * Page.setInterceptFileChooserDialog(true) makes chrome fire
 * Page.fileChooserOpened instead of showing a native dialog.  The
 * handler can resolve via DOM.setFileInputFiles (or dismiss by setting
 * an empty file list).  We arm interception lazily on first
 * waitForFileChooser call. */

function FileChooser(page, params) {
    this._page       = page;
    this._channel    = page._channel;
    this._sessionId  = page._sessionId;
    this._backendNodeId = params.backendNodeId;
    this._multiple   = params.mode === "selectMultiple";
    this._handled    = false;
}
FileChooser.prototype.isMultiple = function() { return this._multiple; };
FileChooser.prototype.accept = function(paths, cb) {
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    if (!Array.isArray(paths)) paths = [paths];
    return _callCdp(this._channel, "DOM.setFileInputFiles",
        {files: paths, backendNodeId: this._backendNodeId},
        this._sessionId, null, null, cb);
};
FileChooser.prototype.cancel = function(cb) {
    if (this._handled) {
        if (cb) { cb(null, null); return; }
        if (_isTranspiled()) return Promise.resolve();
        return;
    }
    this._handled = true;
    return _callCdp(this._channel, "DOM.setFileInputFiles",
        {files: [], backendNodeId: this._backendNodeId},
        this._sessionId, null, null, cb);
};

Page.prototype.waitForFileChooser = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this, opts, false);
    var self = this;
    var ch = self._channel, sid = self._sessionId;
    var evName = ch + ".ev." + sid + ".Page.fileChooserOpened";
    function arm(finish) {
        if (self._fcArmed) return finish(null);
        _callCdp(ch, "Page.setInterceptFileChooserDialog",
            {enabled: true}, sid, null, null, function(_, err) {
                if (!err) self._fcArmed = true;
                finish(err);
            });
    }
    function wait(finish) {
        var fnName = "fc-" + sid + "-" + (_tokenSeq++);
        var timer = null;
        rampart.event.on(evName, fnName, function(uv, payload) {
            var p = payload.params || {};
            rampart.event.off(evName, fnName);
            if (timer) clearTimeout(timer);
            finish(new FileChooser(self, p), null);
        });
        if (timeout > 0) timer = setTimeout(function() {
            rampart.event.off(evName, fnName);
            finish(null, new Error("waitForFileChooser: timeout"));
        }, timeout);
    }
    if (typeof cb === "function") {
        arm(function(e1) {
            if (e1) return cb(null, e1);
            wait(function(r, e2) { e2 ? cb(null, e2) : cb(r, null); });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.waitForFileChooser(opts, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    /* sync — arm then block on thread.del */
    if (!self._fcArmed) {
        self._call("Page.setInterceptFileChooserDialog", {enabled: true}, null);
        self._fcArmed = true;
    }
    var tok = "fc-sync-" + (_tokenSeq++);
    var key = ch + ".fcsync." + tok;
    var fnName = "fc-syncfn-" + sid + "-" + (_tokenSeq++);
    rampart.event.on(evName, fnName, function(uv, payload) {
        rampart.event.off(evName, fnName);
        rampart.thread.put(key, payload.params || {});
    });
    var got = rampart.thread.del(key, timeout > 0 ? timeout : 60000);
    rampart.event.off(evName, fnName);
    if (!got) throw new Error("waitForFileChooser: timeout");
    return new FileChooser(self, got);
};

/* ================================================================== *
 *  Tier 3 additions — long-tail Puppeteer parity:
 *    TimeoutError, networkConditions, executablePath, defaultArgs;
 *    Target + CDPSession (raw CDP escape hatch);
 *    Browser introspection + browser-level events;
 *    Emulation extras (timezone, CPU, network, vision, media features);
 *    Richer Request/Response fields (redirectChain, initiator, timing, ...);
 *    ElementHandle scoped selectors + visibility helpers + tap;
 *    Frame parity with Page;
 *    Page event off/once + popup/frame/worker events + queryObjects;
 *    page.coverage, page.tracing, page.workers + Worker.
 * ================================================================== */

/* ---------------- TimeoutError (typed error) ---------------- */

function TimeoutError(message) {
    if (!(this instanceof TimeoutError))
        return new TimeoutError(message);
    var e = new Error(message);
    e.name = "TimeoutError";
    /* duktape lets us write a real prototype chain only via __proto__;
     * make the returned Error look like an instance of TimeoutError so
     * `err instanceof chrome.TimeoutError` works for callers. */
    try { e.__proto__ = TimeoutError.prototype; } catch(_) {}
    return e;
}
TimeoutError.prototype = Object.create(Error.prototype);
TimeoutError.prototype.constructor = TimeoutError;
TimeoutError.prototype.name = "TimeoutError";

/* ---------------- networkConditions presets ---------------- */

var _networkConditions = {
    "Slow 3G": {
        offline: false,
        downloadThroughput: 500 * 1024 / 8,
        uploadThroughput:   500 * 1024 / 8,
        latency: 400
    },
    "Fast 3G": {
        offline: false,
        downloadThroughput: 1.6 * 1024 * 1024 / 8,
        uploadThroughput:   750 * 1024 / 8,
        latency: 150
    },
    "Slow 4G": {
        offline: false,
        downloadThroughput: 3 * 1024 * 1024 / 8,
        uploadThroughput:   1.5 * 1024 * 1024 / 8,
        latency: 75
    },
    "Fast 4G": {
        offline: false,
        downloadThroughput: 10 * 1024 * 1024 / 8,
        uploadThroughput:   5 * 1024 * 1024 / 8,
        latency: 25
    },
    "Offline": {
        offline: true,
        downloadThroughput: 0,
        uploadThroughput:   0,
        latency: 0
    }
};

/* ---------------- Module-level executablePath / defaultArgs ---------------- */

function executablePath(override) {
    return _locateChrome(override);
}

function defaultArgs(opts) {
    opts = opts || {};
    var flags = [
        "--remote-debugging-port=0",
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-features=Translate,BackForwardCache",
        "--disable-background-networking",
        "--disable-sync"
    ];
    if (opts.userDataDir) flags.push("--user-data-dir=" + opts.userDataDir);
    if (opts.headless !== false) {
        flags.push("--headless=new");
        flags.push("--hide-scrollbars");
        flags.push("--mute-audio");
    }
    if (opts.args) flags = flags.concat(opts.args);
    return flags;
}

/* ---------------- Emulation extras ---------------- */

Page.prototype.emulateMediaFeatures = function(features, cb) {
    /* features: [{name, value}].  Pass [] (or null) to clear. */
    var f = features || [];
    return this._call("Emulation.setEmulatedMedia",
        {features: f}, null, cb);
};

Page.prototype.emulateTimezone = function(tz, cb) {
    /* tz is an IANA name like "America/Los_Angeles" or "" to clear. */
    return this._call("Emulation.setTimezoneOverride",
        {timezoneId: tz || ""}, null, cb);
};

Page.prototype.emulateCPUThrottling = function(rate, cb) {
    /* rate: 1 = no throttling, 2 = 2x slower, ...  Pass null to clear. */
    if (rate === null || rate === undefined) rate = 1;
    return this._call("Emulation.setCPUThrottlingRate",
        {rate: +rate}, null, cb);
};

Page.prototype.emulateNetworkConditions = function(conditions, cb) {
    if (typeof conditions === "string") {
        var preset = _networkConditions[conditions];
        if (!preset) throw new Error("emulateNetworkConditions: unknown preset '" + conditions + "'");
        conditions = preset;
    }
    if (!conditions) conditions = {offline: false, latency: 0,
                                   downloadThroughput: -1, uploadThroughput: -1};
    return this._call("Network.emulateNetworkConditions", {
        offline: !!conditions.offline,
        latency: conditions.latency || 0,
        downloadThroughput: conditions.downloadThroughput !== undefined
                            ? conditions.downloadThroughput : -1,
        uploadThroughput:   conditions.uploadThroughput   !== undefined
                            ? conditions.uploadThroughput   : -1,
        connectionType: conditions.connectionType
    }, null, cb);
};

Page.prototype.emulateVisionDeficiency = function(type, cb) {
    /* type: "none"/"achromatopsia"/"blurredVision"/"deuteranopia"/
     * "protanopia"/"tritanopia"/"reducedContrast"/null */
    return this._call("Emulation.setEmulatedVisionDeficiency",
        {type: type || "none"}, null, cb);
};

/* ---------------- Target + CDPSession ----------------
 *
 * Target is a thin wrapper around CDP TargetInfo.  CDPSession is the
 * raw CDP escape hatch: arbitrary `send(method, params)` and
 * `on(method, fn)` against a specific session, useful for the bits we
 * haven't wrapped.  Sessions own their event subscriptions and must be
 * detached when no longer needed. */

function Target(browser, info) {
    this._browser         = browser;
    this._targetId        = info.targetId;
    this._type            = info.type;
    this._url             = info.url || "";
    this._title           = info.title || "";
    this._browserContextId= info.browserContextId || "";
    /* Only set for targets we have an active session for. */
    this._sessionId       = info.sessionId || null;
}
Target.prototype.targetId       = function() { return this._targetId; };
Target.prototype.type           = function() { return this._type; };
Target.prototype.url            = function() { return this._url; };
Target.prototype.browser        = function() { return this._browser; };
Target.prototype.browserContext = function() {
    if (!this._browserContextId) return this._browser._defaultContext;
    return this._browser._contextById[this._browserContextId]
        || this._browser._defaultContext;
};
Target.prototype.page = function() {
    if (this._type !== "page") return null;
    var existing = this._browser._pages[this._targetId];
    if (existing) {
        /* Match Puppeteer's `target.page()` contract — Promise<Page|null>.
           Callers in transpiled async code (e.g. puppeteer-extra-plugin's
           `const page = await target.page()`) get a thenable. */
        if (_isTranspiled()) return Promise.resolve(existing);
        return existing;
    }
    /* No Page yet.  Targetcreated CDP events arrive while
       browser.newPage()'s RPC reply is still in flight; the Page
       constructor hasn't run for this targetId.  Sync callers get
       null (legacy behavior).  Transpiled callers get a Promise
       that resolves when the Page is constructed and registers
       itself in browser._pages, or null after a short ceiling. */
    if (!_isTranspiled()) return null;
    var self = this;
    var tid  = this._targetId;
    return new Promise(function (res) {
        var pendingMap = self._browser._pagesPending
                     || (self._browser._pagesPending = {});
        var list = pendingMap[tid] || (pendingMap[tid] = []);
        list.push(res);
        setTimeout(function () {
            var l = pendingMap[tid];
            if (l) {
                var i = l.indexOf(res);
                if (i >= 0) l.splice(i, 1);
                if (!l.length) delete pendingMap[tid];
            }
            res(self._browser._pages[tid] || null);
        }, 2000);
    });
};
Target.prototype.createCDPSession = function(cb) {
    var self = this;
    function build(finish) {
        if (self._sessionId)
            return finish(new CDPSession(self._browser, self._sessionId, self._targetId), null);
        _sendProc(self._browser._channel, "attachExisting",
            {targetId: self._targetId}, function(r, err) {
                if (err) return finish(null, err);
                self._sessionId = r.sessionId;
                finish(new CDPSession(self._browser, r.sessionId, self._targetId), null);
            });
    }
    if (typeof cb === "function") {
        build(function(s, err) { err ? cb(null, err) : cb(s, null); });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            build(function(s, err) { err ? rej(err) : res(s); });
        });
    }
    if (self._sessionId)
        return new CDPSession(self._browser, self._sessionId, self._targetId);
    var r = _sendProc(self._browser._channel, "attachExisting", {targetId: self._targetId});
    self._sessionId = r.sessionId;
    return new CDPSession(self._browser, r.sessionId, self._targetId);
};

function CDPSession(browser, sessionId, targetId) {
    this._browser   = browser;
    this._channel   = browser._channel;
    this._sessionId = sessionId;
    this._targetId  = targetId;
    this._handlers  = [];
    this._detached  = false;
}
CDPSession.prototype.send = function(method, params, cb) {
    if (typeof params === "function") { cb = params; params = {}; }
    if (this._detached)
        throw new Error("CDPSession.send: session is detached");
    return _callCdp(this._channel, method, params || {},
        this._sessionId, null, null, cb);
};
CDPSession.prototype.on = function(method, handler) {
    var evName = this._channel + ".ev." + this._sessionId + "." + method;
    var fnName = "cdpsess-" + this._sessionId + "-" + (_tokenSeq++);
    var wrap = function(uv, payload) {
        try { handler(payload.params || {}); } catch(e) {}
    };
    rampart.event.on(evName, fnName, wrap);
    this._handlers.push({method: method, handler: handler,
                         evName: evName, fnName: fnName});
    return this;
};
CDPSession.prototype.off = function(method, handler) {
    for (var i = this._handlers.length - 1; i >= 0; i--) {
        var h = this._handlers[i];
        if (h.method === method && (!handler || h.handler === handler)) {
            try { rampart.event.off(h.evName, h.fnName); } catch(_) {}
            this._handlers.splice(i, 1);
            if (handler) break;
        }
    }
    return this;
};
CDPSession.prototype.detach = function(cb) {
    var self = this;
    self._detached = true;
    self._handlers.forEach(function(h) {
        try { rampart.event.off(h.evName, h.fnName); } catch(_) {}
    });
    self._handlers = [];
    return _callCdp(self._channel, "Target.detachFromTarget",
        {sessionId: self._sessionId}, null, null, null, cb);
};
CDPSession.prototype.id = function() { return this._sessionId; };

/* ---------------- Back-references on Page/Frame ---------------- */

Page.prototype.target = function() {
    return new Target(this._browser, {
        targetId: this._targetId,
        type:     "page",
        url:      this._lastUrl || "",
        sessionId: this._sessionId,
        browserContextId: this._context && this._context._contextId
    });
};
Page.prototype.browser        = function() { return this._browser; };
Page.prototype.browserContext = function() { return this._context; };

/* page._client() — Puppeteer's internal handle on the page's CDP
   session.  Documented as private but widely used by libraries that
   need raw CDP access (puppeteer-extra-plugin-stealth's
   user-agent-override, devtools-frontend, har-recorders, etc.).
   Returns a CDPSession bound to the page's existing session, so no
   extra Target.attachToTarget round-trip is needed.  Memoized per
   page so identity-based callers (e.g. `page._client() === client`
   checks) see a stable object. */
Page.prototype._client = function() {
    if (!this.__clientCDPSession) {
        this.__clientCDPSession =
            new CDPSession(this._browser, this._sessionId, this._targetId);
    }
    return this.__clientCDPSession;
};

/* ---------------- Browser introspection ---------------- */

Browser.prototype.version = function(cb) {
    function unwrap(r) { return r ? r.product : ""; }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(unwrap(r), null); } : null;
    var raw = _callCdp(this._channel, "Browser.getVersion", {},
        null, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Browser.prototype.userAgent = function(cb) {
    function unwrap(r) { return r ? r.userAgent : ""; }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(unwrap(r), null); } : null;
    var raw = _callCdp(this._channel, "Browser.getVersion", {},
        null, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Browser.prototype.targets = function(cb) {
    var self = this;
    function unwrap(r) {
        if (!r || !r.targetInfos) return [];
        return r.targetInfos.map(function(t) { return new Target(self, t); });
    }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(unwrap(r), null); } : null;
    var raw = _sendProc(self._channel, "listTargets", {}, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

/* Enumerate live `service_worker` targets and return them as
   attached `Worker` instances.  Used by Puppeteer code to inspect
   PWA/cache service workers — `await sw.evaluate(...)` to query SW
   state, etc.  Attach is lazy: only the first call for a given SW
   target issues `Target.attachToTarget`; subsequent calls hit the
   per-Browser cache so identity stays stable across calls.  Stale
   entries for targets that have since been destroyed are evicted on
   each enumeration. */
Browser.prototype.serviceWorkers = function(cb) {
    var self = this;
    if (!self._swByTargetId) self._swByTargetId = {};
    function attach(target, finish) {
        var cached = self._swByTargetId[target._targetId];
        if (cached) return finish(cached, null);
        _callCdp(self._channel, "Target.attachToTarget",
            {targetId: target._targetId, flatten: true},
            null, null, null, function(r, err) {
                if (err) return finish(null, err);
                var w = new Worker(self, {
                    sessionId: r.sessionId,
                    targetId:  target._targetId,
                    url:       target.url(),
                    type:      "service_worker"
                });
                self._swByTargetId[target._targetId] = w;
                finish(w, null);
            });
    }
    function postprocess(targets, finish) {
        var swTargets = targets.filter(function(t) {
            return t.type() === "service_worker";
        });
        /* Drop cache entries for SW targets that no longer exist. */
        var live = {};
        swTargets.forEach(function(t) { live[t._targetId] = true; });
        Object.keys(self._swByTargetId).forEach(function(tid) {
            if (!live[tid]) delete self._swByTargetId[tid];
        });
        /* Attach any not-yet-cached, collect results in target order. */
        if (!swTargets.length) return finish([], null);
        var out = new Array(swTargets.length);
        var pending = swTargets.length;
        var firstErr = null;
        swTargets.forEach(function(t, i) {
            attach(t, function(w, err) {
                if (err && !firstErr) firstErr = err;
                out[i] = w;
                if (--pending === 0) {
                    if (firstErr) return finish(null, firstErr);
                    finish(out.filter(Boolean), null);
                }
            });
        });
    }
    if (typeof cb === "function") {
        self.targets(function(targets, err) {
            if (err) return cb(null, err);
            postprocess(targets, cb);
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.serviceWorkers(function(r, err) { err ? rej(err) : res(r); });
        });
    }
    /* Sync mode — block on each CDP step. */
    var targets = self.targets();
    var swTargets = targets.filter(function(t) {
        return t.type() === "service_worker";
    });
    var live = {};
    swTargets.forEach(function(t) { live[t._targetId] = true; });
    Object.keys(self._swByTargetId).forEach(function(tid) {
        if (!live[tid]) delete self._swByTargetId[tid];
    });
    var out = [];
    for (var i = 0; i < swTargets.length; i++) {
        var t = swTargets[i];
        var cached = self._swByTargetId[t._targetId];
        if (cached) { out.push(cached); continue; }
        var r = _callCdp(self._channel, "Target.attachToTarget",
            {targetId: t._targetId, flatten: true},
            null, null, null, null);
        var w = new Worker(self, {
            sessionId: r.sessionId,
            targetId:  t._targetId,
            url:       t.url(),
            type:      "service_worker"
        });
        self._swByTargetId[t._targetId] = w;
        out.push(w);
    }
    return out;
};

/* Same as Browser.serviceWorkers but scoped to one BrowserContext.
   Default-context: everything not in a named context.  Named
   context: only workers whose target reports our contextId. */
BrowserContext.prototype.serviceWorkers = function(cb) {
    var self = this;
    var browser = self._browser;
    function filter(arr) {
        if (self === browser._defaultContext) {
            return arr.filter(function(w) {
                var t = w._browserContextId || "";
                return !t || !browser._contextById[t];
            });
        }
        return arr.filter(function(w) {
            return w._browserContextId === self._contextId;
        });
    }
    /* We need the SW's browserContextId, which Target captures on
       construction.  Build a side-table from targets() so we can
       annotate each Worker. */
    function annotate(workers, finish) {
        browser.targets(function(targets, err) {
            if (err) return finish(null, err);
            var ctxByTarget = {};
            targets.forEach(function(t) {
                ctxByTarget[t._targetId] = t._browserContextId || "";
            });
            workers.forEach(function(w) {
                w._browserContextId = ctxByTarget[w._targetId] || "";
            });
            finish(filter(workers), null);
        });
    }
    if (typeof cb === "function") {
        browser.serviceWorkers(function(workers, err) {
            if (err) return cb(null, err);
            annotate(workers, cb);
        });
        return;
    }
    if (_isTranspiled()) {
        return browser.serviceWorkers().then(function(workers) {
            return new Promise(function(res, rej) {
                annotate(workers, function(r, err) { err ? rej(err) : res(r); });
            });
        });
    }
    var workers = browser.serviceWorkers();
    var targets = browser.targets();
    var ctxByTarget = {};
    targets.forEach(function(t) {
        ctxByTarget[t._targetId] = t._browserContextId || "";
    });
    workers.forEach(function(w) {
        w._browserContextId = ctxByTarget[w._targetId] || "";
    });
    return filter(workers);
};

BrowserContext.prototype.targets = function(cb) {
    var self = this;
    var browser = self._browser;
    function filter(arr) {
        if (self === browser._defaultContext) {
            return arr.filter(function(t) {
                if (!t._browserContextId) return true;
                return !browser._contextById[t._browserContextId];
            });
        }
        return arr.filter(function(t) { return t._browserContextId === self._contextId; });
    }
    if (typeof cb === "function") {
        browser.targets(function(r, err) { err ? cb(null, err) : cb(filter(r), null); });
        return;
    }
    if (_isTranspiled()) {
        return browser.targets().then(filter);
    }
    return filter(browser.targets());
};

BrowserContext.prototype.isIncognito = function() {
    /* Any explicitly created context is incognito-ish (separate cookie jar);
     * the default context is not. */
    return this !== this._browser._defaultContext;
};

Browser.prototype.isConnected = function() {
    if (this._disconnected) return false;
    /* In sync mode the on('disconnected') handler may not have run
     * yet because main's event loop is blocked.  Check the worker's
     * clipboard flag directly — it's set by the ws close callback
     * regardless of subscribers. */
    try {
        var closed = rampart.thread.get(this._channel + ".closed");
        if (closed) { this._disconnected = true; return false; }
    } catch(_) {}
    return true;
};

Browser.prototype.disconnect = function(cb) {
    var self = this;
    self._disconnected = true;
    rampart.thread.put(self._channel + ".shutdown", Date.now());
    rampart.thread.del(self._channel + ".closed", 1000);
    try { self._thread.close(); } catch(e) {}
    if (cb) { cb(null, null); return; }
    if (_isTranspiled()) return Promise.resolve();
};

Browser.prototype.process = function() {
    return this._proc ? {pid: this._proc.pid} : null;
};

/* ---------------- Browser-level events ---------------- */

(function() {
    var browserEvMap = {
        "targetcreated":   "Target.targetCreated",
        "targetdestroyed": "Target.targetDestroyed",
        "targetchanged":   "Target.targetInfoChanged"
    };
    Browser.prototype.on = function(event, handler) {
        var self = this;
        if (event === "disconnected") {
            /* Worker's ws close callback triggers `ch + ".disconnected"`
             * on the rampart.event bus.  Subscribe each listener
             * independently; mark as fire-once so a slow consumer can't
             * see the same disconnect twice. */
            var evName = self._channel + ".disconnected";
            var fnName = "br-dc-" + (_tokenSeq++);
            var fired = false;
            rampart.event.on(evName, fnName, function() {
                if (fired) return;
                fired = true;
                self._disconnected = true;
                try { handler(); } catch(_) {}
                try { rampart.event.off(evName, fnName); } catch(_) {}
            });
            return self;
        }
        var cdpMethod = browserEvMap[event];
        if (!cdpMethod)
            throw new Error("Browser.on: unsupported event '" + event + "'");
        var self = this;
        /* Ensure Target.setDiscoverTargets is on. */
        if (!self._discoverOn) {
            _callCdp(self._channel, "Target.setDiscoverTargets",
                {discover: true}, null, null, null, function(){});
            self._discoverOn = true;
        }
        var evName = self._channel + ".ev.." + cdpMethod;
        var fnName = "br-" + event + "-" + (_tokenSeq++);
        rampart.event.on(evName, fnName, function(uv, payload) {
            try {
                var p = payload.params || {};
                var info = p.targetInfo || p;
                var ret;
                if (event === "targetdestroyed") {
                    /* destroyed event has just `targetId`, no info; look it up */
                    var tid = info.targetId || p.targetId;
                    var page = self._pages[tid];
                    if (page) page._closed = true;
                    ret = handler(new Target(self, {targetId: tid, type: "unknown", url: ""}));
                } else {
                    ret = handler(new Target(self, info));
                }
                /* Track in-flight async handlers so BrowserContext.newPage
                   can drain them before resolving — that's how the
                   puppeteer-extra-plugin onPageCreated chain manages to
                   install stealth evasions, set UA overrides via
                   Network.setUserAgentOverride, etc., before the caller's
                   first page.goto().  Without this, those calls race the
                   navigation and apply too late. */
                if (ret && typeof ret.then === "function") {
                    self._tcInflight.push(ret);
                    var rm = function () {
                        var i = self._tcInflight.indexOf(ret);
                        if (i >= 0) self._tcInflight.splice(i, 1);
                    };
                    ret.then(rm, rm);
                }
            } catch(e) {}
        });
        return this;
    };
})();

/* ---------------- Richer Request fields ---------------- */

Request.prototype.redirectChain = function() {
    return this._redirectChain ? this._redirectChain.slice() : [];
};
Request.prototype.isNavigationRequest = function() {
    return this._resourceType === "Document";
};
Request.prototype.initiator = function() {
    return this._initiator || null;
};

/* Extend the Network.requestWillBeSent tracker to handle redirects
 * and stash the initiator. */
var _origSetupReqTracking = _setupRequestTracking;
_setupRequestTracking = function(page) {
    /* Re-implement: we replace the original, not augment, because the
     * original already calls rampart.event.on with a specific function
     * name and we can't easily intercept. */
    var ch = page._channel, sid = page._sessionId;
    function listen(method, tag, fn) {
        rampart.event.on(ch + ".ev." + sid + "." + method,
            "trk-" + tag + "-" + sid + "-" + (_tokenSeq++), fn);
    }
    listen("Network.requestWillBeSent", "wb", function(uv, payload) {
        var p = payload.params || {};
        var existing = page._reqByNetId[p.requestId];
        if (existing && p.redirectResponse) {
            /* The existing request was a redirect step.  Snapshot it as
             * the chain entry, then create the new request for the
             * post-redirect URL. */
            existing._response = new Response(page, {
                requestId: p.requestId,
                response:  p.redirectResponse
            });
            existing._response._request = existing;
            var chain = (existing._redirectChain || []).concat([existing]);
            var newReq = new Request(page, {
                requestId:    p.requestId,
                networkId:    p.requestId,
                request:      p.request,
                resourceType: p.type,
                frameId:      p.frameId
            });
            newReq._redirectChain = chain;
            newReq._initiator     = p.initiator || null;
            page._reqByNetId[p.requestId] = newReq;
            return;
        }
        if (existing) return;
        var req = new Request(page, {
            requestId:    p.requestId,
            networkId:    p.requestId,
            request:      p.request,
            resourceType: p.type,
            frameId:      p.frameId
        });
        req._initiator = p.initiator || null;
        _trackReq(page, p.requestId, req);
    });
    listen("Network.responseReceived", "rr", function(uv, payload) {
        var p = payload.params || {};
        if (page._respByNetId[p.requestId]) return;
        var resp = new Response(page, p);
        page._respByNetId[p.requestId] = resp;
        var req = page._reqByNetId[p.requestId];
        if (req) { req._response = resp; resp._request = req; }
    });
    listen("Network.loadingFailed", "lf", function(uv, payload) {
        var p = payload.params || {};
        var req = page._reqByNetId[p.requestId];
        if (req) req._failure = {errorText: p.errorText || ""};
        _scheduleNetTrackCleanup(page, p.requestId);
    });
    listen("Network.loadingFinished", "lfin", function(uv, payload) {
        var p = payload.params || {};
        _scheduleNetTrackCleanup(page, p.requestId);
    });
};

/* Defer deletion to the next event-loop tick so any sync user-level
   handlers subscribed to the page.on('requestfinished'/'requestfailed')
   surface (which look up by requestId in these same maps — see the
   `requestfinished` case in the page-level event dispatch) still find
   their entries.  After the tick, the request is fully reported to
   user code and can be freed.  Without this, every requestId hangs on
   the Page forever (`_reqByNetId`/`_respByNetId` grow linearly with
   navigations), which a long-running scraper would hit hard. */
function _scheduleNetTrackCleanup(page, requestId) {
    if (!requestId) return;
    setTimeout(function () {
        delete page._reqByNetId[requestId];
        delete page._respByNetId[requestId];
        var order = page._reqByNetIdOrder;
        if (order) {
            var i = order.indexOf(requestId);
            if (i >= 0) order.splice(i, 1);
        }
    }, 0);
}

/* Backstop cap for `_reqByNetId` / `_respByNetId`.  Chrome doesn't
   always emit Network.loadingFinished / loadingFailed for every
   request — notably for some Fetch-intercepted-then-continued
   requests under `--headless=new` (favicons, opaque sub-resources).
   Those entries would otherwise persist forever in spite of the
   loadingFinished cleanup.  The cap evicts the oldest entries (by
   insertion order) when the map exceeds `_NET_TRACK_CAP`.  Insertion
   order is tracked in a parallel array kept in sync with the map.
   `_NET_TRACK_CAP` is set generously high — large enough that no
   real page's in-flight working set is touched, low enough that a
   long-running scraper can't drift past it. */
var _NET_TRACK_CAP = 256;
function _trackReq(page, id, req) {
    if (id in page._reqByNetId) return;
    page._reqByNetId[id] = req;
    var order = page._reqByNetIdOrder || (page._reqByNetIdOrder = []);
    order.push(id);
    while (order.length > _NET_TRACK_CAP) {
        var dropId = order.shift();
        delete page._reqByNetId[dropId];
        delete page._respByNetId[dropId];
    }
}

/* ---------------- Richer Response fields ---------------- */

Response.prototype.fromCache         = function() {
    return !!(this._response.fromDiskCache || this._response.fromPrefetchCache);
};
Response.prototype.fromServiceWorker = function() {
    return !!this._response.fromServiceWorker;
};
Response.prototype.remoteAddress = function() {
    var r = this._response;
    if (!r.remoteIPAddress && !r.remotePort) return null;
    return {ip: r.remoteIPAddress || "", port: r.remotePort || 0};
};
Response.prototype.timing          = function() { return this._response.timing || null; };
Response.prototype.securityDetails = function() { return this._response.securityDetails || null; };
Response.prototype.frame           = function() {
    var req = this._request;
    return req ? req.frame() : null;
};

/* ---------------- ElementHandle scoped selectors + visibility ---------------- */

ElementHandle.prototype.$ = function(selector, cb) {
    var self = this;
    var fnSrc = "function(root, sel){return root.querySelector(sel);}";
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(null,[this].concat(Array.prototype.slice.call(arguments)));}";
    function unwrap(r) {
        if (!r || !r.result) return null;
        var o = r.result;
        if (o.subtype === "null" || o.type === "undefined") return null;
        if (!o.objectId) return null;
        return new ElementHandle(self._page, o);
    }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(unwrap(r), null); } : null;
    var raw = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId: self._objectId,
        functionDeclaration: wrapperSrc,
        arguments: [{value: selector}],
        returnByValue: false,
        awaitPromise: false
    }, self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

ElementHandle.prototype.$$ = function(selector, cb) {
    var self = this;
    var fnSrc = "function(root, sel){return Array.from(root.querySelectorAll(sel));}";
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(null,[this].concat(Array.prototype.slice.call(arguments)));}";
    if (typeof cb === "function") {
        _callCdp(self._channel, "Runtime.callFunctionOn", {
            objectId: self._objectId,
            functionDeclaration: wrapperSrc,
            arguments: [{value: selector}],
            returnByValue: false,
            awaitPromise: false
        }, self._sessionId, null, null, function(r1, e1) {
            if (e1) return cb(null, e1);
            if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
            _callCdp(self._channel, "Runtime.getProperties",
                {objectId: r1.result.objectId, ownProperties: true},
                self._sessionId, null, null, function(r2, e2) {
                    if (e2) return cb(null, e2);
                    cb(_elemsFromProps(self._page, r2), null);
                });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$$(selector, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    var r1 = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId: self._objectId,
        functionDeclaration: wrapperSrc,
        arguments: [{value: selector}],
        returnByValue: false,
        awaitPromise: false
    }, self._sessionId, null, null, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
        {objectId: r1.result.objectId, ownProperties: true},
        self._sessionId, null, null, null);
    return _elemsFromProps(self._page, r2);
};

ElementHandle.prototype.$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "ElementHandle.$eval function");
    /* ElementHandle.evaluate wraps so the user's body receives the element
     * as the first argument and then any additional args. */
    var body = "function(root, sel){"
             + "  var inner = root.querySelector(sel);"
             + "  if (!inner) throw new Error('$eval: no element matched ' + sel);"
             + "  return (" + fnSrc + ").apply(null, [inner].concat(" + JSON.stringify(args) + "));"
             + "}";
    return this.evaluate(body, selector, cb);
};

ElementHandle.prototype.$$eval = function(selector, pageFn /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 2);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var fnSrc = _requireSource(pageFn, "ElementHandle.$$eval function");
    var body = "function(root, sel){"
             + "  var els = Array.from(root.querySelectorAll(sel));"
             + "  return (" + fnSrc + ").apply(null, [els].concat(" + JSON.stringify(args) + "));"
             + "}";
    return this.evaluate(body, selector, cb);
};

ElementHandle.prototype.$x = function(xpath, cb) {
    var self = this;
    var fnSrc = "function(xp){"
              + "  var r = document.evaluate(xp, this, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);"
              + "  var a = [];"
              + "  for (var i=0;i<r.snapshotLength;i++) a.push(r.snapshotItem(i));"
              + "  return a;"
              + "}";
    var wrapperSrc = "function(){return (" + fnSrc
                   + ").apply(this, Array.prototype.slice.call(arguments));}";
    if (typeof cb === "function") {
        _callCdp(self._channel, "Runtime.callFunctionOn", {
            objectId: self._objectId,
            functionDeclaration: wrapperSrc,
            arguments: [{value: xpath}],
            returnByValue: false,
            awaitPromise: false
        }, self._sessionId, null, null, function(r1, e1) {
            if (e1) return cb(null, e1);
            if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
            _callCdp(self._channel, "Runtime.getProperties",
                {objectId: r1.result.objectId, ownProperties: true},
                self._sessionId, null, null, function(r2, e2) {
                    if (e2) return cb(null, e2);
                    cb(_elemsFromProps(self._page, r2), null);
                });
        });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$x(xpath, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    var r1 = _callCdp(self._channel, "Runtime.callFunctionOn", {
        objectId: self._objectId,
        functionDeclaration: wrapperSrc,
        arguments: [{value: xpath}],
        returnByValue: false,
        awaitPromise: false
    }, self._sessionId, null, null, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
        {objectId: r1.result.objectId, ownProperties: true},
        self._sessionId, null, null, null);
    return _elemsFromProps(self._page, r2);
};

ElementHandle.prototype.scrollIntoView = function(cb) {
    return this.evaluate(
        "function(el){el.scrollIntoView({block:'center',inline:'center'});}",
        cb);
};

ElementHandle.prototype.isIntersectingViewport = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var threshold = opts.threshold !== undefined ? opts.threshold : 0;
    return this.evaluate(
        "function(el, t){return new Promise(function(res){"
        + "  var io = new IntersectionObserver(function(entries){"
        + "    io.disconnect();"
        + "    res(entries[0] && entries[0].intersectionRatio > t);"
        + "  });"
        + "  io.observe(el);"
        + "  requestAnimationFrame(function(){});"
        + "});}", threshold, cb);
};

ElementHandle.prototype.isVisible = function(cb) {
    return this.evaluate(
        "function(el){"
        + "  if (!el || !el.ownerDocument) return false;"
        + "  var style = el.ownerDocument.defaultView.getComputedStyle(el);"
        + "  if (style.visibility === 'hidden' || style.display === 'none') return false;"
        + "  var r = el.getBoundingClientRect();"
        + "  return !!(r.width || r.height);"
        + "}", cb);
};

ElementHandle.prototype.isHidden = function(cb) {
    if (typeof cb === "function") {
        return this.isVisible(function(v, err) { err ? cb(null, err) : cb(!v, null); });
    }
    if (_isTranspiled()) return this.isVisible().then(function(v){ return !v; });
    return !this.isVisible();
};

ElementHandle.prototype.tap = function(cb) {
    var self = this;
    function doChain(finish) {
        self.boundingBox(function(box, err) {
            if (err) return finish(err);
            if (!box) return finish(new Error("tap: element has no bounding box"));
            var cx = box.x + box.width/2, cy = box.y + box.height/2;
            _callCdp(self._channel, "Input.dispatchTouchEvent",
                {type: "touchStart", touchPoints: [{x: cx, y: cy}]},
                self._sessionId, null, null, function(_, e1) {
                    if (e1) return finish(e1);
                    _callCdp(self._channel, "Input.dispatchTouchEvent",
                        {type: "touchEnd", touchPoints: []},
                        self._sessionId, null, null, function(_, e2) { finish(e2); });
                });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            doChain(function(err){ err ? rej(err) : res(); });
        });
    }
    var box = this.boundingBox();
    if (!box) throw new Error("tap: element has no bounding box");
    var cx = box.x + box.width/2, cy = box.y + box.height/2;
    _callCdp(self._channel, "Input.dispatchTouchEvent",
        {type: "touchStart", touchPoints: [{x: cx, y: cy}]},
        self._sessionId, null, null, null);
    _callCdp(self._channel, "Input.dispatchTouchEvent",
        {type: "touchEnd", touchPoints: []},
        self._sessionId, null, null, null);
};

/* ---------------- Frame parity with Page ----------------
 *
 * Frames don't have their own Page-domain enable, but most of these
 * just delegate to the page (since chrome treats top-level Page calls
 * as applying to the whole tree) — limit to frame scope where it
 * matters (DOM ops, selectors). */

Frame.prototype.goto = function(url, optsOrCb, cb) {
    var opts;
    if (typeof optsOrCb === "function") { cb = optsOrCb; opts = {}; }
    else opts = optsOrCb || {};
    /* CDP doesn't have a per-frame navigate, but Page.navigate accepts
     * frameId.  Page.loadEventFired is still the right wait signal. */
    var waitUntil = opts.waitUntil || "load";
    var evMap = {"load": "Page.loadEventFired",
                 "domcontentloaded": "Page.domContentEventFired"};
    var evMethod = evMap[waitUntil] || null;
    var params = {url: url, frameId: this._frameId};
    if (opts.referrer) params.referrer = opts.referrer;
    return _callCdp(this._channel, "Page.navigate", params,
        this._sessionId, evMethod, evMethod ? this._sessionId : null, cb);
};

Frame.prototype.setContent = function(html, cb) {
    return _callCdp(this._channel, "Page.setDocumentContent",
        {frameId: this._frameId, html: html},
        this._sessionId, null, null, cb);
};

Frame.prototype.waitForNavigation = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var evMap = {"load": "Page.loadEventFired",
                 "domcontentloaded": "Page.domContentEventFired"};
    var evMethod = evMap[opts.waitUntil || "load"] || "Page.loadEventFired";
    return _sendProc(this._channel, "waitForEvent", {
        evMethod:  evMethod,
        evSession: this._sessionId
    }, cb);
};

Frame.prototype.addScriptTag = function(opts, cb) {
    opts = opts || {};
    var src;
    if (opts.url) {
        src = "new Promise(function(res, rej){"
            + "  var s=document.createElement('script');"
            + "  s.src=" + JSON.stringify(opts.url) + ";"
            + (opts.type ? "  s.type=" + JSON.stringify(opts.type) + ";" : "")
            + "  s.onload=function(){res(true);};"
            + "  s.onerror=function(){rej(new Error('addScriptTag: load failed'));};"
            + "  document.head.appendChild(s);"
            + "})";
    } else {
        var content = opts.content;
        if (!content && opts.path) content = utils.readFile(opts.path, true);
        if (!content) throw new Error("addScriptTag: need {url}, {path}, or {content}");
        src = "(function(){"
            + "  var s=document.createElement('script');"
            + (opts.type ? "  s.type=" + JSON.stringify(opts.type) + ";" : "")
            + "  s.text=" + JSON.stringify(content) + ";"
            + "  document.head.appendChild(s);"
            + "  return true;"
            + "})()";
    }
    return this.evaluate(src, cb);
};

Frame.prototype.addStyleTag = function(opts, cb) {
    opts = opts || {};
    var src;
    if (opts.url) {
        src = "new Promise(function(res, rej){"
            + "  var l=document.createElement('link');"
            + "  l.rel='stylesheet'; l.href=" + JSON.stringify(opts.url) + ";"
            + "  l.onload=function(){res(true);};"
            + "  l.onerror=function(){rej(new Error('addStyleTag: load failed'));};"
            + "  document.head.appendChild(l);"
            + "})";
    } else {
        var content = opts.content;
        if (!content && opts.path) content = utils.readFile(opts.path, true);
        if (!content) throw new Error("addStyleTag: need {url}, {path}, or {content}");
        src = "(function(){"
            + "  var s=document.createElement('style');"
            + "  s.appendChild(document.createTextNode(" + JSON.stringify(content) + "));"
            + "  document.head.appendChild(s);"
            + "  return true;"
            + "})()";
    }
    return this.evaluate(src, cb);
};

Frame.prototype.$x = function(xpath, cb) {
    var src = "(function(){"
            + "  var r = document.evaluate(" + JSON.stringify(xpath)
            + ", document, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);"
            + "  var a = [];"
            + "  for (var i=0;i<r.snapshotLength;i++) a.push(r.snapshotItem(i));"
            + "  return a;"
            + "})()";
    var self = this;
    if (typeof cb === "function") {
        _sendProc(self._channel, "frameEval",
            {sessionId: self._sessionId, frameId: self._frameId,
             expression: src, returnByValue: false},
            function(r1, e1) {
                if (e1) return cb(null, e1);
                if (!r1 || !r1.result || !r1.result.objectId) return cb([], null);
                _callCdp(self._channel, "Runtime.getProperties",
                    {objectId: r1.result.objectId, ownProperties: true},
                    self._sessionId, null, null, function(r2, e2) {
                        if (e2) return cb(null, e2);
                        cb(_elemsFromProps(self._page, r2), null);
                    });
            });
        return;
    }
    if (_isTranspiled()) {
        return new Promise(function(res, rej) {
            self.$x(xpath, function(r, err) { err ? rej(err) : res(r); });
        });
    }
    var r1 = _sendProc(self._channel, "frameEval",
        {sessionId: self._sessionId, frameId: self._frameId,
         expression: src, returnByValue: false}, null);
    if (!r1 || !r1.result || !r1.result.objectId) return [];
    var r2 = _callCdp(self._channel, "Runtime.getProperties",
        {objectId: r1.result.objectId, ownProperties: true},
        self._sessionId, null, null, null);
    return _elemsFromProps(self._page, r2);
};

Frame.prototype.waitForXPath = function(xpath, opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var timeout = _timeoutOf(this._page, opts, false);
    var polling = opts.polling || 50;
    var src = "new Promise(function(resolve, reject){"
            + "  var xp = " + JSON.stringify(xpath) + ";"
            + "  var start = Date.now(), t = " + (+timeout) + ", poll = " + (+polling) + ";"
            + "  (function c(){"
            + "    var r = document.evaluate(xp, document, null,"
            + "        XPathResult.FIRST_ORDERED_NODE_TYPE, null);"
            + "    if (r.singleNodeValue) return resolve(true);"
            + "    if (t > 0 && Date.now() - start > t) return reject(new Error('waitForXPath: timeout'));"
            + "    setTimeout(c, poll);"
            + "  })();"
            + "})";
    return this.evaluate(src, cb);
};

Frame.prototype.select = function(selector /* ...values */) {
    var values = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (values.length && typeof values[values.length-1] === "function") cb = values.pop();
    return this.$eval(selector,
        "function(el, values) {"
        + "  if (el.nodeName !== 'SELECT')"
        + "    throw new Error('select: not a <select>');"
        + "  el.value = undefined;"
        + "  var opts = Array.from(el.options);"
        + "  for (var i=0;i<opts.length;i++) {"
        + "    var opt = opts[i];"
        + "    opt.selected = values.indexOf(opt.value) >= 0;"
        + "    if (opt.selected && !el.multiple) break;"
        + "  }"
        + "  el.dispatchEvent(new Event('input',  {bubbles:true}));"
        + "  el.dispatchEvent(new Event('change', {bubbles:true}));"
        + "  return opts.filter(function(o){return o.selected;}).map(function(o){return o.value;});"
        + "}", values, cb);
};

/* Frame.waitForResponse / waitForRequest delegate to the page — there's
 * no per-frame Network.* event in CDP. */
Frame.prototype.waitForResponse = function() {
    return Page.prototype.waitForResponse.apply(this._page, arguments);
};
Frame.prototype.waitForRequest = function() {
    return Page.prototype.waitForRequest.apply(this._page, arguments);
};

/* ---------------- Page event API: off, removeListener, once ----------------
 *
 * The existing page.on(event, handler) registers via rampart.event.on
 * with an opaque generated name.  To support off/once we now store
 * each subscription in a per-page map keyed by (event, originalHandler). */

(function() {
    var origOn = Page.prototype.on;
    Page.prototype.on = function(event, handler) {
        if (!this._evSubs) this._evSubs = [];
        var preCount = (rampart.event && rampart.event.list)
            ? rampart.event.list().length : 0;
        var ret = origOn.call(this, event, handler);
        /* origOn registered the most recent listener with a generated
         * name; we can't easily recover that.  Track the (event, handler)
         * pair so off() can re-subscribe-and-detach via a fresh wrapper.
         * For full off support, we wrap the handler ourselves. */
        return ret;
    };
})();

/* Off / removeListener / once use a parallel internal map: we wrap
 * handlers and remember the wrapper name so off can call rampart.event.off. */
function _pageSubsAdd(page, event, handler, wrapperName, evName) {
    if (!page._evSubs) page._evSubs = [];
    page._evSubs.push({event: event, handler: handler,
                       wrapperName: wrapperName, evName: evName});
}
function _pageSubsRemove(page, event, handler) {
    var arr = page._evSubs || [];
    for (var i = arr.length - 1; i >= 0; i--) {
        var s = arr[i];
        if (s.event === event && (!handler || s.handler === handler)) {
            try { rampart.event.off(s.evName, s.wrapperName); } catch(_) {}
            arr.splice(i, 1);
            if (handler) break;
        }
    }
}

Page.prototype.off = function(event, handler) {
    _pageSubsRemove(this, event, handler);
    return this;
};
Page.prototype.removeListener = Page.prototype.off;

/* ------------------------------------------------------------------ *
 *  Async iteration over page events.
 *
 *  Non-canonical Puppeteer; useful pattern for streaming code:
 *
 *      for await (const req of page.requests())   { ...; if (...) break; }
 *      for await (const res of page.responses())  { ... }
 *      for await (const x   of page.events('console')) { ... }
 *
 *  Pulled from the event bus into a bounded queue.  Each iterator
 *  maintains its own queue + waiter list, so multiple concurrent
 *  iterations of the same event are independent.  `break` /
 *  `iterator.return()` unsubscribes cleanly.  Auto-completes when
 *  the page closes.
 * ------------------------------------------------------------------ */
function _pageEventAsyncIter(page, eventName) {
    var queue   = [];
    var waiters = [];
    var done    = false;

    var handler = function (item) {
        if (done) return;
        if (waiters.length) {
            var w = waiters.shift();
            w({ value: item, done: false });
        } else {
            queue.push(item);
        }
    };
    var closeHandler = function () {
        if (done) return;
        done = true;
        while (waiters.length)
            waiters.shift()({ value: undefined, done: true });
    };
    page.on(eventName, handler);
    /* `close` is dispatched via Inspector.detached or browser shutdown
       — wired through Page.prototype.on already.  Best-effort guard
       against pages that don't expose it. */
    try { page.on("close", closeHandler); } catch (_e) {}

    var iter = {
        next: function () {
            return new Promise(function (res) {
                /* Once `return()` was called, drop the queue and
                   short-circuit to done.  Otherwise the iterator
                   would keep yielding events that were buffered
                   between the last `await` and the `break` — that's
                   correct for live iteration but wrong after a
                   spec-mandated cleanup. */
                if (done)         return res({ value: undefined,     done: true  });
                if (queue.length) return res({ value: queue.shift(), done: false });
                waiters.push(res);
            });
        },
        "return": function () {
            closeHandler();
            queue.length = 0;
            try { page.off(eventName, handler); } catch (_e) {}
            try { page.off("close", closeHandler); } catch (_e) {}
            return Promise.resolve({ value: undefined, done: true });
        },
        "throw": function (e) {
            closeHandler();
            queue.length = 0;
            try { page.off(eventName, handler); } catch (_e) {}
            try { page.off("close", closeHandler); } catch (_e) {}
            return Promise.reject(e);
        }
    };
    /* Self-iterable: `for await (... of iter)` works directly. */
    if (typeof Symbol !== "undefined" && Symbol.asyncIterator)
        iter[Symbol.asyncIterator] = function () { return this; };
    return iter;
}

/* Streaming async-iter over completed requests on this page.
   `for await (const req of page.requests()) { ... }`.  Subscribes
   to `requestfinished` so it fires regardless of whether request
   interception is on.  For pre-completion observation during
   interception, use `page.eventsAsyncIter('request')` and call
   `req.continue()` / `req.respond()` per usual. */
Page.prototype.requests  = function () { return _pageEventAsyncIter(this, "requestfinished"); };

/* Streaming async-iter over responses (fires when Network.responseReceived
   lands — covers both intercepted and natural traffic). */
Page.prototype.responses = function () { return _pageEventAsyncIter(this, "response"); };

/* Generic escape hatch — `page.eventsAsyncIter('console')`,
   `page.eventsAsyncIter('requestfailed')`, anything `page.on(...)`
   accepts. */
Page.prototype.eventsAsyncIter = function (event) { return _pageEventAsyncIter(this, event); };
Page.prototype.once = function(event, handler) {
    var self = this;
    var wrapped = function() {
        self.off(event, wrapped);
        handler.apply(null, arguments);
    };
    return this.on(event, wrapped);
};

/* Replace Page.prototype.on with one that tracks subscriptions and
 * supports the popup/frame/worker/error/requestservedfromcache events
 * the original didn't cover. */
(function() {
    var evMap = {
        "load":             "Page.loadEventFired",
        "domcontentloaded": "Page.domContentEventFired",
        "console":          "Runtime.consoleAPICalled",
        "pageerror":        "Runtime.exceptionThrown",
        "dialog":           "Page.javascriptDialogOpening",
        "response":         "Network.responseReceived",
        "framenavigated":   "Page.frameNavigated",
        "request":          "Fetch.requestPaused",
        "requestfailed":    "Network.loadingFailed",
        "requestfinished":  "Network.loadingFinished",
        "error":            "Inspector.targetCrashed",
        "frameattached":    "Page.frameAttached",
        "framedetached":    "Page.frameDetached",
        "requestservedfromcache": "Network.requestServedFromCache",
        "workercreated":    "Target.attachedToTarget",
        "workerdestroyed":  "Target.detachedFromTarget"
    };
    Page.prototype.on = function(event, handler) {
        if (event === "close") {
            if (!this._closeListeners) this._closeListeners = [];
            this._closeListeners.push(handler);
            _pageSubsAdd(this, event, handler, null, null);
            return this;
        }
        if (event === "popup") {
            /* Popup is browser-level: a new page-type target created with
             * an opener equal to this page's targetId. */
            var self = this;
            var ch = self._channel;
            if (!self._browser._discoverOn) {
                _callCdp(ch, "Target.setDiscoverTargets",
                    {discover: true}, null, null, null, function(){});
                self._browser._discoverOn = true;
            }
            var evName = ch + ".ev..Target.targetCreated";
            var fnName = "popup-" + self._sessionId + "-" + (_tokenSeq++);
            rampart.event.on(evName, fnName, function(uv, payload) {
                var info = (payload.params && payload.params.targetInfo) || {};
                if (info.type !== "page") return;
                if (info.openerId !== self._targetId) return;
                /* Build a Page for it (attaching as needed). */
                _sendProc(ch, "attachExisting", {targetId: info.targetId},
                    function(r, err) {
                        if (err) return;
                        try {
                            var ctx = self._context;
                            if (info.browserContextId && self._browser._contextById[info.browserContextId])
                                ctx = self._browser._contextById[info.browserContextId];
                            var p = new Page(ctx, r.targetId, r.sessionId, r.mainFrameId);
                            handler(p);
                        } catch(e) {}
                    });
            });
            _pageSubsAdd(self, event, handler, fnName, evName);
            return self;
        }
        var cdpMethod = evMap[event];
        if (!cdpMethod) throw new Error("Page.on: unsupported event '" + event + "'");
        /* worker* events come on this page's session because we set
         * autoAttach there; everything else uses the page session too. */
        var self = this;
        if ((event === "workercreated" || event === "workerdestroyed")
            && !self._workersArmed) {
            _callCdp(self._channel, "Target.setAutoAttach",
                {autoAttach: true, waitForDebuggerOnStart: false, flatten: true},
                self._sessionId, null, null, function(){});
            self._workersArmed = true;
        }
        var evName  = self._channel + ".ev." + self._sessionId + "." + cdpMethod;
        var fnName  = "h-" + self._sessionId + "-" + event + "-" + (_tokenSeq++);
        rampart.event.on(evName, fnName, function(uv, payload) {
            try {
                if (event === "workercreated" || event === "workerdestroyed") {
                    var p = payload.params || {};
                    if (event === "workercreated") {
                        var ti = p.targetInfo || {};
                        if (ti.type !== "worker"
                            && ti.type !== "service_worker"
                            && ti.type !== "shared_worker") return;
                        var w = new Worker(self._browser, {
                            sessionId: p.sessionId,
                            targetId:  ti.targetId,
                            url:       ti.url,
                            type:      ti.type
                        });
                        if (!self._workers) self._workers = {};
                        self._workers[p.sessionId] = w;
                        handler(w);
                    } else {
                        var sid = p.sessionId;
                        var w2 = self._workers && self._workers[sid];
                        if (w2) {
                            delete self._workers[sid];
                            handler(w2);
                        }
                    }
                    return;
                }
                _dispatchPageEvent(event, handler, payload, self);
            } catch(e) {
                utils.fprintf(utils.stderr, "rampart-chromeview: handler threw: %s\n", e);
            }
        });
        _pageSubsAdd(self, event, handler, fnName, evName);
        return self;
    };
})();

/* ---------------- Page misc ---------------- */

Page.prototype.queryObjects = function(prototypeHandle, cb) {
    if (!prototypeHandle || !prototypeHandle._objectId)
        throw new Error("queryObjects: pass a JSHandle for the prototype");
    var self = this;
    function unwrap(r) {
        if (!r || !r.objects) return null;
        return _wrapRemote(self, r.objects);
    }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(unwrap(r), null); } : null;
    var raw = _callCdp(self._channel, "Runtime.queryObjects",
        {prototypeObjectId: prototypeHandle._objectId},
        self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Page.prototype.removeExposedFunction = function(name, cb) {
    var self = this;
    /* Unsubscribe our main-side binding handler.  We don't have a
     * Runtime.removeBinding equivalent (it exists in some Chrome
     * versions); fall back to removing our listener and overwriting
     * the page-side wrapper.  Best-effort. */
    if (self._exposedBindings) {
        var keep = [];
        self._exposedBindings.forEach(function(b) {
            if (b.name === name) {
                try { rampart.event.off(b.evName, b.fnName); } catch(_) {}
            } else keep.push(b);
        });
        self._exposedBindings = keep;
    }
    return _callCdp(self._channel, "Runtime.removeBinding",
        {name: name}, self._sessionId, null, null, cb);
};

/* ------------------------------------------------------------------ *
 *  Locator — Puppeteer 19+ auto-waiting selector wrapper.
 *
 *  Locators are immutable handles describing how to find one DOM
 *  node (or a subset of nodes via .first/.last/.nth/.filter).  They
 *  resolve LAZILY: the chain is only walked when an action runs.
 *  All action methods (.click, .fill, .hover, .wait) poll the
 *  selector until an element satisfying the preconditions
 *  (existence + visibility, by default) is found, then act.
 *
 *      const submit = page.locator('form').locator('button[type=submit]');
 *      await submit.click();          // waits, clicks
 *      await submit.setTimeout(5000).wait();
 *
 *      page.locator('.row').first()
 *      page.locator('.row').nth(3)
 *      page.locator('.row').filter(el => el.evaluate(n => n.dataset.active))
 *
 *  Chain semantics: `parent.locator(sel)` scopes the child selector
 *  to descendants of whatever the parent resolves to.
 * ------------------------------------------------------------------ */

function Locator(page, selector, parent) {
    this._page       = page;
    this._selector   = selector;
    this._parent     = parent || null;
    this._timeout    = 30000;
    this._visibility = "visible";   /* "visible" | "hidden" | null */
    this._viewport   = false;
    this._index      = null;        /* null | 0 | -1 | <int> */
    this._filter     = null;
}

function _locClone(loc) {
    var c = new Locator(loc._page, loc._selector, loc._parent);
    c._timeout    = loc._timeout;
    c._visibility = loc._visibility;
    c._viewport   = loc._viewport;
    c._index      = loc._index;
    c._filter     = loc._filter;
    return c;
}

/* Chain into a descendant locator. */
Locator.prototype.locator = function (selector) {
    return new Locator(this._page, selector, this);
};
Locator.prototype.setTimeout = function (ms) {
    var c = _locClone(this); c._timeout = ms; return c;
};
Locator.prototype.setVisibility = function (v) {
    var c = _locClone(this); c._visibility = v; return c;
};
Locator.prototype.setEnsureElementIsInTheViewport = function (b) {
    var c = _locClone(this); c._viewport = !!b; return c;
};
Locator.prototype.first = function () {
    var c = _locClone(this); c._index = 0; return c;
};
Locator.prototype.last = function () {
    var c = _locClone(this); c._index = -1; return c;
};
Locator.prototype.nth = function (n) {
    var c = _locClone(this); c._index = +n; return c;
};
/* `predicate(elementHandle)` -> truthy keeps the candidate.  Async
   predicates (returning a Promise) are awaited.  Order is preserved. */
Locator.prototype.filter = function (predicate) {
    var c = _locClone(this); c._filter = predicate; return c;
};

/* The visibility precondition, evaluated in-page on a candidate. */
var _LOC_VISIBLE_FN =
    "function(el){"
    + "var s = el.ownerDocument && el.ownerDocument.defaultView"
    + "       && el.ownerDocument.defaultView.getComputedStyle(el);"
    + "if (!s) return false;"
    + "if (s.visibility === 'hidden' || s.display === 'none') return false;"
    + "if (el.offsetWidth === 0 && el.offsetHeight === 0"
    + "    && el.getClientRects().length === 0) return false;"
    + "return true;"
    + "}";
var _LOC_VIEWPORT_FN =
    "function(el){"
    + "var r = el.getBoundingClientRect();"
    + "var w = el.ownerDocument && el.ownerDocument.defaultView;"
    + "if (!w) return false;"
    + "return r.bottom > 0 && r.right > 0"
    + "    && r.top    < w.innerHeight && r.left < w.innerWidth;"
    + "}";

/* Resolve the chain to a list of ElementHandle candidates (no
   index/filter/visibility applied yet).  Returns null if a parent in
   the chain doesn't exist — caller's poll-loop tries again. */
function _locResolveCandidates(loc) {
    var page = loc._page;
    if (loc._parent) {
        return _locResolveOne(loc._parent).then(function (parentEl) {
            if (!parentEl) return null;
            return parentEl.evaluateHandle(
                "function(parent,sel){return Array.from(parent.querySelectorAll(sel));}",
                loc._selector
            ).then(function (arrHandle) {
                if (!arrHandle) return [];
                return Promise.resolve(arrHandle.getProperties()).then(function (props) {
                    /* getProperties returns a plain object keyed by
                       prop name (digits for array indices); iterate
                       via Object.keys, not Map.forEach. */
                    var out = [];
                    var keys = Object.keys(props);
                    for (var i = 0; i < keys.length; i++) {
                        var k = keys[i];
                        if (!/^\d+$/.test(k)) continue;
                        var v = props[k];
                        if (v && v.asElement && v.asElement())
                            out.push(v.asElement());
                    }
                    return out;
                });
            });
        });
    }
    return Promise.resolve(page.$$(loc._selector));
}

/* Resolve to a single candidate (index/filter applied), without
   the visibility precondition.  Returns null if none matches. */
function _locResolveOne(loc) {
    return Promise.resolve(_locResolveCandidates(loc)).then(function (cands) {
        if (cands == null) return null;
        var arr = cands;
        if (loc._filter) {
            var kept = [];
            return (function loop(i) {
                if (i >= arr.length) return kept;
                var r = loc._filter(arr[i]);
                return Promise.resolve(r).then(function (ok) {
                    if (ok) kept.push(arr[i]);
                    return loop(i + 1);
                });
            })(0).then(function (filtered) {
                arr = filtered;
                return _pickIndex(arr, loc._index);
            });
        }
        return _pickIndex(arr, loc._index);
    });
}
function _pickIndex(arr, idx) {
    if (!arr || !arr.length) return null;
    if (idx === null || idx === undefined) return arr[0];
    if (idx === -1)   return arr[arr.length - 1];
    if (idx < 0)      return arr[arr.length + idx] || null;
    return arr[idx] || null;
}

/* Poll until the chain resolves to a single element satisfying the
   visibility precondition.  Resolves with that element, or throws
   TimeoutError after `loc._timeout` ms. */
Locator.prototype.waitHandle = function () {
    var loc  = this;
    var page = loc._page;
    var deadline = Date.now() + loc._timeout;
    var pollMs   = 50;
    return new Promise(function (res, rej) {
        (function poll() {
            _locResolveOne(loc).then(function (el) {
                if (!el) return next();
                /* Visibility precondition. */
                var checks = Promise.resolve(true);
                if (loc._visibility === "visible") {
                    checks = checks.then(function () {
                        return el.evaluate(_LOC_VISIBLE_FN);
                    });
                } else if (loc._visibility === "hidden") {
                    checks = checks.then(function () {
                        return el.evaluate(_LOC_VISIBLE_FN).then(function (v) {
                            return !v;
                        });
                    });
                }
                if (loc._viewport) {
                    checks = checks.then(function (ok) {
                        if (!ok) return false;
                        return el.evaluate(_LOC_VIEWPORT_FN);
                    });
                }
                return checks.then(function (ok) {
                    if (ok) return res(el);
                    next();
                });
            }, next);
            function next() {
                if (Date.now() >= deadline)
                    return rej(new TimeoutError("Locator: timed out waiting for "
                        + JSON.stringify(loc._selector)));
                setTimeout(poll, pollMs);
            }
        })();
    });
};

/* `wait()` is `waitHandle().then(()=>{})` — common Puppeteer style. */
Locator.prototype.wait = function () {
    return this.waitHandle().then(function () { return undefined; });
};

/* Map: resolve, then run a page-side function on the matched element. */
Locator.prototype.map = function (fn) {
    return this.waitHandle().then(function (el) {
        return el.evaluate(fn);
    });
};

/* --- action shortcuts: wait + delegate to ElementHandle. --- */
Locator.prototype.click = function (opts) {
    return this.waitHandle().then(function (el) { return el.click(opts); });
};
Locator.prototype.hover = function () {
    return this.waitHandle().then(function (el) { return el.hover(); });
};
Locator.prototype.focus = function () {
    return this.waitHandle().then(function (el) { return el.focus(); });
};
Locator.prototype.scroll = function () {
    return this.waitHandle().then(function (el) {
        return el.evaluate(
            "function(n){n.scrollIntoView({block:'center',inline:'center'});}"
        );
    });
};
/* fill() — set the value on an input/textarea, fire input+change. */
Locator.prototype.fill = function (value) {
    return this.waitHandle().then(function (el) {
        return el.evaluate(
            "function(n,v){"
            + "n.focus();"
            + "var p = Object.getPrototypeOf(n);"
            + "var d = Object.getOwnPropertyDescriptor(p,'value');"
            + "var s = d && d.set;"
            + "if (s) s.call(n, v); else n.value = v;"
            + "n.dispatchEvent(new Event('input',{bubbles:true}));"
            + "n.dispatchEvent(new Event('change',{bubbles:true}));"
            + "}",
            value
        );
    });
};
/* type() — same as fill but appends and dispatches per-char if needed. */
Locator.prototype.type = function (text) {
    return this.waitHandle().then(function (el) { return el.type(text); });
};

/* Page-level entry point. */
Page.prototype.locator = function (selector) {
    return new Locator(this, selector, null);
};

/* ---------------- Worker class ---------------- */

function Worker(browser, info) {
    this._browser   = browser;
    this._channel   = browser._channel;
    this._sessionId = info.sessionId;
    this._targetId  = info.targetId;
    this._url       = info.url || "";
    this._type      = info.type || "worker";
}
Worker.prototype.url      = function() { return this._url; };
Worker.prototype.type     = function() { return this._type; };
Worker.prototype.targetId = function() { return this._targetId; };

Worker.prototype.evaluate = function(fnOrStr /* ...args */) {
    var args = Array.prototype.slice.call(arguments, 1);
    var cb = null;
    if (args.length && typeof args[args.length-1] === "function") cb = args.pop();
    var src = _toExprSource(fnOrStr, args);
    var self = this;
    function unwrap(r) {
        if (!r) return undefined;
        if (r.exceptionDetails) {
            var ed = r.exceptionDetails;
            throw new Error((ed.exception && ed.exception.description) || ed.text || "Worker.evaluate failed");
        }
        return r.result ? r.result.value : undefined;
    }
    var cbw = cb ? function(r, err) {
        if (err) return cb(null, err);
        try { cb(unwrap(r), null); } catch(e) { cb(null, e); }
    } : null;
    var raw = _callCdp(self._channel, "Runtime.evaluate", {
        expression: src,
        returnByValue: true,
        awaitPromise: true
    }, self._sessionId, null, null, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(unwrap);
    return unwrap(raw);
};

Page.prototype.workers = function() {
    if (!this._workersArmed) {
        this._call("Target.setAutoAttach",
            {autoAttach: true, waitForDebuggerOnStart: false, flatten: true},
            null);
        this._workersArmed = true;
    }
    var out = [];
    var m = this._workers || {};
    for (var k in m) out.push(m[k]);
    return out;
};

/* ---------------- page.coverage ---------------- */

function Coverage(page) { this._page = page; }
Coverage.prototype.startJSCoverage = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    function doChain(finish) {
        self._page._call("Profiler.enable", {}, null, function(_, e1) {
            if (e1) return finish(e1);
            self._page._call("Profiler.startPreciseCoverage", {
                callCount:        opts.reportAnonymousScripts ? true : false,
                detailed:         true,
                allowTriggeredUpdates: false
            }, null, function(_, e2) { finish(e2); });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) return new Promise(function(res, rej) {
        doChain(function(err){ err ? rej(err) : res(); });
    });
    this._page._call("Profiler.enable", {}, null);
    this._page._call("Profiler.startPreciseCoverage", {
        callCount: !!opts.reportAnonymousScripts, detailed: true,
        allowTriggeredUpdates: false
    }, null);
};
Coverage.prototype.stopJSCoverage = function(cb) {
    var self = this;
    function doChain(finish) {
        self._page._call("Profiler.takePreciseCoverage", {}, null, function(r, e1) {
            if (e1) return finish(null, e1);
            self._page._call("Profiler.stopPreciseCoverage", {}, null, function(_, e2) {
                if (e2) return finish(null, e2);
                self._page._call("Profiler.disable", {}, null, function() {
                    finish((r && r.result) || [], null);
                });
            });
        });
    }
    if (typeof cb === "function") { doChain(cb); return; }
    if (_isTranspiled()) return new Promise(function(res, rej) {
        doChain(function(r, err){ err ? rej(err) : res(r); });
    });
    var r = self._page._call("Profiler.takePreciseCoverage", {}, null);
    self._page._call("Profiler.stopPreciseCoverage", {}, null);
    self._page._call("Profiler.disable", {}, null);
    return (r && r.result) || [];
};
Coverage.prototype.startCSSCoverage = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    var self = this;
    function doChain(finish) {
        self._page._call("DOM.enable", {}, null, function(_, e1) {
            if (e1) return finish(e1);
            self._page._call("CSS.enable", {}, null, function(_, e2) {
                if (e2) return finish(e2);
                self._page._call("CSS.startRuleUsageTracking", {}, null,
                    function(_, e3) { finish(e3); });
            });
        });
    }
    if (typeof cb === "function") { doChain(function(err){ cb(null, err); }); return; }
    if (_isTranspiled()) return new Promise(function(res, rej) {
        doChain(function(err){ err ? rej(err) : res(); });
    });
    this._page._call("DOM.enable", {}, null);
    this._page._call("CSS.enable", {}, null);
    this._page._call("CSS.startRuleUsageTracking", {}, null);
};
Coverage.prototype.stopCSSCoverage = function(cb) {
    var self = this;
    function doChain(finish) {
        self._page._call("CSS.stopRuleUsageTracking", {}, null, function(r, e1) {
            if (e1) return finish(null, e1);
            self._page._call("CSS.disable", {}, null, function() {
                finish((r && r.ruleUsage) || [], null);
            });
        });
    }
    if (typeof cb === "function") { doChain(cb); return; }
    if (_isTranspiled()) return new Promise(function(res, rej) {
        doChain(function(r, err){ err ? rej(err) : res(r); });
    });
    var r = self._page._call("CSS.stopRuleUsageTracking", {}, null);
    self._page._call("CSS.disable", {}, null);
    return (r && r.ruleUsage) || [];
};

Object.defineProperty(Page.prototype, "coverage", {
    get: function() {
        if (!this._coverage) this._coverage = new Coverage(this);
        return this._coverage;
    }
});

/* ---------------- page.tracing ----------------
 *
 * Tracing.start kicks off a trace, dataCollected events stream events,
 * Tracing.tracingComplete signals end-of-stream.  We accumulate events
 * on the main side via subscriptions on those CDP events. */

function Tracing(page) {
    this._page = page;
    this._active = false;
    this._path = null;
}
Tracing.prototype.start = function(opts, cb) {
    if (typeof opts === "function") { cb = opts; opts = {}; }
    opts = opts || {};
    var self = this;
    if (self._active) throw new Error("Tracing.start: already started");
    self._active = true;
    self._path = opts.path || null;
    var includedCategories = opts.categories || [
        "devtools.timeline", "v8.execute",
        "disabled-by-default-devtools.timeline",
        "disabled-by-default-devtools.timeline.frame",
        "toplevel", "blink.console", "blink.user_timing", "latencyInfo"
    ];
    if (opts.screenshots)
        includedCategories.push("disabled-by-default-devtools.screenshot");
    var params = {
        traceConfig: {
            recordMode: "recordContinuously",
            includedCategories: includedCategories
        }
    };
    return _sendProc(self._page._channel, "traceStart", {
        sessionId: self._page._sessionId,
        params:    params
    }, cb);
};
Tracing.prototype.stop = function(cb) {
    var self = this;
    if (!self._active) throw new Error("Tracing.stop: not started");
    self._active = false;
    function finish(r) {
        var events = (r && r.events) || [];
        var json = JSON.stringify(events);
        if (self._path) utils.fprintf(self._path, "%s", json);
        return utils.bprintf("%s", json);
    }
    var cbw = cb ? function(r, err) { err ? cb(null, err) : cb(finish(r), null); } : null;
    var raw = _sendProc(self._page._channel, "traceStop",
        {sessionId: self._page._sessionId}, cbw);
    if (cbw) return;
    if (raw && typeof raw.then === "function") return raw.then(finish);
    return finish(raw);
};

Object.defineProperty(Page.prototype, "tracing", {
    get: function() {
        if (!this._tracing) this._tracing = new Tracing(this);
        return this._tracing;
    }
});

/* ------------------------------------------------------------------ *
 *  Bi-modal bottom: behave as a module when require()d, or run the
 *  comprehensive self-test suite when invoked directly via
 *      rampart rampart-chromeview.js
 *  (matches the pattern of /usr/local/rampart/modules/rampart-url.js)
 * ------------------------------------------------------------------ */

var _ismod = (typeof module !== "undefined" && module && module.exports);

if (_ismod) {
    module.exports = {
        launch:             launch,
        connect:            connect,
        executablePath:     executablePath,
        defaultArgs:        defaultArgs,
        devices:            _devices,
        networkConditions:  _networkConditions,
        TimeoutError:       TimeoutError,
        /* `errors` namespace alias matching Puppeteer's
           `puppeteer.errors.TimeoutError` layout.  Lets code written
           against the real Puppeteer API (and libraries like
           puppeteer-extra) do `instanceof <pup>.errors.TimeoutError`
           checks against our identity-equal TimeoutError. */
        errors: {
            TimeoutError:   TimeoutError
        },
        Browser:            Browser,
        BrowserContext:     BrowserContext,
        Page:               Page,
        Frame:              Frame,
        Locator:            Locator,
        Target:             Target,
        CDPSession:         CDPSession,
        Worker:             Worker,
        JSHandle:           JSHandle,
        ElementHandle:      ElementHandle,
        Request:            Request,
        Response:           Response,
        Dialog:             Dialog,
        Mouse:              Mouse,
        Keyboard:           Keyboard,
        ConsoleMessage:     ConsoleMessage,
        FileChooser:        FileChooser,
        Accessibility:      Accessibility,
        Coverage:           Coverage,
        Tracing:            Tracing
    };
} else {
    _runSelfTests();
}

function _runSelfTests() {
    rampart.globalize(rampart.utils);

    var chrome_candidates = [
        "/usr/bin/google-chrome-stable",
        "/usr/bin/google-chrome",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
        "/snap/bin/chromium",
        "/usr/local/bin/chrome",        /* FreeBSD: pkg install chromium */
        "/usr/local/bin/chromium",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium"
    ];
    function find_chrome() {
        for (var i = 0; i < chrome_candidates.length; i++)
            if (stat(chrome_candidates[i])) return chrome_candidates[i];
        return null;
    }

    var chrome_bin = find_chrome();
    if (!chrome_bin) {
        printf("chromeview: SKIPPING self-tests (no chrome/chromium found; "
             + "install `chromium` or `google-chrome` to run these tests)\n");
        process.exit(0);
    }

    /* Re-require this file as a module to exercise the public interface
     * exactly as a consumer would.  (Inside that require the _ismod
     * branch runs and just sets module.exports.) */
    var chrome = require(process.script);

    var browser;
    try {
        browser = chrome.launch({headless: true, executablePath: chrome_bin});
    } catch (e) {
        printf("chromeview: SKIPPING self-tests (chrome failed to launch: %s)\n",
            (e && e.message) || e);
        process.exit(0);
    }

    var tmpdir = process.scriptPath + "/tmp-test";
    if (!stat(tmpdir)) mkdir(tmpdir);

    function cleanup() {
        try { browser.close(); } catch(e) {}
        try { shell("rm -rf " + tmpdir); } catch(e) {}
    }

    function testFeature(name, test, error) {
        if (typeof test === "function") {
            try { test = test(); }
            catch (e) { error = e; test = false; }
        }
        printf("testing chromeview - %-54s - ", name);
        fflush(stdout);
        if (test) {
            printf("passed\n");
        } else {
            printf(">>>>> FAILED <<<<<\n");
            if (error !== undefined && error !== null && error !== false)
                printf("  error: %s\n", (error && error.message) || String(error));
            cleanup();
            process.exit(1);
        }
        if (error) console.log(error);
        fflush(stdout);
    }

    /* -------- Module-level checks -------- */

    testFeature("module exports",
        typeof chrome.launch === "function"
        && typeof chrome.connect === "function"
        && typeof chrome.Browser === "function"
        && typeof chrome.BrowserContext === "function"
        && typeof chrome.Page === "function");

    testFeature("browser is a Browser instance",
        browser instanceof chrome.Browser
        && typeof browser.newPage === "function"
        && typeof browser.close === "function"
        && typeof browser.wsEndpoint === "function");

    testFeature("browser.wsEndpoint returns ws:// URL",
        /^ws:\/\//.test(browser.wsEndpoint()));

    /* -------- Pages & basic navigation -------- */

    var page;
    testFeature("browser.newPage returns a Page", function() {
        page = browser.newPage();
        return page instanceof chrome.Page
            && typeof page.goto === "function"
            && typeof page.screenshot === "function";
    });

    testFeature("page.goto with waitUntil=load", function() {
        page.goto("data:text/html,<title>T</title><body><h1>Hello</h1></body>",
                  {waitUntil: "load"});
        return true;
    });

    testFeature("page.title",                page.title() === "T");
    testFeature("page.url",                  /^data:/.test(page.url()));
    testFeature("page.content returns HTML", /<h1>Hello<\/h1>/.test(page.content()));

    testFeature("page.setContent replaces document", function() {
        page.setContent("<title>AFTER</title><body>Replaced</body>");
        return page.title() === "AFTER"
            && /Replaced/.test(page.content());
    });

    testFeature("page.reload re-fires load", function() {
        page.setContent("<title>R</title><script>"
                      + "document.title=document.title+'/'+(++window._n||(window._n=1))"
                      + "</script>");
        var t1 = page.title();
        page.reload();
        var t2 = page.title();
        return t1 !== t2;
    });

    /* -------- evaluate -------- */

    testFeature("page.evaluate returns number", function() {
        page.setContent("<body></body>");
        return page.evaluate("40 + 2") === 42;
    });

    testFeature("page.evaluate returns object", function() {
        var o = page.evaluate("({x:1, y:'a'})");
        return o.x === 1 && o.y === "a";
    });

    testFeature("page.evaluate awaits in-page Promise",
        page.evaluate("new Promise(r => setTimeout(() => r('late'), 100))") === "late");

    testFeature("page.evaluate propagates thrown errors", function() {
        try { page.evaluate("throw new Error('kablooey')"); return false; }
        catch (e) { return /kablooey/.test(e.message); }
    });

    /* -------- Selectors -------- */

    testFeature("page.$$eval maps elements", function() {
        page.setContent("<ul><li>a</li><li>b</li><li>c</li></ul>");
        var arr = page.$$eval("li", "els => els.map(e => e.textContent)");
        return arr.length === 3 && arr[0] === "a" && arr[2] === "c";
    });

    testFeature("page.$eval runs on single element",
        page.$eval("li", "el => el.textContent.toUpperCase()") === "A");

    testFeature("page.$ returns ElementHandle", function() {
        var h = page.$("li");
        var ok = h !== null
              && typeof h.textContent === "function"
              && typeof h.dispose === "function"
              && typeof h.click === "function";
        if (h) h.dispose();
        return ok;
    });

    testFeature("page.$ returns null on no match", function() {
        return page.$("#does-not-exist") === null;
    });

    testFeature("page.$$ returns array of ElementHandles", function() {
        var arr = page.$$("li");
        var ok = Array.isArray(arr) && arr.length === 3
            && typeof arr[1].textContent === "function";
        arr.forEach(function(h){ h.dispose(); });
        return ok;
    });

    /* -------- ElementHandle -------- */

    testFeature("ElementHandle.textContent", function() {
        page.setContent("<p id=t>hello&nbsp;world</p>");
        var h = page.$("#t");
        var t = h.textContent();
        h.dispose();
        return t.indexOf("hello") === 0;
    });

    testFeature("ElementHandle.getAttribute", function() {
        page.setContent('<div id=t data-x="42"></div>');
        var h = page.$("#t");
        var v = h.getAttribute("data-x");
        h.dispose();
        return v === "42";
    });

    testFeature("ElementHandle.boundingBox", function() {
        page.setContent('<div id=t style="width:100px;height:50px">x</div>');
        var h = page.$("#t");
        var box = h.boundingBox();
        h.dispose();
        return box && box.width === 100 && box.height === 50;
    });

    testFeature("ElementHandle.evaluate passes args", function() {
        page.setContent('<div id=t>base</div>');
        var h = page.$("#t");
        var v = h.evaluate("function(el,a,b){return el.textContent+a+b;}", "-", "x");
        h.dispose();
        return v === "base-x";
    });

    testFeature("ElementHandle.dispose is idempotent", function() {
        var h = page.$("body");
        h.dispose();
        h.dispose();
        return true;
    });

    /* -------- Page interaction -------- */

    testFeature("page.click (real mouse events)", function() {
        page.setContent(
            "<html><body><button id=b "
          + "onmousedown=\"document.title='down'\" "
          + "onmouseup=\"document.title+=';up'\">go</button></body></html>");
        page.click("#b");
        return page.title() === "down;up";
    });

    testFeature("page.click {jsClick:true} falls back to el.click()", function() {
        page.setContent("<button id=b onclick=\"document.title='jc'\">x</button>");
        page.click("#b", {jsClick: true});
        return page.title() === "jc";
    });

    testFeature("page.type via real keyboard", function() {
        page.setContent("<input id=i />");
        page.focus("#i");
        page.keyboard.type("abc123");
        return page.$eval("#i", "e => e.value") === "abc123";
    });

    testFeature("page.keyboard.press Enter submits form", function() {
        page.setContent(
            "<form><input id=i /></form>"
          + "<script>document.querySelector('form').addEventListener('submit',"
          + " e => { e.preventDefault(); document.title='submitted'; });</script>");
        page.focus("#i");
        page.keyboard.type("x");
        page.keyboard.press("Enter");
        return page.title() === "submitted";
    });

    testFeature("page.mouse.move + down + up", function() {
        page.setContent(
            "<div id=area style='width:200px;height:200px'></div>"
          + "<script>document.getElementById('area').addEventListener('mousedown',"
          + " e => { document.title = 'md:' + e.clientX + ',' + e.clientY; });</script>");
        page.mouse.move(50, 50);
        page.mouse.down();
        page.mouse.up();
        return /^md:\d+,\d+/.test(page.title());
    });

    /* -------- Wait helpers -------- */

    testFeature("page.waitForSelector eventually finds it", function() {
        page.setContent(
            "<script>setTimeout(() => {"
          + "  const d = document.createElement('div'); d.id='late'; d.textContent='yo';"
          + "  document.body.appendChild(d);"
          + "}, 200);</script>");
        page.waitForSelector("#late", {timeout: 5000});
        return page.$eval("#late", "e=>e.textContent") === "yo";
    });

    testFeature("page.waitForFunction", function() {
        page.setContent("<script>setTimeout(() => window.ready=7, 200);</script>");
        page.waitForFunction("() => window.ready === 7", {timeout: 5000});
        return page.evaluate("window.ready") === 7;
    });

    testFeature("page.waitForTimeout pauses", function() {
        var t0 = Date.now();
        page.waitForTimeout(300);
        return (Date.now() - t0) >= 250;
    });

    testFeature("page.waitForSelector timeout rejects", function() {
        try { page.waitForSelector("#nope", {timeout: 150}); return false; }
        catch (e) { return /timeout/i.test(e.message); }
    });

    /* -------- Viewport / UA / headers -------- */

    testFeature("page.setViewport", function() {
        page.setViewport({width: 1024, height: 768});
        page.setContent("<body></body>");
        var wh = page.evaluate("({w: innerWidth, h: innerHeight})");
        return wh.w === 1024 && wh.h === 768;
    });

    testFeature("page.setUserAgent", function() {
        page.setUserAgent("rampart-chromeview-test/1.0");
        page.setContent("<body></body>");
        return page.evaluate("navigator.userAgent") === "rampart-chromeview-test/1.0";
    });

    testFeature("page.setExtraHTTPHeaders (smoke)", function() {
        page.setExtraHTTPHeaders({"X-Custom": "hello"});
        return true;
    });

    /* -------- Cookies -------- */

    testFeature("page.setCookie and page.cookies", function() {
        page.goto("https://example.com/");
        page.setCookie(
            {name: "cv_a", value: "alpha", domain: "example.com", path: "/"},
            {name: "cv_b", value: "beta",  domain: "example.com", path: "/"}
        );
        var jar = page.cookies();
        var a = jar.filter(function(c){return c.name==="cv_a";})[0];
        var b = jar.filter(function(c){return c.name==="cv_b";})[0];
        return a && a.value === "alpha" && b && b.value === "beta";
    });

    testFeature("page.deleteCookie removes one", function() {
        page.deleteCookie({name: "cv_a", domain: "example.com", path: "/"});
        var jar = page.cookies();
        var a = jar.filter(function(c){return c.name==="cv_a";});
        var b = jar.filter(function(c){return c.name==="cv_b";});
        return a.length === 0 && b.length === 1;
    });

    /* -------- Screenshot & PDF -------- */

    testFeature("page.screenshot returns a PNG buffer", function() {
        page.setViewport({width: 200, height: 100});
        page.setContent("<body style='background:#3050ff;margin:0'>x</body>");
        var png = page.screenshot();
        return png && png.length > 100
            && png[0] === 0x89 && png[1] === 0x50
            && png[2] === 0x4E && png[3] === 0x47;
    });

    testFeature("page.screenshot {fullPage:true, type:jpeg}", function() {
        var jpg = page.screenshot({fullPage: true, type: "jpeg", quality: 50});
        return jpg && jpg.length > 100
            && jpg[0] === 0xFF && jpg[1] === 0xD8 && jpg[2] === 0xFF;
    });

    testFeature("page.screenshot encoding:base64 returns string", function() {
        var b64 = page.screenshot({encoding: "base64"});
        return typeof b64 === "string" && /^iVBORw/.test(b64);
    });

    testFeature("page.pdf returns a PDF buffer", function() {
        var pdf = page.pdf({format: "letter"});
        return pdf && pdf.length > 200
            && String.fromCharCode(pdf[0], pdf[1], pdf[2], pdf[3], pdf[4]) === "%PDF-";
    });

    /* -------- Page events (deferred verification) -------- */

    var evt_console_seen = [];
    var evt_pageerror_seen = [];
    var evt_load_seen = 0;

    testFeature("page.on('console') registers", function() {
        page.on("console", function(m) {
            evt_console_seen.push(m.type() + ":" + m.text());
        });
        return true;
    });

    testFeature("page.on('pageerror') registers", function() {
        page.on("pageerror", function(err) {
            evt_pageerror_seen.push(err.message);
        });
        return true;
    });

    testFeature("page.on('load') registers", function() {
        page.on("load", function() { evt_load_seen++; });
        return true;
    });

    testFeature("fire events (navigate + provoke)", function() {
        page.setContent("<script>console.log('hi','there');throw new Error('ohno');</script>");
        return true;
    });

    /* -------- Frames -------- */

    testFeature("page.frames + frame.$eval", function() {
        var html = "<body><iframe id=a srcdoc=\"<p id=tx>ALPHA</p>\"></iframe>"
                 + "<iframe id=b srcdoc=\"<p id=tx>BETA</p>\"></iframe></body>";
        page.setContent(html);
        page.waitForFunction(
            "() => { var fs=document.querySelectorAll('iframe');"
          + " return fs.length===2 && Array.from(fs).every(function(f){"
          + "   return f.contentDocument && f.contentDocument.querySelector('#tx');}); }",
            {timeout: 5000});
        var frames = page.frames();
        if (frames.length !== 3) return false;
        var childTexts = frames.slice(1).map(function(f) {
            return f.$eval("#tx", "e => e.textContent");
        });
        return childTexts.join(",") === "ALPHA,BETA";
    });

    testFeature("page.mainFrame", function() {
        var mf = page.mainFrame();
        return mf && typeof mf.evaluate === "function"
            && mf.evaluate("1+1") === 2;
    });

    /* -------- Request interception (sync smoke) -------- */

    testFeature("setRequestInterception enable/disable", function() {
        page.setRequestInterception(true);
        page.setRequestInterception(false);
        return true;
    });

    testFeature("page.on('request') registers", function() {
        page.on("request", function(req) { /* exercised in subprocess */ });
        return true;
    });

    /* -------- Browser contexts -------- */

    testFeature("createBrowserContext returns distinct context", function() {
        var ctx = browser.createBrowserContext();
        if (!(ctx instanceof chrome.BrowserContext)) return false;
        var p3 = ctx.newPage();
        p3.goto("https://example.com/");
        p3.setCookie({name:"iso2", value:"ctx1", domain:"example.com", path:"/"});
        var inCtx = p3.cookies().filter(function(c){return c.name==="iso2";})[0];

        var p4 = browser.newPage();
        p4.goto("https://example.com/");
        var inDef = p4.cookies().filter(function(c){return c.name==="iso2";})[0];

        p3.close(); p4.close(); ctx.close();
        return inCtx && inCtx.value === "ctx1" && !inDef;
    });

    /* -------- Callback-mode API (deferred verification) -------- */

    var _cb_final = {};
    testFeature("callback-mode newPage + goto + title (registered)", function() {
        browser.newPage(function(p, err) {
            if (err) { _cb_final.err = err; return; }
            p.goto("data:text/html,<title>CBMODE</title>", function(_, err) {
                if (err) { _cb_final.err = err; p.close(); return; }
                p.title(function(t, err) {
                    if (err) { _cb_final.err = err; p.close(); return; }
                    _cb_final.got_title = t;
                    _cb_final.got_cb    = true;
                    p.close();
                });
            });
        });
        return true;
    });

    /* -------- New API: sync-mode parts --------                       *
     * ElementHandle.screenshot, page.select, page.$x, waitForXPath,
     * Frame.$, Frame.$$, Frame.click, Frame.type, Frame.waitForFunction.
     * Event-driven ones (dialog, waitForNavigation, response,
     * waitForResponse, authenticate) need the main event loop and are
     * covered in the transpiled subprocess below. */

    testFeature("ElementHandle.screenshot (clip to element)", function() {
        page.setViewport({width: 400, height: 300});
        page.setContent("<style>body{margin:0}.b{width:100px;height:50px;"
                      + "background:red;margin:50px}</style><div class=b id=t></div>");
        var h = page.$("#t");
        var png = h.screenshot();
        h.dispose();
        return png && png.length > 50 && png[0] === 0x89 && png[1] === 0x50;
    });

    testFeature("page.select single value", function() {
        page.setContent("<select id=s><option value=a>A</option>"
                      + "<option value=b>B</option><option value=c>C</option></select>");
        var picked = page.select("#s", "b");
        return picked.length === 1 && picked[0] === "b"
            && page.$eval("#s", "e => e.value") === "b";
    });

    testFeature("page.select multi value", function() {
        page.setContent("<select id=s multiple><option value=a>A</option>"
                      + "<option value=b>B</option><option value=c>C</option></select>");
        var picked = page.select("#s", "a", "c");
        picked.sort();
        return picked.join(",") === "a,c";
    });

    testFeature("page.$x returns ElementHandles", function() {
        page.setContent("<h1>A</h1><h1>B</h1><p>not</p>");
        var arr = page.$x("//h1");
        var ok = arr.length === 2
              && arr[0].textContent() === "A" && arr[1].textContent() === "B";
        arr.forEach(function(h){ h.dispose(); });
        return ok;
    });

    testFeature("page.$x empty on no match", function() {
        var arr = page.$x("//doesnotexist");
        return Array.isArray(arr) && arr.length === 0;
    });

    testFeature("page.waitForXPath finds delayed node", function() {
        page.setContent("<script>setTimeout(() => {"
                      + "var d = document.createElement('div'); d.id='late';"
                      + "document.body.appendChild(d);}, 200)</script>");
        page.waitForXPath("//div[@id='late']", {timeout: 3000});
        return page.$eval("div#late", "e => e.id") === "late";
    });

    testFeature("Frame.$ / Frame.$$", function() {
        page.setContent("<body><iframe srcdoc=\"<ul><li>x<li>y<li>z</ul>\"></iframe></body>");
        page.waitForFunction("() => { var f=document.querySelector('iframe');"
            + " return f&&f.contentDocument&&f.contentDocument.querySelectorAll('li').length===3}",
            {timeout: 3000});
        var fr = page.frames()[1];
        var one = fr.$("li");
        var all = fr.$$("li");
        var ok = one && one.textContent() === "x"
              && all.length === 3 && all[2].textContent() === "z";
        if (one) one.dispose();
        all.forEach(function(h){ h.dispose(); });
        return ok;
    });

    testFeature("Frame.click / Frame.type", function() {
        /* Use &quot; inside srcdoc so inner onclick can use "..." */
        var src = "<input id=i><button id=b onclick=&quot;document.title="
                + "document.getElementById('i').value&quot;>go</button>";
        page.setContent("<body><iframe srcdoc=\"" + src + "\"></iframe></body>");
        page.waitForFunction("() => { var f=document.querySelector('iframe');"
            + " return f&&f.contentDocument&&f.contentDocument.getElementById('b'); }",
            {timeout: 3000});
        var fr = page.frames()[1];
        fr.type("#i", "hello");
        fr.click("#b");
        return fr.evaluate("document.title") === "hello";
    });

    testFeature("Frame.waitForFunction", function() {
        var src = "<script>setTimeout(() => { window.fr = 7 }, 150)</script>";
        page.setContent("<body><iframe srcdoc=\"" + src + "\"></iframe></body>");
        page.waitForFunction("() => document.querySelector('iframe') && "
            + "document.querySelector('iframe').contentWindow", {timeout: 3000});
        var fr = page.frames()[1];
        fr.waitForFunction("() => window.fr === 7", {timeout: 3000});
        return fr.evaluate("window.fr") === 7;
    });

    /* -------- Tier 1 additions: sync-mode parts -------- */

    testFeature("chrome.devices contains common phones",
        chrome.devices && chrome.devices["iPhone 12"]
            && chrome.devices["iPhone 12"].viewport.width === 390
            && /Mobile/.test(chrome.devices["iPhone 12"].userAgent));

    testFeature("page.emulate(device) by name", function() {
        var p2 = browser.newPage();
        p2.emulate("iPhone 12");
        /* Need a meta viewport for innerWidth/innerHeight to track device px. */
        p2.setContent("<head><meta name=viewport content='width=device-width'></head>"
                    + "<body></body>");
        var dims = p2.evaluate("({w:innerWidth, h:innerHeight,"
                             + " ua:navigator.userAgent, dpr:devicePixelRatio,"
                             + " mob:navigator.userAgentData ? navigator.userAgentData.mobile : null})");
        p2.close();
        return dims.w === 390 && dims.h === 844
            && /Mobile/.test(dims.ua) && dims.dpr === 3;
    });

    testFeature("page.viewport reflects setViewport", function() {
        page.setViewport({width: 800, height: 600});
        var v = page.viewport();
        return v && v.width === 800 && v.height === 600;
    });

    testFeature("page.emulateMediaType('print')", function() {
        page.setContent("<style>@media print { body { color: red } }</style>"
                      + "<body>hi</body>");
        page.emulateMediaType("print");
        var c = page.evaluate("getComputedStyle(document.body).color");
        page.emulateMediaType("screen");
        return c === "rgb(255, 0, 0)";
    });

    testFeature("page.setGeolocation (smoke)", function() {
        page.setGeolocation({latitude: 37.7749, longitude: -122.4194});
        return true;
    });

    testFeature("page.evaluateOnNewDocument runs before page scripts", function() {
        page.evaluateOnNewDocument("window.__injected = 'pre';");
        /* setDocumentContent doesn't trigger the on-new-document hook;
         * use goto to a fresh URL to force a real document load. */
        page.goto("data:text/html,<script>document.title=window.__injected||'none'</script>");
        return page.title() === "pre";
    });

    testFeature("page.addStyleTag {content} applies", function() {
        page.setContent("<body><p id=t>x</p></body>");
        page.addStyleTag({content: "#t { color: rgb(0, 200, 0) }"});
        return page.evaluate("getComputedStyle(document.getElementById('t')).color")
            === "rgb(0, 200, 0)";
    });

    testFeature("page.addScriptTag {content} runs", function() {
        page.setContent("<body></body>");
        page.addScriptTag({content: "window.tagged = 99"});
        return page.evaluate("window.tagged") === 99;
    });

    testFeature("page.goBack / goForward navigates history", function() {
        page.goto("data:text/html,<title>A</title>");
        page.goto("data:text/html,<title>B</title>");
        page.goBack();
        var t1 = page.title();
        page.goForward();
        var t2 = page.title();
        return t1 === "A" && t2 === "B";
    });

    testFeature("page.setDefaultTimeout affects waitForSelector", function() {
        page.setContent("<body></body>");
        page.setDefaultTimeout(200);
        var t0 = Date.now();
        var caught = false;
        try { page.waitForSelector("#nope"); }
        catch(e) { caught = /timeout/i.test(e.message); }
        var elapsed = Date.now() - t0;
        page.setDefaultTimeout(30000);
        return caught && elapsed >= 150 && elapsed < 1500;
    });

    testFeature("page.setJavaScriptEnabled(false) disables JS on next nav", function() {
        page.setJavaScriptEnabled(false);
        page.goto("data:text/html,<title>X</title><script>document.title='Y'</script>");
        var t = page.title();
        page.setJavaScriptEnabled(true);
        return t === "X";
    });

    testFeature("page.setCacheEnabled / setBypassCSP / setOfflineMode (smoke)", function() {
        page.setCacheEnabled(false);
        page.setCacheEnabled(true);
        page.setBypassCSP(true);
        page.setBypassCSP(false);
        page.setOfflineMode(false);
        return true;
    });

    testFeature("browser.pages returns at least the active page", function() {
        var pages = browser.pages();
        if (!Array.isArray(pages) || pages.length < 1) return false;
        var found = false;
        for (var i = 0; i < pages.length; i++)
            if (pages[i] instanceof chrome.Page) found = true;
        return found;
    });

    testFeature("BrowserContext.pages scoped to default context", function() {
        var defCtx = browser.defaultBrowserContext();
        var pages = defCtx.pages();
        return Array.isArray(pages) && pages.length >= 1;
    });

    testFeature("page.select via ElementHandle.select", function() {
        page.setContent("<select id=s><option value=a>A</option>"
                      + "<option value=b>B</option></select>");
        var h = page.$("#s");
        var picked = h.select("b");
        h.dispose();
        return picked.length === 1 && picked[0] === "b";
    });

    testFeature("ElementHandle.press fires keydown", function() {
        page.setContent(
            "<input id=i autofocus />"
          + "<script>document.getElementById('i').addEventListener('keydown',"
          + " e => { document.title = 'k:' + e.key; });</script>");
        var h = page.$("#i");
        h.press("Enter");
        h.dispose();
        return page.title() === "k:Enter";
    });

    testFeature("ElementHandle.contentFrame for iframe", function() {
        page.setContent("<body><iframe id=f srcdoc=\"<p>inner</p>\"></iframe></body>");
        page.waitForFunction("() => { var f=document.querySelector('iframe');"
            + " return f && f.contentDocument && f.contentDocument.querySelector('p'); }",
            {timeout: 3000});
        var h = page.$("#f");
        var fr = h.contentFrame();
        h.dispose();
        return fr && typeof fr.evaluate === "function"
            && fr.evaluate("document.querySelector('p').textContent") === "inner";
    });

    testFeature("ElementHandle.uploadFile sets file input", function() {
        var fp = tmpdir + "/upload.txt";
        fprintf(fp, "%s", "hello upload");
        page.setContent("<input id=f type=file />");
        var h = page.$("#f");
        h.uploadFile(fp);
        var n = page.$eval("#f", "el => el.files.length");
        var fname = page.$eval("#f", "el => el.files[0].name");
        h.dispose();
        return n === 1 && /upload\.txt$/.test(fname);
    });

    /* Request.response/.failure linkage + requestfailed/requestfinished events
     * are tested in the transpiled-mode subprocess below — they need the main
     * event loop to be running between event arrival and handler invocation. */

    /* -------- Tier 2 additions: sync-mode parts -------- */

    testFeature("page.evaluateHandle returns ElementHandle for DOM nodes", function() {
        page.setContent("<body><div id=t>x</div></body>");
        var h = page.evaluateHandle("document.getElementById('t')");
        var ok = h instanceof chrome.ElementHandle
              && h instanceof chrome.JSHandle
              && h.asElement() === h
              && h.textContent() === "x";
        h.dispose();
        return ok;
    });

    testFeature("page.evaluateHandle returns JSHandle for objects", function() {
        var h = page.evaluateHandle("({a: 1, b: 'two', c: window})");
        var ok = h instanceof chrome.JSHandle
              && !(h instanceof chrome.ElementHandle)
              && h.asElement() === null;
        if (h) h.dispose();
        return ok;
    });

    testFeature("JSHandle.getProperty / getProperties", function() {
        var h = page.evaluateHandle("({x:10, y:20, z:30})");
        var props = h.getProperties();
        var x = h.getProperty("x");
        var ok = props.x && props.y && props.z
              && x.jsonValue() === 10;
        Object.keys(props).forEach(function(k){ props[k].dispose(); });
        x.dispose();
        h.dispose();
        return ok;
    });

    testFeature("page.metrics returns Performance counters", function() {
        page.setContent("<body><h1>x</h1></body>");
        var m = page.metrics();
        return m && typeof m === "object"
            && (typeof m.JSHeapUsedSize === "number"
                || typeof m.Nodes === "number");
    });

    testFeature("page.isClosed reports false for open page", function() {
        return page.isClosed() === false;
    });

    testFeature("page.bringToFront (smoke)", function() {
        page.bringToFront();
        return true;
    });

    testFeature("mouse.reset releases pressed buttons", function() {
        page.mouse.down();
        if (page.mouse._buttons === 0) return false; /* should be pressed */
        page.mouse.reset();
        return page.mouse._buttons === 0;
    });

    testFeature("mouse.dragAndDrop moves through path", function() {
        page.setContent(
            "<div id=t style='width:200px;height:200px'></div>"
          + "<script>"
          + " var s=[];"
          + " document.getElementById('t').addEventListener('mousedown', e=>s.push('d:'+e.clientX));"
          + " document.getElementById('t').addEventListener('mousemove', e=>s.push('m:'+e.clientX));"
          + " document.getElementById('t').addEventListener('mouseup',   e=>s.push('u:'+e.clientX));"
          + " window.events = s;"
          + "</script>");
        page.mouse.dragAndDrop({x:20,y:20}, {x:120,y:20}, {steps: 4});
        var ev = page.evaluate("window.events");
        var hasDown = ev.some(function(e){return /^d:/.test(e);});
        var hasUp   = ev.some(function(e){return /^u:/.test(e);});
        var hasMove = ev.filter(function(e){return /^m:/.test(e);}).length >= 3;
        return hasDown && hasUp && hasMove;
    });

    testFeature("keyboard.reset clears modifiers", function() {
        page.keyboard.down("Shift");
        if (page.keyboard._modifiers === 0) return false;
        page.keyboard.reset();
        return page.keyboard._modifiers === 0;
    });

    testFeature("page.accessibility.snapshot returns role tree", function() {
        page.setContent("<body><h1>Title</h1><button>Click</button></body>");
        var snap = page.accessibility.snapshot();
        if (!snap) return false;
        /* depth-first scan for role=heading and role=button */
        var roles = [];
        (function walk(n) {
            if (!n) return;
            if (n.role) roles.push(n.role);
            if (n.children) n.children.forEach(walk);
        })(snap);
        return roles.indexOf("heading") >= 0 && roles.indexOf("button") >= 0;
    });

    testFeature("BrowserContext.overridePermissions / clearPermissionOverrides", function() {
        var ctx = browser.defaultBrowserContext();
        ctx.overridePermissions("https://example.com", ["geolocation"]);
        ctx.clearPermissionOverrides();
        return true;
    });

    /* -------- Tier 3 additions: sync-mode parts -------- */

    testFeature("chrome.TimeoutError is a typed Error", function() {
        var e = new chrome.TimeoutError("timeout");
        return e instanceof Error
            && (e instanceof chrome.TimeoutError || e.name === "TimeoutError");
    });

    testFeature("chrome.networkConditions has Slow 3G + Fast 4G",
        chrome.networkConditions && chrome.networkConditions["Slow 3G"]
            && chrome.networkConditions["Fast 4G"]
            && typeof chrome.networkConditions["Slow 3G"].downloadThroughput === "number");

    testFeature("chrome.executablePath finds chromium", function() {
        var p = chrome.executablePath();
        return typeof p === "string" && stat(p);
    });

    testFeature("chrome.defaultArgs returns flag list",
        Array.isArray(chrome.defaultArgs()) && chrome.defaultArgs().length > 3
            && chrome.defaultArgs().indexOf("--remote-debugging-port=0") >= 0);

    testFeature("page.emulateTimezone (smoke)", function() {
        page.emulateTimezone("America/Los_Angeles");
        page.emulateTimezone("");
        return true;
    });

    testFeature("page.emulateCPUThrottling (smoke)", function() {
        page.emulateCPUThrottling(4);
        page.emulateCPUThrottling(1);
        return true;
    });

    testFeature("page.emulateNetworkConditions by name", function() {
        page.emulateNetworkConditions("Slow 3G");
        page.emulateNetworkConditions(null);
        return true;
    });

    testFeature("page.emulateVisionDeficiency (smoke)", function() {
        page.emulateVisionDeficiency("achromatopsia");
        page.emulateVisionDeficiency("none");
        return true;
    });

    testFeature("page.emulateMediaFeatures dark mode", function() {
        page.setContent("<style>@media (prefers-color-scheme:dark){body{color:rgb(7,7,7)}}"
                      + "</style><body>x</body>");
        page.emulateMediaFeatures([{name: "prefers-color-scheme", value: "dark"}]);
        var c = page.evaluate("getComputedStyle(document.body).color");
        page.emulateMediaFeatures([]);
        return c === "rgb(7, 7, 7)";
    });

    testFeature("page.target / page.browser / page.browserContext", function() {
        var t = page.target();
        return t instanceof chrome.Target && t.type() === "page"
            && page.browser() === browser
            && page.browserContext() === browser.defaultBrowserContext();
    });

    testFeature("page.target().createCDPSession + session.send", function() {
        var sess = page.target().createCDPSession();
        if (!(sess instanceof chrome.CDPSession)) return false;
        var r = sess.send("Page.getNavigationHistory");
        return r && Array.isArray(r.entries) && typeof r.currentIndex === "number";
    });

    testFeature("browser.version / userAgent", function() {
        var v = browser.version();
        var ua = browser.userAgent();
        return typeof v === "string" && /HeadlessChrome|Chrom/.test(v)
            && typeof ua === "string" && /Chrome/.test(ua);
    });

    testFeature("browser.targets returns Target objects", function() {
        var ts = browser.targets();
        return Array.isArray(ts) && ts.length > 0
            && ts.every(function(t){ return t instanceof chrome.Target; });
    });

    testFeature("browser.isConnected + process", function() {
        return browser.isConnected() === true
            && browser.process() && typeof browser.process().pid === "number";
    });

    testFeature("BrowserContext.isIncognito", function() {
        var defCtx = browser.defaultBrowserContext();
        var inc    = browser.createBrowserContext();
        var ok = defCtx.isIncognito() === false
              && inc.isIncognito() === true;
        inc.close();
        return ok;
    });

    testFeature("ElementHandle.$ / $$ scoped to element", function() {
        page.setContent("<div id=root><span>a</span><span>b</span></div>"
                      + "<span>outside</span>");
        var h = page.$("#root");
        var inner1 = h.$("span");
        var innerAll = h.$$("span");
        var ok = inner1 && inner1.textContent() === "a"
              && innerAll.length === 2
              && innerAll[1].textContent() === "b";
        h.dispose(); if (inner1) inner1.dispose();
        innerAll.forEach(function(x){ x.dispose(); });
        return ok;
    });

    testFeature("ElementHandle.$eval scoped",
        function() {
            page.setContent("<div id=root><p>X</p></div><p>Z</p>");
            var h = page.$("#root");
            var t = h.$eval("p", "el => el.textContent");
            h.dispose();
            return t === "X";
        });

    testFeature("ElementHandle.$x scoped to element", function() {
        page.setContent("<div id=root><a>one</a><a>two</a></div><a>outside</a>");
        var h = page.$("#root");
        var arr = h.$x(".//a");
        var ok = arr.length === 2
              && arr[0].textContent() === "one"
              && arr[1].textContent() === "two";
        arr.forEach(function(x){ x.dispose(); });
        h.dispose();
        return ok;
    });

    testFeature("ElementHandle.isVisible / isHidden", function() {
        page.setContent("<div id=v style='width:10px;height:10px'>v</div>"
                      + "<div id=h style='display:none'>h</div>");
        var vh = page.$("#v"), hh = page.$("#h");
        var ok = vh.isVisible() === true
              && vh.isHidden() === false
              && hh.isVisible() === false
              && hh.isHidden() === true;
        vh.dispose(); hh.dispose();
        return ok;
    });

    testFeature("ElementHandle.scrollIntoView", function() {
        page.setContent("<div style='height:2000px'></div>"
                      + "<div id=bot style='height:20px'>bot</div>");
        var h = page.$("#bot");
        h.scrollIntoView();
        var y = page.evaluate("window.scrollY");
        h.dispose();
        return y > 100;
    });

    testFeature("frame.setContent works on iframe's document", function() {
        page.setContent("<body><iframe id=i src='about:blank'></iframe></body>");
        page.waitForFunction("() => document.querySelector('iframe')"
                           + " && document.querySelector('iframe').contentDocument",
                            {timeout: 3000});
        var f = page.frames()[1];
        f.setContent("<title>FT</title><body>fromFrame</body>");
        return f.evaluate("document.title") === "FT"
            && f.evaluate("document.body.textContent") === "fromFrame";
    });

    testFeature("frame.$x scoped to frame", function() {
        page.setContent("<body><iframe srcdoc=\"<h1>A</h1><h2>B</h2>\"></iframe></body>");
        page.waitForFunction("() => { var f = document.querySelector('iframe');"
            + " return f && f.contentDocument && f.contentDocument.querySelector('h1'); }",
            {timeout: 3000});
        var f = page.frames()[1];
        var hs = f.$x("//h1 | //h2");
        var ok = hs.length === 2
              && hs[0].textContent() === "A"
              && hs[1].textContent() === "B";
        hs.forEach(function(h){ h.dispose(); });
        return ok;
    });

    testFeature("frame.select inside iframe", function() {
        page.setContent("<body><iframe srcdoc=\"<select id=s>"
                      + "<option value=a>A</option><option value=b>B</option>"
                      + "</select>\"></iframe></body>");
        page.waitForFunction("() => { var f = document.querySelector('iframe');"
            + " return f && f.contentDocument && f.contentDocument.querySelector('select'); }",
            {timeout: 3000});
        var f = page.frames()[1];
        var picked = f.select("#s", "b");
        return picked.length === 1 && picked[0] === "b";
    });

    testFeature("page.off removes a listener", function() {
        var hits = 0;
        var h = function() { hits++; };
        page.on("load", h);
        page.off("load", h);
        /* navigate, ensure h didn't fire */
        page.goto("data:text/html,<title>X</title>");
        sleep(0.2);
        return hits === 0;
    });

    testFeature("page.queryObjects via JSHandle prototype", function() {
        page.evaluate("class _Foo {}; window.__inst = [new _Foo(), new _Foo()];");
        var proto = page.evaluateHandle("_Foo.prototype");
        var all = page.queryObjects(proto);
        proto.dispose();
        if (!all) return false;
        /* `all` is a JSHandle for an Array of instances. */
        var n = all.evaluate("function(arr){return arr.length;}");
        all.dispose();
        return n >= 2;
    });

    testFeature("page.tracing start + stop returns trace JSON", function() {
        page.tracing.start({categories: ["devtools.timeline", "v8"]});
        page.goto("data:text/html,<title>TR</title><body>tracing</body>");
        page.evaluate("for (var i=0;i<1000;i++){}");
        var buf = page.tracing.stop();
        var ok = buf && buf.length > 50;
        /* must be a JSON-encoded array */
        if (ok) {
            var s = sprintf("%s", buf);
            ok = s.charAt(0) === "[" && s.charAt(s.length-1) === "]";
        }
        return ok;
    });

    testFeature("page.coverage.startJSCoverage / stopJSCoverage", function() {
        page.coverage.startJSCoverage();
        page.goto("data:text/html,<script>function f(){return 1+1}f();f();</script>");
        var entries = page.coverage.stopJSCoverage();
        return Array.isArray(entries);
    });

    testFeature("page.coverage.startCSSCoverage / stopCSSCoverage", function() {
        page.coverage.startCSSCoverage();
        page.goto("data:text/html,<style>p{color:red}.unused{color:blue}</style><p>hi</p>");
        var entries = page.coverage.stopCSSCoverage();
        return Array.isArray(entries);
    });

    testFeature("HTML5 mouse.dragAndDrop fires dragstart/drop", function() {
        page.setContent(
            "<div id=src draggable=true"
          + " style='width:100px;height:100px;background:red;"
          +        "position:absolute;top:50px;left:50px'"
          + " ondragstart=\"event.dataTransfer.setData('text/plain','HELLO');"
          +              "window._started=true\">src</div>"
          + "<div id=dst"
          + " style='width:100px;height:100px;background:blue;"
          +        "position:absolute;top:50px;left:300px'"
          + " ondragover=\"event.preventDefault()\""
          + " ondrop=\"event.preventDefault();"
          +         "document.title=event.dataTransfer.getData('text/plain')\""
          + ">dst</div>");
        page.setDragInterception(true);
        page.mouse.dragAndDrop({x: 100, y: 100}, {x: 350, y: 100});
        page.setDragInterception(false);
        return page.title() === "HELLO"
            && page.evaluate("window._started") === true;
    });

    testFeature("browser.isConnected flips false after chrome kill (ping/pong)", function() {
        /* Spawn a throwaway browser, SIGKILL chrome, wait for the ws
         * ping/pong watchdog (~30s) to mark the connection dead.
         * Uses isConnected() — which checks the worker's clipboard flag
         * directly — so this works in sync mode without depending on
         * main's event loop running. */
        var b2 = chrome.launch({headless: true});
        var pid = b2.process().pid;
        kill(pid, 9);
        var t0 = Date.now();
        while (b2.isConnected() && Date.now() - t0 < 45000) sleep(0.5);
        var connected = b2.isConnected();
        try { b2.close(); } catch(_) {}
        return connected === false;
    });

    testFeature("ElementHandle.dragAndDrop between handles", function() {
        page.setContent(
            "<div id=a draggable=true"
          + " style='width:80px;height:80px;background:green;"
          +        "position:absolute;top:30px;left:30px'"
          + " ondragstart=\"event.dataTransfer.setData('text/plain','EH');"
          +              "window._eh_start=true\">A</div>"
          + "<div id=b"
          + " style='width:80px;height:80px;background:purple;"
          +        "position:absolute;top:30px;left:250px'"
          + " ondragover=\"event.preventDefault()\""
          + " ondrop=\"event.preventDefault();"
          +         "document.title=event.dataTransfer.getData('text/plain')\""
          + ">B</div>");
        page.setDragInterception(true);
        var a = page.$("#a"), b = page.$("#b");
        a.dragAndDrop(b);
        a.dispose(); b.dispose();
        page.setDragInterception(false);
        return page.title() === "EH";
    });


    /* -------- Transpiled-mode (Promise/await + expose + interception)
     *          via a child rampart process --------                    */

    var subtest = tmpdir + "/_sub.js";

    fprintf(subtest,
    "%s",
      "\"use transpiler\"\n"
    + "rampart.globalize(rampart.utils);\n"
    + "var chrome = require(\"" + process.scriptPath.replace(/"/g,'\\"') + "/rampart-chromeview.js\");\n"
    + "async function main() {\n"
    + "  var b = await chrome.launch({headless:true, executablePath:\"" + chrome_bin + "\"});\n"
    + "\n"
    + "  var p = await b.newPage();\n"
    + "  await p.setContent(\"<title>PM</title><body>P</body>\");\n"
    + "  if ((await p.title()) !== \"PM\") { printf(\"FAIL:title\\n\"); process.exit(2); }\n"
    + "\n"
    + "  await p.exposeFunction(\"sumR\", function(a,b) { return a + b; });\n"
    + "  await p.setContent(\n"
    + "    \"<script>window.addEventListener('load', async () => { \"\n"
    + "    + \"var s = await window.sumR(100, 23); document.title = 'SUM=' + s; \"\n"
    + "    + \"});</script>\");\n"
    + "  await p.waitForFunction(\"() => /^SUM=/.test(document.title)\", {timeout: 5000});\n"
    + "  var t = await p.title();\n"
    + "  if (t !== \"SUM=123\") { printf(\"FAIL:expose got %s\\n\", t); process.exit(2); }\n"
    + "  await p.close();\n"
    + "\n"
    + "  var p2 = await b.newPage();\n"
    + "  await p2.setRequestInterception(true);\n"
    + "  var seen = 0;\n"
    + "  p2.on(\"request\", function(req) {\n"
    + "    seen++;\n"
    + "    var url = req.url();\n"
    + "    if (/block\\.example/.test(url)) { req.abort(\"BlockedByClient\"); return; }\n"
    + "    if (/mock\\.example\\/data/.test(url)) {\n"
    + "      req.respond({status:200,\n"
    + "        headers:{\"Content-Type\":\"text/plain\",\n"
    + "                 \"Access-Control-Allow-Origin\":\"*\"},\n"
    + "        body:\"MOCKED\"});\n"
    + "      return;\n"
    + "    }\n"
    + "    req.continue();\n"
    + "  });\n"
    + "  await p2.setContent(\n"
    + "    \"<div id=out></div><script>(async function(){\"\n"
    + "    + \"var r=[];\"\n"
    + "    + \"try{var m=await fetch('https://mock.example/data');r.push('m:'+(await m.text()));}\"\n"
    + "    + \"catch(e){r.push('merr:'+e.message);}\"\n"
    + "    + \"try{var bl=await fetch('https://block.example/x');r.push('b:'+bl.status);}\"\n"
    + "    + \"catch(e){r.push('berr:'+e.message);}\"\n"
    + "    + \"document.getElementById('out').textContent=r.join('|');\"\n"
    + "    + \"document.title='done';\"\n"
    + "    + \"})();</script>\");\n"
    + "  await p2.waitForFunction(\"() => document.title === 'done'\", {timeout: 5000});\n"
    + "  var out = await p2.$eval(\"#out\", \"e => e.textContent\");\n"
    + "  if (!/m:MOCKED/.test(out) || !/berr:/.test(out)) {\n"
    + "    printf(\"FAIL:intercept got '%s' seen=%d\\n\", out, seen);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 4) Dialog.accept() resolves the in-page confirm() */\n"
    + "  var p3 = await b.newPage();\n"
    + "  var dialogSeen = null;\n"
    + "  p3.on(\"dialog\", async function(d) {\n"
    + "    dialogSeen = {type: d.type(), message: d.message()};\n"
    + "    await d.accept();\n"
    + "  });\n"
    + "  await p3.goto(\"data:text/html,<title>D</title>\");\n"
    + "  await p3.evaluate(\"document.title = (confirm('ok?') ? 'YES' : 'NO')\");\n"
    + "  if ((await p3.title()) !== \"YES\") {\n"
    + "    printf(\"FAIL:dialog got title=%s seen=%J\\n\", await p3.title(), dialogSeen);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 5) waitForNavigation: install waiter before triggering nav.\n"
    + "   * Start from about:blank — chrome blocks data: -> data: navs. */\n"
    + "  await p3.goto(\"about:blank\");\n"
    + "  var navP = p3.waitForNavigation({timeout: 5000});\n"
    + "  /* fire-and-forget second goto; navP catches its load event */\n"
    + "  p3.goto(\"data:text/html,<title>LANDED</title>\");\n"
    + "  await navP;\n"
    + "  if ((await p3.title()) !== \"LANDED\") {\n"
    + "    printf(\"FAIL:nav got title=%s\\n\", await p3.title());\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 6) page.on(\"response\") fires for each loaded resource */\n"
    + "  var responses = [];\n"
    + "  p3.on(\"response\", function(resp) { responses.push(resp.url()); });\n"
    + "  await p3.goto(\"data:text/html,<title>R</title>\");\n"
    + "  /* data: goto may not trigger a Network.responseReceived (no wire request);\n"
    + "   * force an in-page fetch to a real-ish URL that will be mocked below. */\n"
    + "\n"
    + "  /* 7) waitForResponse matches url substring */\n"
    + "  var p4 = await b.newPage();\n"
    + "  await p4.setRequestInterception(true);\n"
    + "  p4.on(\"request\", function(req) {\n"
    + "    if (/mockedWR/.test(req.url())) {\n"
    + "      req.respond({status:200,\n"
    + "        headers:{\"Content-Type\":\"text/plain\",\n"
    + "                 \"Access-Control-Allow-Origin\":\"*\"},\n"
    + "        body:\"WR-OK\"});\n"
    + "      return;\n"
    + "    }\n"
    + "    req.continue();\n"
    + "  });\n"
    + "  await p4.goto(\"data:text/html,<title>W</title>\");\n"
    + "  var respWP = p4.waitForResponse(\"mockedWR\", {timeout: 5000});\n"
    + "  await p4.evaluate(\"fetch('https://example.com/mockedWR').then(r=>r.text())\");\n"
    + "  var resp = await respWP;\n"
    + "  if (!resp || resp.status() !== 200 || !/mockedWR/.test(resp.url())) {\n"
    + "    printf(\"FAIL:waitForResponse resp=%J\\n\", resp && {u:resp.url(),s:resp.status()});\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  var body = await resp.text();\n"
    + "  if (body !== \"WR-OK\") {\n"
    + "    printf(\"FAIL:response.text got '%s'\\n\", body);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 8) ConsoleMessage shape: .type(), .text(), .args(), .location() */\n"
    + "  var p5 = await b.newPage();\n"
    + "  var consoleSeen = null;\n"
    + "  p5.on(\"console\", function(m) {\n"
    + "    consoleSeen = {type: m.type(), text: m.text(), nargs: m.args().length,\n"
    + "                   url: m.location().url};\n"
    + "  });\n"
    + "  await p5.goto(\"data:text/html,<script>console.warn('hi','there')</script>\");\n"
    + "  await p5.waitForFunction(\"() => true\", {timeout: 1000});\n"
    + "  if (!consoleSeen || consoleSeen.type !== \"warning\"\n"
    + "      || !/hi there/.test(consoleSeen.text) || consoleSeen.nargs !== 2) {\n"
    + "    printf(\"FAIL:console %J\\n\", consoleSeen);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 9) requestfailed + Request.failure linkage */\n"
    + "  var p6 = await b.newPage();\n"
    + "  await p6.setRequestInterception(true);\n"
    + "  var failSeen = null;\n"
    + "  p6.on(\"requestfailed\", function(req) {\n"
    + "    if (/blockme/.test(req.url()))\n"
    + "      failSeen = {url: req.url(), err: req.failure() && req.failure().errorText};\n"
    + "  });\n"
    + "  p6.on(\"request\", function(req) {\n"
    + "    if (/blockme/.test(req.url())) req.abort(\"BlockedByClient\");\n"
    + "    else req.continue();\n"
    + "  });\n"
    + "  await p6.setContent(\n"
    + "    \"<script>fetch('https://example.com/blockme').catch(()=>\"\n"
    + "    + \"{document.title='blocked';});</script>\");\n"
    + "  await p6.waitForFunction(\"() => document.title === 'blocked'\", {timeout: 5000});\n"
    + "  if (!failSeen || !/blockme/.test(failSeen.url) || !/Blocked/i.test(failSeen.err || \"\")) {\n"
    + "    printf(\"FAIL:requestfailed seen=%J\\n\", failSeen);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 10) goBack/goForward */\n"
    + "  var p7 = await b.newPage();\n"
    + "  await p7.goto(\"data:text/html,<title>A1</title>\");\n"
    + "  await p7.goto(\"data:text/html,<title>B1</title>\");\n"
    + "  await p7.goBack();\n"
    + "  if ((await p7.title()) !== \"A1\") { printf(\"FAIL:goBack got %s\\n\", await p7.title()); process.exit(2); }\n"
    + "  await p7.goForward();\n"
    + "  if ((await p7.title()) !== \"B1\") { printf(\"FAIL:goForward got %s\\n\", await p7.title()); process.exit(2); }\n"
    + "\n"
    + "  /* 11) request.response()/response.request() cross-linkage */\n"
    + "  var p8 = await b.newPage();\n"
    + "  await p8.setRequestInterception(true);\n"
    + "  p8.on(\"request\", function(req) {\n"
    + "    if (/linkme/.test(req.url())) {\n"
    + "      req.respond({status:200,\n"
    + "        headers:{\"Content-Type\":\"text/plain\",\n"
    + "                 \"Access-Control-Allow-Origin\":\"*\"},\n"
    + "        body:\"LINKED\"});\n"
    + "      return;\n"
    + "    }\n"
    + "    req.continue();\n"
    + "  });\n"
    + "  await p8.goto(\"data:text/html,<title>L</title>\");\n"
    + "  var linkRP = p8.waitForResponse(\"linkme\", {timeout: 5000});\n"
    + "  await p8.evaluate(\"fetch('https://example.com/linkme').then(r=>r.text())\");\n"
    + "  var linkResp = await linkRP;\n"
    + "  var linkReq = linkResp.request();\n"
    + "  if (!linkReq || !/linkme/.test(linkReq.url())) {\n"
    + "    printf(\"FAIL:resp.request() missing — got %J\\n\", linkReq && linkReq.url());\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  if (linkReq.response() !== linkResp) {\n"
    + "    printf(\"FAIL:req.response() linkage missing\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 12) waitForRequest matches url substring */\n"
    + "  var p9 = await b.newPage();\n"
    + "  await p9.goto(\"data:text/html,<title>WR</title>\");\n"
    + "  var reqWP = p9.waitForRequest(\"watchme\", {timeout: 5000});\n"
    + "  await p9.evaluate(\"fetch('https://example.com/watchme').catch(()=>{})\");\n"
    + "  var seenReq = await reqWP;\n"
    + "  if (!seenReq || !/watchme/.test(seenReq.url())) {\n"
    + "    printf(\"FAIL:waitForRequest got %J\\n\", seenReq && seenReq.url());\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 13) waitForFileChooser — chrome requires a real user gesture\n"
    + "   * for file inputs, so we click via the CDP Input domain. */\n"
    + "  var p10 = await b.newPage();\n"
    + "  await p10.setContent(\n"
    + "    \"<body><input id=f type=file multiple\"\n"
    + "    + \" style='position:absolute;top:50px;left:50px;width:200px;height:30px'>\"\n"
    + "    + \"</body>\");\n"
    + "  var chP = p10.waitForFileChooser({timeout: 5000});\n"
    + "  await p10.click(\"#f\");\n"
    + "  var chooser = await chP;\n"
    + "  if (!chooser || chooser.isMultiple() !== true) {\n"
    + "    printf(\"FAIL:waitForFileChooser got %J\\n\", chooser && {m:chooser.isMultiple()});\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  await chooser.cancel();\n"
    + "\n"
    + "  /* 14) page.on('close') synthetic event fires on close() */\n"
    + "  var p11 = await b.newPage();\n"
    + "  var closedSeen = false;\n"
    + "  p11.on(\"close\", function(){ closedSeen = true; });\n"
    + "  await p11.close();\n"
    + "  if (!closedSeen || !p11.isClosed()) {\n"
    + "    printf(\"FAIL:close-event closedSeen=%s isClosed=%s\\n\", closedSeen, p11.isClosed());\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 15) page.on('popup') fires for window.open */\n"
    + "  var p12 = await b.newPage();\n"
    + "  var popup = null;\n"
    + "  p12.on(\"popup\", function(np) { popup = np; });\n"
    + "  await p12.goto(\"about:blank\");\n"
    + "  await p12.evaluate(\"window.open('about:blank?popup', '_blank'); 1\");\n"
    + "  /* allow time for Target.targetCreated + attachExisting */\n"
    + "  for (var i = 0; i < 50 && !popup; i++) await p12.waitForTimeout(50);\n"
    + "  if (!popup || !(popup instanceof chrome.Page)) {\n"
    + "    printf(\"FAIL:popup not delivered\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 16) frameattached / framedetached events */\n"
    + "  var p13 = await b.newPage();\n"
    + "  var frameAttached = 0, frameDetached = 0;\n"
    + "  p13.on(\"frameattached\", function(){ frameAttached++; });\n"
    + "  p13.on(\"framedetached\", function(){ frameDetached++; });\n"
    + "  await p13.setContent(\"<body></body>\");\n"
    + "  await p13.evaluate(\"var f = document.createElement('iframe'); f.id='f1';\"\n"
    + "    + \"f.src='about:blank'; document.body.appendChild(f);\");\n"
    + "  await p13.waitForFunction(\"() => !!document.getElementById('f1')\", {timeout: 3000});\n"
    + "  await p13.waitForTimeout(100);\n"
    + "  await p13.evaluate(\"document.getElementById('f1').remove();\");\n"
    + "  await p13.waitForTimeout(150);\n"
    + "  if (frameAttached < 1 || frameDetached < 1) {\n"
    + "    printf(\"FAIL:frame events attached=%d detached=%d\\n\", frameAttached, frameDetached);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 17) Response.fromCache / .timing / .securityDetails surfaces */\n"
    + "  var p14 = await b.newPage();\n"
    + "  await p14.setRequestInterception(true);\n"
    + "  p14.on(\"request\", function(req) {\n"
    + "    if (/respfields/.test(req.url())) {\n"
    + "      req.respond({status: 200,\n"
    + "        headers: {\"Content-Type\":\"text/plain\",\n"
    + "                  \"Access-Control-Allow-Origin\":\"*\"},\n"
    + "        body: \"x\"});\n"
    + "      return;\n"
    + "    }\n"
    + "    req.continue();\n"
    + "  });\n"
    + "  await p14.goto(\"data:text/html,<title>RF</title>\");\n"
    + "  var rfP = p14.waitForResponse(\"respfields\", {timeout: 5000});\n"
    + "  await p14.evaluate(\"fetch('https://example.com/respfields').then(r=>r.text())\");\n"
    + "  var rf = await rfP;\n"
    + "  if (rf.fromCache() !== false || rf.fromServiceWorker() !== false) {\n"
    + "    printf(\"FAIL:fromCache/fromSW shape\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  if (!rf.timing()) printf(\"  note: timing() is null for synthesized response (expected)\\n\");\n"
    + "\n"
    + "  /* 18) Browser.on('targetcreated' / 'targetdestroyed') */\n"
    + "  var created = 0, destroyed = 0;\n"
    + "  b.on(\"targetcreated\",   function(){ created++; });\n"
    + "  b.on(\"targetdestroyed\", function(){ destroyed++; });\n"
    + "  var p15 = await b.newPage();\n"
    + "  await p15.evaluate(\"new Promise(r => setTimeout(r, 150))\");\n"
    + "  await p15.close();\n"
    + "  /* p15 is gone; use another page for the wait window */\n"
    + "  await p14.evaluate(\"new Promise(r => setTimeout(r, 150))\");\n"
    + "  /* multiple page targets get created (the new page + any internals),\n"
    + "   * just check we saw at least one of each. */\n"
    + "  if (created < 1 || destroyed < 1) {\n"
    + "    printf(\"FAIL:targetcreated=%d targetdestroyed=%d\\n\", created, destroyed);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 19) Worker discovery via page.on('workercreated') + page.workers() */\n"
    + "  var p16 = await b.newPage();\n"
    + "  var workerSeen = null;\n"
    + "  p16.on(\"workercreated\", function(w) { workerSeen = w; });\n"
    + "  await p16.setContent(\n"
    + "    \"<script>var blob = new Blob([\\\"self.onmessage=()=>self.postMessage(1)\\\"],\"\n"
    + "    + \"{type:'application/javascript'});\"\n"
    + "    + \"window._w = new Worker(URL.createObjectURL(blob));</script>\");\n"
    + "  for (var i = 0; i < 50 && !workerSeen; i++) await p16.waitForTimeout(50);\n"
    + "  if (!workerSeen || !(workerSeen instanceof chrome.Worker)) {\n"
    + "    printf(\"FAIL:workercreated not delivered\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  /* page.workers() should now list it */\n"
    + "  var ws = p16.workers();\n"
    + "  if (ws.length < 1) {\n"
    + "    printf(\"FAIL:page.workers() empty\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "  var wval = await workerSeen.evaluate(\"40 + 2\");\n"
    + "  if (wval !== 42) {\n"
    + "    printf(\"FAIL:worker.evaluate got %J\\n\", wval);\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  /* 20) Browser.on('disconnected') fires on Browser.close. */\n"
    + "  var b3 = await chrome.launch({headless: true});\n"
    + "  var dcSeen = false;\n"
    + "  b3.on(\"disconnected\", function(){ dcSeen = true; });\n"
    + "  await b3.close();\n"
    + "  /* allow the event to flush */\n"
    + "  await new Promise(function(r){ setTimeout(r, 100); });\n"
    + "  if (!dcSeen) {\n"
    + "    printf(\"FAIL:disconnected on close not delivered\\n\");\n"
    + "    process.exit(2);\n"
    + "  }\n"
    + "\n"
    + "  await b.close();\n"
    + "  printf(\"SUBOK\\n\");\n"
    + "}\n"
    + "main().catch(function(e){ printf(\"FAIL:caught %s\\n\", e && e.message || e); process.exit(2); });\n"
    );

    testFeature("transpiled-mode: Promise + expose + intercept + dialog + waitForNav + waitForResp",
        function() {
            var r = shell("rampart -t " + subtest + " 2>&1");
            if (!(r.exitStatus === 0 && /SUBOK/.test(r.stdout))) {
                printf("\n  subprocess output:\n%s\n", r.stdout);
                return false;
            }
            return true;
        });

    testFeature("page.close", function() { page.close(); return true; });

    /* Deferred checks -- console/pageerror/load handlers and callback-mode
     * callbacks only fire once top-level returns and the event loop runs. */
    function finalize() {
        testFeature("page.on('console') fired",   evt_console_seen.length >= 1);
        testFeature("page.on('pageerror') fired", evt_pageerror_seen.length >= 1);
        testFeature("page.on('load') fired",      evt_load_seen >= 1);

        testFeature("callback-mode completed",    _cb_final.got_cb === true);
        testFeature("callback-mode title",        _cb_final.got_title === "CBMODE");

        testFeature("browser.close shuts down cleanly",
            function() { browser.close(); return true; });

        try { shell("rm -rf " + tmpdir); } catch(e) {}
        printf("chromeview: all self-tests passed.\n");
    }

    function waitForCb() {
        if (_cb_final.got_cb || _cb_final.err) { finalize(); return; }
        setTimeout(waitForCb, 100);
    }
    setTimeout(waitForCb, 200);
}
