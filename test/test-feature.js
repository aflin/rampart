//first line
/* Shared test harness for rampart's test/*-test.js files.
 *
 * Usage:
 *
 *     var testFeature = new (require('./test-feature.js'))({
 *         prefix:    "intl",         // required; appears as "testing <prefix> - ..."
 *         allowNode: true,           // optional; default false.  If true, the
 *                                    // test is dual-mode (runs under both
 *                                    // rampart and node); the printf/fflush/
 *                                    // stdout globals get shimmed under node
 *                                    // and the label becomes "<prefix>(node)".
 *                                    // If false and we're under node, the
 *                                    // script prints a one-line skip notice
 *                                    // and exit(0)s.
 *         width:     100,            // optional; default 100.  Total max line
 *                                    // width (incl. ">>>>> FAILED <<<<<").  The
 *                                    // <name> argument is padded — or truncated
 *                                    // with "…" — to make every line that wide.
 *         onFail:    fn(name, err)   // optional callback fired on each failure
 *                                    // (lets a test file bail with cleanup +
 *                                    // process.exit without a wrapper).
 *     });
 *
 *     testFeature("first thing", function() {
 *         testFeature.must(1 === 1, "math");
 *         testFeature.mustEq(2+2, 4, "add");
 *     });
 *     // ... more testFeature calls ...
 *     testFeature.exit();
 *
 * Each call returns true (passed) or false (failed).
 *
 * Thread compatibility: child threads spawned via rampart.thread.exec()
 * copy globals at thread-creation time, but DO NOT carry function closures.
 * So `testFeature` is a global function with no closure dependencies —
 * all its config (label, width, etc.) lives on `global._tfState`, which IS
 * copied to the child.  The thread can call testFeature directly and lines
 * land on the same stdout stream the main thread writes to.
 *
 * The `onFail` callback won't survive across the thread boundary (functions
 * lose closure environment); thread failures still print FAILED but won't
 * trigger the main-thread bail.
 */

/* Global testFeature function.  Looks up everything via `global._tfState`
   (a plain object that survives thread.exec).  No closure references. */
function testFeature(name, test) {
    var cfg = global._tfState;
    if (!cfg) throw new Error("test-feature: not initialized — call `new TestFeature(...)` first");

    var FAILED_TEXT    = ">>>>> FAILED <<<<<";
    var label          = cfg.label;
    var width          = cfg.width;
    var FIXED_OVERHEAD = 8 /* "testing " */ + label.length + 3 + 3;
    var MAX_NAME_W     = width - FIXED_OVERHEAD - FAILED_TEXT.length;
    if (MAX_NAME_W < 8) MAX_NAME_W = 8;

    var err = null, ok = false;
    try {
        var r = (typeof test === 'function') ? test() : test;
        /* Historical contract from sql-test/crypto-test/etc.: any truthy
           return passes; `undefined` also passes (tests that assert via
           throwing don't need to return).  Only falsy values fail. */
        ok = (r === undefined) || !!r;
    } catch (e) {
        err = e;
    }

    var status = ok ? "passed" : FAILED_TEXT;
    var s = String(name);
    if (s.length > MAX_NAME_W) s = s.substring(0, MAX_NAME_W - 1) + '…';
    var target = width - FIXED_OVERHEAD - status.length;
    if (target < 1) target = 1;
    while (s.length < target) s += ' ';
    printf("testing %s - %s - %s\n", label, s, status);
    if (!ok && err)
        printf("    %s\n", (err && err.message) || String(err));
    if (!ok && cfg.onFail) cfg.onFail(name, err);
    return ok;
}

/* Skip helper.  Padding sized for the actual tag width so the dash sits
   next to the status text the same way passed/FAILED do. */
testFeature.skip = function(name, why) {
    var cfg = global._tfState;
    if (!cfg) throw new Error("test-feature: not initialized");
    var tag = why ? ("skipped (" + why + ")") : "skipped";
    var label = cfg.label, width = cfg.width;
    var FIXED_OVERHEAD = 8 + label.length + 3 + 3;
    var MAX_NAME_W = width - FIXED_OVERHEAD - 18;
    if (MAX_NAME_W < 8) MAX_NAME_W = 8;
    var s = String(name);
    if (s.length > MAX_NAME_W) s = s.substring(0, MAX_NAME_W - 1) + '…';
    var target = width - FIXED_OVERHEAD - tag.length;
    if (target < 1) target = 1;
    while (s.length < target) s += ' ';
    printf("testing %s - %s - %s\n", label, s, tag);
};

/* ----- assertion helpers ----- */
testFeature.must = function(cond, lbl) {
    if (!cond) throw new Error(lbl);
};
testFeature.mustEq = function(got, want, lbl) {
    if (got !== want) {
        if (typeof got === 'object' && typeof want === 'object'
            && JSON.stringify(got) === JSON.stringify(want)) return;
        throw new Error(lbl + ': got ' + JSON.stringify(got)
                            + ', want ' + JSON.stringify(want));
    }
};
testFeature.mustThrow = function(fn, ctor, lbl) {
    /* Two-arg form: (fn, label).  Three-arg form: (fn, ErrorCtor, label). */
    if (typeof ctor === 'string') { lbl = ctor; ctor = null; }
    var threw = false, got = null;
    try { fn(); } catch (e) { threw = true; got = e; }
    if (!threw) throw new Error(lbl + ': no throw');
    if (ctor && !(got instanceof ctor))
        throw new Error(lbl + ': wrong error type: ' + (got && got.name));
};
testFeature.mustContain = function(haystack, needle, lbl) {
    if (String(haystack).indexOf(needle) < 0)
        throw new Error(lbl + ': "' + haystack + '" missing "' + needle + '"');
};

testFeature.exit = function() {
    if (typeof process !== 'undefined' && process.exit) process.exit(0);
};

/* Constructor.  Initializes `global._tfState` and `global.testFeature`
   (so child threads inherit both via thread.exec's globals copy). */
function TestFeature(opts) {
    if (!opts || typeof opts.prefix !== 'string' || !opts.prefix.length)
        throw new Error("test-feature: opts.prefix is required");

    var prefix    = opts.prefix;
    var allowNode = !!opts.allowNode;
    var width     = (typeof opts.width === 'number' && opts.width > 0)
                        ? opts.width : 100;
    var onFail    = (typeof opts.onFail === 'function') ? opts.onFail : null;

    var _isRampart = (typeof rampart !== 'undefined' && rampart && rampart.utils);

    /* Rampart-only test under node: print one-line skip notice and exit 0. */
    if (!_isRampart && !allowNode) {
        var msg = "test '" + prefix + "' is rampart-only; skipping under node\n";
        if (typeof process !== 'undefined' && process.stderr)
            process.stderr.write(msg);
        if (typeof process !== 'undefined' && process.exit)
            process.exit(0);
        return function(){};
    }

    if (_isRampart) {
        if (typeof global.printf !== 'function')
            rampart.globalize(rampart.utils);
    } else {
        global.stdout = process.stdout;
        global.fflush = function() {};
        global.printf = function(fmt) {
            var args = Array.prototype.slice.call(arguments, 1);
            /* Format codes: %s, %d, %j, %%, with optional `-` left-flag
               and width specified as digits or `*` (taken from args).
               So %-NNs / %NNs / %*s / %-*s are all supported. */
            var out = String(fmt).replace(/%(-?)(\d+|\*|)([sdj%])/g, function(m, sign, w, t) {
                if (t === '%') return '%';
                var pad = 0;
                if (w === '*')      pad = parseInt(args.shift(), 10) || 0;
                else if (w !== '')  pad = parseInt(w, 10) || 0;
                var v = args.shift();
                var s;
                if (t === 'j')      s = JSON.stringify(v);
                else if (t === 'd') s = String(Math.trunc(Number(v)));
                else                s = String(v);
                if (sign === '-') {
                    while (s.length < pad) s += ' ';
                } else if (pad > 0) {
                    while (s.length < pad) s = ' ' + s;
                }
                return s;
            });
            process.stdout.write(out);
        };
    }

    global._tfState = {
        label:     _isRampart ? prefix : (prefix + "(node)"),
        width:     width,
        isRampart: _isRampart,
        /* onFail is a function — it does NOT survive .exec() into a worker
           thread (closures get stripped).  That's intentional: only the
           main thread can sensibly cleanup + process.exit. */
        onFail:    onFail
    };
    /* Do NOT set `global.testFeature = testFeature` here.  Callers that
       want thread support do `var testFeature = new TestFeature(...)`
       at top level with "use transpilerGlobally", which itself puts the
       binding on global (top-level var → global under that directive).
       Overwriting global.testFeature here would clobber a caller's
       local `function testFeature(...)` wrapper hoisted to the same
       slot, producing surprises like `testFeature.exit` recursing into
       itself.  Threads in non-transpiled tests (vector-test, map-test)
       handle their own thread bridging. */

    /* Expose state directly on the function for introspection. */
    testFeature.isRampart = _isRampart;
    testFeature.label     = global._tfState.label;
    testFeature.width     = width;
    return testFeature;
}

module.exports = TestFeature;
//lastline
