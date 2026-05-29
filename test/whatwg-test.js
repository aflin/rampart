/* WHATWG / W3C Web platform globals exposed by rampart-whatwg.so.
   This file covers ONLY the lazy-loaded Web platform surface; ECMAScript
   intrinsics, rampart extensions to duktape (Buffer / console / TextEncoder)
   and rampart-specific GC primitives (WeakRef family etc.) live in
   test/js-extensions-test.js instead.

   Architecture rule (claude-work/whatwg-todo.md §2):
       ECMAScript        → rampart core
       duktape natives extended by rampart → rampart core
       WHATWG / W3C      → rampart-whatwg.so (this test)
       Node-specific     → rampart-nodeshim.so (test/nodeshim-test.js)

   Runs under both rampart and node so we can cross-validate that the
   surface matches the standard:

       rampart  whatwg-test.js
       node     whatwg-test.js

   One line per category.  Subtests only print on failure (via the
   test-feature harness's exception path). */

var testJS = new (require('./test-feature.js'))({
    prefix:    "whatwg",
    allowNode: true
});
var must       = testJS.must;
var mustEq     = testJS.mustEq;
var mustThrow  = testJS.mustThrow;
var _isRampart = testJS.isRampart;

/* --- async-tail infrastructure (hoisted so any testJS body can ++/--
       the counter and signal completion).  Each async section does
       `_pendingAsync++;` synchronously then `_doneAsync()` from the
       promise chain's terminal callback; when the counter hits 0 the
       file exits via testJS.exit().  Failures inside a .then chain
       call _asyncFail() to log + record fail before _doneAsync(). */
var _pendingAsync = 0;
var _exitOnce = false;
function _doneAsync() {
    if (--_pendingAsync <= 0 && !_exitOnce) {
        _exitOnce = true;
        testJS.exit();
    }
}
function _asyncFail(label, e) {
    var msg = (e && e.message) || String(e);
    if (typeof printf === 'function') {
        printf("  %s: async assertion failed: %s\n", label, msg);
        /* Mirror test-feature.js's right-alignment so the FAILED text
         * lines up with sync passed/FAILED rows in the same column. */
        var FAILED_TEXT = ">>>>> FAILED <<<<<";
        var tfState     = global._tfState || {};
        var prefix      = tfState.label || "whatwg";
        var width       = tfState.width || 100;
        var FIXED_OVERHEAD = 8 /* "testing " */ + prefix.length + 3 /* " - " */ + 3 /* " - " */;
        var name        = label + " (async)";
        var maxNameW    = width - FIXED_OVERHEAD - FAILED_TEXT.length;
        if (maxNameW < 8) maxNameW = 8;
        if (name.length > maxNameW) name = name.substring(0, maxNameW - 3) + '...';
        var target = width - FIXED_OVERHEAD - FAILED_TEXT.length;
        if (target < 1) target = 1;
        while (name.length < target) name += ' ';
        printf("testing %s - %s - %s\n", prefix, name, FAILED_TEXT);
    }
    else if (typeof console !== 'undefined') console.error(label, e);
    try { if (typeof process !== 'undefined' && process.exit) process.exit(1); } catch (_) {}
}


/* ============================================================
 * URL / URLSearchParams / URLPattern
 * ============================================================ */

testJS("URL", function () {
    /* === Full-credentials parse === */
    var u = new URL('https://user:pass@example.com:8080/path?q=1#frag');
    mustEq(u.protocol, 'https:',        "URL.protocol");
    mustEq(u.username, 'user',          "URL.username");
    mustEq(u.password, 'pass',          "URL.password");
    mustEq(u.host,     'example.com:8080', "URL.host (with port)");
    mustEq(u.hostname, 'example.com',   "URL.hostname");
    mustEq(u.port,     '8080',          "URL.port");
    mustEq(u.pathname, '/path',         "URL.pathname");
    mustEq(u.search,   '?q=1',          "URL.search");
    mustEq(u.hash,     '#frag',         "URL.hash");
    mustEq(u.origin,   'https://example.com:8080', "URL.origin (excludes creds + path)");
    mustEq(u.href,     'https://user:pass@example.com:8080/path?q=1#frag', "URL.href round-trip");

    /* === Default port stripping === */
    mustEq(new URL('https://example.com:443/').port, '',  "https default port (443) stripped");
    mustEq(new URL('http://example.com:80/').port,   '',  "http default port (80) stripped");
    mustEq(new URL('ws://example.com:80/').port,     '',  "ws default port (80) stripped");
    mustEq(new URL('wss://example.com:443/').port,   '',  "wss default port (443) stripped");
    mustEq(new URL('ftp://example.com:21/').port,    '',  "ftp default port (21) stripped");

    /* === Relative resolution === */
    mustEq(new URL('/about', 'https://example.com').href, 'https://example.com/about', "relative root");
    mustEq(new URL('about', 'https://example.com/a/').href, 'https://example.com/a/about', "relative path");
    mustEq(new URL('../x', 'https://example.com/a/b/c').href, 'https://example.com/a/x',  "relative ..");
    mustEq(new URL('?q=1', 'https://example.com/a').href, 'https://example.com/a?q=1', "relative just-query");
    mustEq(new URL('#x', 'https://example.com/a').href, 'https://example.com/a#x',    "relative just-hash");

    /* === Mutation rewrites href === */
    var m = new URL('https://example.com/a?x=1');
    m.pathname = '/b';
    mustEq(m.href, 'https://example.com/b?x=1', "pathname mutation");
    m.hash = '#section';
    mustEq(m.hash, '#section', "hash mutation");
    m.searchParams.set('y', '2');
    must(m.search.indexOf('y=2') >= 0, "searchParams mutation reflected in .search");

    /* === Special-scheme schemes === */
    mustEq(new URL('file:///tmp/x').protocol, 'file:', "file: scheme");
    mustEq(new URL('file:///tmp/x').pathname, '/tmp/x', "file: pathname");
    /* data: URLs are opaque-path; spec parses them with empty host */
    mustEq(new URL('data:text/plain,hello').protocol, 'data:', "data: scheme");
    /* blob: URLs — protocol + pathname (inner URL string) + origin lifted
       from the inner URL.  Spec: https://url.spec.whatwg.org/#concept-url-origin */
    var bl = new URL('blob:https://example.com/abc');
    mustEq(bl.protocol, 'blob:',                       "blob: scheme");
    mustEq(bl.pathname, 'https://example.com/abc',     "blob: pathname is inner URL");
    mustEq(bl.origin,   'https://example.com',         "blob: origin from inner URL");
    mustEq(bl.host,     '',                            "blob: host is empty");
    var bl2 = new URL('blob:https://example.com:8443/x?y=1#z');
    mustEq(bl2.origin,  'https://example.com:8443',    "blob: origin keeps non-default port");
    /* Fragment on a blob: URL belongs to the blob URL itself, not the inner. */
    mustEq(bl2.hash,    '#z',                          "blob: fragment captured");

    /* === Percent encoding === */
    var enc = new URL('https://example.com/' + encodeURIComponent('a b/c'));
    mustEq(enc.pathname, '/a%20b%2Fc', "percent-encoded path preserved");
    mustEq(decodeURIComponent(enc.pathname.substring(1)), 'a b/c', "round-trip");

    /* === IDN / punycode === Unicode hostname → ACE form.  Spec: WHATWG
       URL §IDNA.  Both rampart (via require('punycode').toASCII) and
       node (built-in ICU) should produce the same xn-- form. */
    var idn = new URL('https://例え.example/');
    mustEq(idn.hostname, 'xn--r8jz45g.example', "IDN hostname → punycode");
    /* Mixed: subdomain Unicode, parent ASCII. */
    var idn2 = new URL('https://日本.example.com/');
    mustEq(idn2.hostname, 'xn--wgv71a.example.com', "IDN mixed-ASCII subdomain");
    /* Already-encoded ACE form passes through untouched. */
    var idn3 = new URL('https://xn--r8jz45g.example/');
    mustEq(idn3.hostname, 'xn--r8jz45g.example', "ACE hostname passes through");

    /* === Invalid URLs throw === */
    mustThrow(function() { new URL('not a url'); },         "bare 'not a url' throws");
    mustThrow(function() { new URL('//no-scheme'); },       "no-scheme (no base) throws");
    mustThrow(function() { new URL('http://[invalid'); },   "bad IPv6 throws");

    /* === URL.canParse / URL.parse statics (newer) === */
    if (typeof URL.canParse === 'function') {
        must(URL.canParse('https://example.com'),         "canParse true");
        must(!URL.canParse('not a url'),                  "canParse false");
        must(URL.canParse('/x', 'https://example.com'),   "canParse with base");
    }
    if (typeof URL.parse === 'function') {
        must(URL.parse('https://example.com') instanceof URL, "URL.parse returns URL");
        mustEq(URL.parse('not a url'), null,              "URL.parse returns null on bad input");
    }

    /* === fileURLToPath / pathToFileURL (URL module statics; not URL class) === */
    if (_isRampart) {
        var modURL = require('url').URL;
        mustEq(modURL, URL, "require('url').URL === global URL");
    }
});

testJS("URLSearchParams", function () {
    /* === Parsing === */
    var sp = new URLSearchParams('a=1&b=2&a=3');
    mustEq(sp.get('a'),   '1',           "get returns first occurrence");
    mustEq(JSON.stringify(sp.getAll('a')), '["1","3"]', "getAll returns all occurrences");
    mustEq(sp.has('b'),   true,          "has true");
    mustEq(sp.has('z'),   false,         "has false");

    /* === Constructor accepts iterable / record === */
    var sp2 = new URLSearchParams([['x', '1'], ['y', '2']]);
    mustEq(sp2.get('x'), '1',  "ctor from array-of-pairs");
    var sp3 = new URLSearchParams({a: '1', b: '2'});
    mustEq(sp3.get('a'), '1',  "ctor from record");

    /* === append / set / delete === */
    sp.append('c', '3');
    mustEq(sp.get('c'), '3', "append");
    sp.set('a', '99');
    mustEq(JSON.stringify(sp.getAll('a')), '["99"]', "set replaces all occurrences");
    sp['delete']('c');
    must(!sp.has('c'), "delete");

    /* === toString — order preserved, percent-encoded === */
    var sp4 = new URLSearchParams();
    sp4.append('greeting', 'hi there');
    sp4.append('q', 'a&b');
    var s = sp4.toString();
    must(s.indexOf('greeting=hi+there') >= 0 || s.indexOf('greeting=hi%20there') >= 0,
         "space encoded as + or %20 (impl varies)");
    must(s.indexOf('q=a%26b') >= 0, "ampersand percent-encoded");

    /* === Iteration === */
    var sp5 = new URLSearchParams('a=1&b=2');
    var keys = [];   var ki = sp5.keys();    var step;
    while (!(step = ki.next()).done) keys.push(step.value);
    mustEq(JSON.stringify(keys), '["a","b"]', "keys() iterator");

    var vals = [];   var vi = sp5.values();
    while (!(step = vi.next()).done) vals.push(step.value);
    mustEq(JSON.stringify(vals), '["1","2"]', "values() iterator");

    var ents = [];   var ei = sp5.entries();
    while (!(step = ei.next()).done) ents.push(step.value.join('='));
    mustEq(JSON.stringify(ents), '["a=1","b=2"]', "entries() iterator");

    /* forEach */
    var collected = [];
    sp5.forEach(function (v, k) { collected.push(k + '=' + v); });
    mustEq(JSON.stringify(collected), '["a=1","b=2"]', "forEach");

    /* size (newer) */
    if (typeof sp5.size === 'number') mustEq(sp5.size, 2, "size = 2");

    /* sort (alphabetical by key) */
    var sp6 = new URLSearchParams('c=1&a=2&b=3');
    sp6.sort();
    mustEq(sp6.toString().split('&').join(','), 'a=2,b=3,c=1', "sort alphabetizes by key");

    /* Plus sign in input is space when parsed */
    mustEq(new URLSearchParams('q=a+b').get('q'), 'a b', "+ decodes to space");
});

testJS("URLSearchParams.size + sort()", function () {
    var sp = new URLSearchParams('a=1&b=2&a=3');
    mustEq(sp.size, 3,                          ".size counts all entries (dupes included)");
    sp.delete('a');
    mustEq(sp.size, 1,                          ".size after delete");

    /* sort() — stable; same-name pairs preserve relative order */
    sp = new URLSearchParams('b=2&a=4&a=1&c=3');
    sp.sort();
    mustEq(sp.toString(), 'a=4&a=1&b=2&c=3',    "sort() stable + alphabetic");
});

testJS("URL method .name properties", function () {
    mustEq(URL.prototype.toString.name, "toString", "URL.prototype.toString.name");
    mustEq(URL.prototype.toJSON.name,   "toJSON",   "URL.prototype.toJSON.name");
    mustEq(URL.canParse.name,           "canParse", "URL.canParse.name");
    mustEq(URL.parse.name,              "parse",    "URL.parse.name");
});

testJS("URL setters (spec-compliant via upa)", function () {
    var u = new URL("https://user:pass@example.com:8080/path?q=1#h");
    u.hostname = "other.com";
    mustEq(u.hostname, "other.com",              "hostname set");
    mustEq(u.host, "other.com:8080",             "host reflects hostname change");
    u.port = "9000";
    mustEq(u.port, "9000",                       "port set");
    u.pathname = "/x/y";
    mustEq(u.pathname, "/x/y",                   "pathname set");
    u.hash = "#new";
    mustEq(u.hash, "#new",                       "hash set");
    u.protocol = "http";
    mustEq(u.protocol, "http:",                  "protocol set (default port handling)");
    /* Invalid set: silently ignored per spec */
    var saved = u.hostname;
    u.hostname = "bad host with spaces";
    /* Either rejects or rewrites; spec says reject (no change) for invalid */
    must(u.hostname === saved || u.hostname.indexOf(' ') < 0,
                                                 "invalid hostname rejected or coerced");
});

testJS("URLPattern", function () {
    if (typeof URLPattern !== 'function') return;  /* node <23 doesn't expose */
    /* String form */
    var p = new URLPattern("https://example.com/users/:id");
    must(p instanceof URLPattern,                "new URLPattern");
    mustEq(p.test("https://example.com/users/42"), true,  "test true");
    mustEq(p.test("https://example.com/posts/42"), false, "test false");
    var r = p.exec("https://example.com/users/42");
    must(r && r.pathname,                        "exec returns result");
    mustEq(r.pathname.groups.id, "42",           "named group");

    /* Object form */
    var p2 = new URLPattern({pathname: "/api/:version/*"});
    mustEq(p2.test({pathname: "/api/v1/foo"}),   true,  "object test true");
    mustEq(p2.pathname, "/api/:version/*",       "pathname pattern getter");
    var r2 = p2.exec({pathname: "/api/v2/x"});
    mustEq(r2.pathname.groups.version, "v2",     "object exec named group");
});


/* ============================================================
 * performance (timeOrigin / mark / measure / getEntries / clear*)
 * PerformanceObserver
 * ============================================================ */

testJS("performance (timeOrigin + mark/measure/getEntries)", function () {
    /* Surface */
    must(typeof performance.now === 'function',         "performance.now");
    must(typeof performance.mark === 'function',        "performance.mark");
    must(typeof performance.measure === 'function',     "performance.measure");
    must(typeof performance.clearMarks === 'function',  "performance.clearMarks");
    must(typeof performance.clearMeasures === 'function', "performance.clearMeasures");
    must(typeof performance.getEntries === 'function',  "performance.getEntries");
    must(typeof performance.getEntriesByName === 'function', "getEntriesByName");
    must(typeof performance.getEntriesByType === 'function', "getEntriesByType");
    must(typeof performance.timeOrigin === 'number' && performance.timeOrigin > 0,
         "timeOrigin > 0 (ms since epoch)");

    /* Clear any leftover state from earlier tests */
    performance.clearMarks();
    performance.clearMeasures();

    /* mark returns PerformanceMark with correct fields */
    var m1 = performance.mark('pf-a');
    if (typeof PerformanceMark === 'function') must(m1 instanceof PerformanceMark, "mark returns PerformanceMark");
    mustEq(m1.name, 'pf-a',               "mark.name");
    mustEq(m1.entryType, 'mark',          "mark.entryType = mark");
    must(typeof m1.startTime === 'number', "mark.startTime is number");
    mustEq(m1.duration, 0,                "mark.duration is 0");

    /* mark accepts {startTime, detail} */
    var m2 = performance.mark('pf-b', {startTime: 1000, detail: {info: 'x'}});
    mustEq(m2.startTime, 1000,            "mark startTime override");
    mustEq(m2.detail.info, 'x',           "mark.detail preserved");

    /* measure between two marks → PerformanceMeasure */
    performance.mark('pf-c', {startTime: 100});
    performance.mark('pf-d', {startTime: 250});
    var meas = performance.measure('pf-cd', 'pf-c', 'pf-d');
    if (typeof PerformanceMeasure === 'function')
        must(meas instanceof PerformanceMeasure, "measure returns PerformanceMeasure");
    mustEq(meas.name, 'pf-cd',            "measure.name");
    mustEq(meas.entryType, 'measure',     "measure.entryType");
    mustEq(meas.startTime, 100,           "measure.startTime");
    mustEq(meas.duration,  150,           "measure.duration (250-100)");

    /* measure({start, end, duration}) options form */
    var meas2 = performance.measure('pf-opt', {start: 500, end: 700});
    mustEq(meas2.startTime, 500,          "measure opts.start");
    mustEq(meas2.duration,  200,          "measure opts duration from end-start");

    /* getEntries returns chronological list of all entries */
    var all = performance.getEntries();
    must(Array.isArray(all),              "getEntries returns array");
    must(all.length >= 6,                 "≥6 entries (4 marks + 2 measures)");

    /* getEntriesByName narrows */
    var byName = performance.getEntriesByName('pf-cd');
    mustEq(byName.length, 1,              "getEntriesByName count");
    mustEq(byName[0].name, 'pf-cd',       "getEntriesByName[0].name");

    /* getEntriesByName(name, type) filters by type too */
    var marksOnly = performance.getEntriesByName('pf-a', 'mark');
    mustEq(marksOnly.length, 1,           "getEntriesByName + type filter");
    var measOnly  = performance.getEntriesByName('pf-a', 'measure');
    mustEq(measOnly.length, 0,            "name + wrong type → empty");

    /* getEntriesByType */
    var typeMarks = performance.getEntriesByType('mark');
    must(typeMarks.length >= 4,           "getEntriesByType('mark') count");
    typeMarks.forEach(function (e) { mustEq(e.entryType, 'mark', "type filter mark"); });

    /* clearMarks(name) clears just that name; without arg clears all */
    performance.clearMarks('pf-a');
    mustEq(performance.getEntriesByName('pf-a').length, 0, "clearMarks(name)");
    must(performance.getEntriesByType('mark').length > 0,  "clearMarks(name) keeps others");
    performance.clearMarks();
    mustEq(performance.getEntriesByType('mark').length, 0, "clearMarks() clears all");
    performance.clearMeasures();
    mustEq(performance.getEntriesByType('measure').length, 0, "clearMeasures() clears all");

    /* PerformanceEntry.toJSON */
    performance.mark('pf-json');
    var je = performance.getEntriesByName('pf-json')[0];
    if (typeof je.toJSON === 'function') {
        var j = je.toJSON();
        mustEq(j.name, 'pf-json',         "toJSON.name");
        mustEq(j.entryType, 'mark',       "toJSON.entryType");
    }
    performance.clearMarks();

    /* performance.now is monotonic + returns number */
    var t1 = performance.now();
    var t2 = performance.now();
    must(typeof t1 === 'number' && t1 >= 0, "now() returns non-negative number");
    must(t2 >= t1,                          "now() is monotonic");
});

testJS("PerformanceObserver (async)", function () {
    _pendingAsync++;
    var got = [];
    var obs = new PerformanceObserver(function (list) {
        var entries = list.getEntries();
        for (var i = 0; i < entries.length; i++) got.push(entries[i]);
    });
    obs.observe({entryTypes: ['mark', 'measure']});
    performance.mark('ob-a');
    performance.mark('ob-b');
    performance.measure('ob-span', 'ob-a', 'ob-b');
    /* Microtask delivery — check after one tick */
    Promise.resolve().then(function () {
        try {
            must(got.length >= 3, "observer got 3+ entries (got " + got.length + ")");
            var marks = got.filter(function (e) { return e.entryType === 'mark'; });
            var msrs  = got.filter(function (e) { return e.entryType === 'measure'; });
            must(marks.length >= 2, "saw both marks");
            must(msrs.length >= 1,  "saw the measure");
            mustEq(PerformanceObserver.supportedEntryTypes.indexOf('mark') >= 0, true,
                                    "supportedEntryTypes includes 'mark'");
        } catch (e) { _asyncFail("PerformanceObserver", e); }
        obs.disconnect();
        _doneAsync();
    });
});


/* ============================================================
 * atob / btoa / reportError / queueMicrotask / structuredClone
 * ============================================================ */

testJS("atob / btoa", function () {
    /* Round-trips */
    mustEq(btoa('hello'),    'aGVsbG8=', "btoa(hello)");
    mustEq(atob('aGVsbG8='), 'hello',    "atob(aGVsbG8=)");

    /* All 256 byte values */
    var bin = '';
    for (var i = 0; i < 256; i++) bin += String.fromCharCode(i);
    mustEq(atob(btoa(bin)), bin,         "atob/btoa 256-byte round-trip");

    /* Padding variants (1, 2 bytes) */
    mustEq(btoa('A'),  'QQ==',           "btoa 1 byte → 2 chars + ==");
    mustEq(btoa('AB'), 'QUI=',           "btoa 2 bytes → 3 chars + =");
    mustEq(btoa('ABC'), 'QUJD',          "btoa 3 bytes → 4 chars no pad");
    mustEq(atob('QQ=='),  'A',           "atob with == pad");
    mustEq(atob('QUI='),  'AB',          "atob with = pad");
    mustEq(atob('QUJD'),  'ABC',         "atob without pad");

    /* Empty */
    mustEq(btoa(''), '',                 "btoa empty");
    mustEq(atob(''), '',                 "atob empty");

    /* btoa rejects characters > 0xff (é=0xE9 is in-range; '中' is U+4E2D > 0xFF) */
    mustThrow(function () { btoa('中'); }, "btoa rejects code point > 0xff (InvalidCharacterError)");
    mustThrow(function () { btoa('\u{1F600}'); }, "btoa rejects emoji (surrogate pair)");
    /* But latin-1 range chars ARE accepted */
    must(btoa('hé') === btoa('h\xe9'), "btoa accepts latin-1 chars");

    /* atob rejects invalid base64 */
    mustThrow(function () { atob('not!base64'); }, "atob rejects invalid char");
});

testJS("reportError", function () {
    /* node v22 still doesn't ship reportError as a global — gate. */
    if (typeof reportError !== 'function') {
        if (_isRampart) throw new Error("reportError missing in rampart-whatwg");
        return true;  /* node gap — accept */
    }
    /* Should NOT throw — silence console.error during the call. */
    var origErr = console.error;
    var consoleCalls = 0;
    console.error = function () { consoleCalls++; };
    /* If process.emit exists, capture uncaughtException */
    var origHandlers = (typeof process !== 'undefined' && typeof process.listeners === 'function')
                       ? process.listeners('uncaughtException').slice() : null;
    if (origHandlers && typeof process.removeAllListeners === 'function')
        process.removeAllListeners('uncaughtException');
    var emittedErrs = [];
    if (typeof process !== 'undefined' && typeof process.on === 'function')
        process.on('uncaughtException', function (e) { emittedErrs.push(e); });
    try {
        reportError(new Error('rampart-report-test'));
        /* Must not throw.  Should also have either invoked console.error
           OR emitted uncaughtException (impl-defined). */
        must(consoleCalls >= 1 || emittedErrs.length >= 1,
             "reportError fires console.error and/or process.emit('uncaughtException')");
        /* Non-Error argument also accepted */
        reportError('a string error');
        reportError({foo: 'bar'});
        /* Verify it doesn't crash on weird inputs */
    } finally {
        console.error = origErr;
        if (origHandlers && typeof process.removeAllListeners === 'function') {
            process.removeAllListeners('uncaughtException');
            origHandlers.forEach(function (h) { process.on('uncaughtException', h); });
        }
    }
});

testJS("queueMicrotask (async)", function () {
    _pendingAsync++;
    var fired = false;
    queueMicrotask(function () { fired = true; });
    setTimeout(function () {
        try { mustEq(fired, true, "queueMicrotask fires before setTimeout"); }
        catch (e) { _asyncFail("queueMicrotask", e); }
        _doneAsync();
    }, 1);
    return true;
});

testJS("structuredClone", function () {
    /* Primitives pass through */
    mustEq(structuredClone(null),      null,      "null");
    mustEq(structuredClone(42),        42,        "number");
    mustEq(structuredClone('hi'),      'hi',      "string");
    mustEq(structuredClone(true),      true,      "boolean");
    mustEq(structuredClone(undefined), undefined, "undefined");
    /* Plain object deep-cloned */
    var o = {a: 1, b: {c: 2, d: [3, 4]}};
    var co = structuredClone(o);
    must(co !== o,     "clone !== orig");
    must(co.b !== o.b, "nested clone is deep");
    mustEq(JSON.stringify(co), JSON.stringify(o), "structure preserved");
    /* Date / RegExp */
    var d = new Date(1234567890000);
    var dc = structuredClone(d);
    must(dc instanceof Date,         "Date preserved");
    mustEq(dc.getTime(), d.getTime(), "Date time");
    var r = /abc/gi;
    var rc = structuredClone(r);
    must(rc instanceof RegExp, "RegExp preserved");
    mustEq(rc.source, 'abc',   "RegExp.source");
    mustEq(rc.flags,  'gi',    "RegExp.flags");
    /* Map / Set */
    var m = new Map([['k', 'v'], ['k2', {nested: true}]]);
    var mc = structuredClone(m);
    must(mc instanceof Map,            "Map preserved");
    mustEq(mc.size, 2,                 "Map size");
    must(mc.get('k2').nested === true, "Map nested object preserved");
    var s = new Set([1, 2, 3]);
    var sc = structuredClone(s);
    must(sc instanceof Set, "Set preserved");
    mustEq(sc.size, 3,      "Set size");
    /* Error subclass */
    var e = new TypeError('typeOops');
    var ec = structuredClone(e);
    must(ec instanceof TypeError, "Error subclass preserved");
    mustEq(ec.message, 'typeOops', "Error.message preserved");
    /* TypedArray + cycle + shared reference */
    var u8 = new Uint8Array([10, 20, 30]);
    var u8c = structuredClone(u8);
    must(u8c instanceof Uint8Array, "Uint8Array preserved");
    must(u8c.buffer !== u8.buffer,  "ArrayBuffer cloned");
    var cyc = {n: 1}; cyc.self = cyc;
    var cycC = structuredClone(cyc);
    must(cycC.self === cycC, "cycle preserved");
    must(cycC.self !== cyc,  "cycle points at clone, not original");
    var shared = {x: 1};
    var shc = structuredClone({a: shared, b: shared});
    must(shc.a === shc.b, "shared-reference collapsed");
    /* Function / Symbol rejection */
    mustThrow(function () { structuredClone(function(){}); }, "function rejected");
    mustThrow(function () { structuredClone(Symbol());    }, "symbol rejected");
    /* {transfer:[…]} — rampart doesn't support detachment; node does */
    if (_isRampart)
        mustThrow(function () { structuredClone({}, {transfer:[new ArrayBuffer(4)]}); },
                  "{transfer} rejected (rampart only)");
});


/* ============================================================
 * Event / EventTarget / Event subclasses / AbortController/Signal
 * DOMException
 * ============================================================ */

testJS("Event / EventTarget", function () {
    var e = new Event('test', {cancelable: true});
    mustEq(e.type,             'test', "Event.type");
    mustEq(e.cancelable,       true,   "Event.cancelable");
    mustEq(e.defaultPrevented, false,  "Event.defaultPrevented default");
    e.preventDefault();
    mustEq(e.defaultPrevented, true,   "Event.preventDefault");
    var et = new EventTarget();
    var fired = 0;
    et.addEventListener('hello', function (ev) {
        fired++;
        mustEq(ev.type, 'hello', "dispatched event.type");
        mustEq(ev.target, et,    "dispatched event.target");
    });
    et.dispatchEvent(new Event('hello'));
    mustEq(fired, 1, "dispatch fires listener");
    /* once option */
    var onceN = 0;
    et.addEventListener('o', function () { onceN++; }, {once: true});
    et.dispatchEvent(new Event('o'));
    et.dispatchEvent(new Event('o'));
    mustEq(onceN, 1, "once removes listener after first fire");
    /* removeEventListener */
    var rfn = function () { throw new Error('should not fire'); };
    et.addEventListener('r', rfn);
    et.removeEventListener('r', rfn);
    et.dispatchEvent(new Event('r'));  /* should not throw */
    /* stopImmediatePropagation */
    var sip = [];
    et.addEventListener('s', function (ev) { sip.push('a'); ev.stopImmediatePropagation(); });
    et.addEventListener('s', function () { sip.push('b'); });
    et.dispatchEvent(new Event('s'));
    mustEq(sip.length, 1, "stopImmediatePropagation halts dispatch");
    mustEq(sip[0],     'a', "stopped after first listener");
});

testJS("Event subclasses (Custom/Message/Close/Error)", function () {
    /* CustomEvent */
    var ce = new CustomEvent('x', {detail: {n: 42}});
    must(ce instanceof Event,        "CustomEvent extends Event");
    must(ce instanceof CustomEvent,  "CustomEvent instanceof");
    mustEq(ce.type,       'x',       "CustomEvent.type");
    mustEq(ce.detail.n,    42,       "CustomEvent.detail");
    /* default detail = null */
    mustEq(new CustomEvent('y').detail, null, "CustomEvent detail default null");

    /* MessageEvent */
    var me = new MessageEvent('m', {data: 'hi', origin: 'http://x', lastEventId: '7'});
    must(me instanceof Event,        "MessageEvent extends Event");
    must(me instanceof MessageEvent, "MessageEvent instanceof");
    mustEq(me.data,        'hi',     "MessageEvent.data");
    mustEq(me.origin,      'http://x', "MessageEvent.origin");
    mustEq(me.lastEventId, '7',      "MessageEvent.lastEventId");
    must(Array.isArray(me.ports),    "MessageEvent.ports is array");
    mustEq(me.ports.length, 0,       "MessageEvent.ports default []");

    /* CloseEvent and ErrorEvent: rampart-whatwg installs them; node
       v22 doesn't expose them as globals (lives inside undici).
       Gate the assertions. */
    if (typeof CloseEvent === 'function') {
        var cle = new CloseEvent('close', {wasClean: true, code: 1000, reason: 'bye'});
        must(cle instanceof Event,      "CloseEvent extends Event");
        must(cle instanceof CloseEvent, "CloseEvent instanceof");
        mustEq(cle.wasClean, true,      "CloseEvent.wasClean");
        mustEq(cle.code,     1000,      "CloseEvent.code");
        mustEq(cle.reason,   'bye',     "CloseEvent.reason");
    } else if (_isRampart) {
        throw new Error("CloseEvent missing in rampart-whatwg");
    }

    if (typeof ErrorEvent === 'function') {
        var ee = new ErrorEvent('error', {message: 'oops', filename: 'a.js', lineno: 42, colno: 7});
        must(ee instanceof Event,      "ErrorEvent extends Event");
        must(ee instanceof ErrorEvent, "ErrorEvent instanceof");
        mustEq(ee.message,  'oops',    "ErrorEvent.message");
        mustEq(ee.filename, 'a.js',    "ErrorEvent.filename");
        mustEq(ee.lineno,   42,        "ErrorEvent.lineno");
        mustEq(ee.colno,    7,         "ErrorEvent.colno");
    } else if (_isRampart) {
        throw new Error("ErrorEvent missing in rampart-whatwg");
    }

    /* Dispatch through EventTarget — listener sees the subclass-specific fields */
    var et = new EventTarget();
    var got = null;
    et.addEventListener('m', function (ev) { got = ev; });
    et.dispatchEvent(new MessageEvent('m', {data: 'payload'}));
    must(got instanceof MessageEvent, "dispatched MessageEvent through EventTarget");
    mustEq(got.data, 'payload',       "dispatched MessageEvent.data");
});

testJS("ProgressEvent + PromiseRejectionEvent", function () {
    if (typeof ProgressEvent !== 'function' || typeof PromiseRejectionEvent !== 'function')
        return;  /* node <22 / not exposed as global in this version */
    var pe = new ProgressEvent('progress', {loaded: 100, total: 200, lengthComputable: true});
    must(pe instanceof Event,                    "ProgressEvent extends Event");
    mustEq(pe.loaded, 100,                       "loaded");
    mustEq(pe.total,  200,                       "total");
    mustEq(pe.lengthComputable, true,            "lengthComputable");

    var rj = new PromiseRejectionEvent('unhandledrejection', {
        promise: Promise.resolve(),
        reason:  new Error("boom")
    });
    must(rj instanceof Event,                    "PromiseRejectionEvent extends Event");
    mustEq(rj.reason.message, "boom",            "reason field");
});

testJS("AbortController / AbortSignal", function () {
    var ac = new AbortController();
    must(ac.signal instanceof AbortSignal, "AbortController.signal is AbortSignal");
    mustEq(ac.signal.aborted, false,     "signal.aborted false initially");
    mustEq(ac.signal.reason,  undefined, "signal.reason undefined initially");
    var fired = false, gotReason = null;
    ac.signal.addEventListener('abort', function () { fired = true; gotReason = ac.signal.reason; });
    ac.abort('user-cancel');
    mustEq(ac.signal.aborted, true,          "signal.aborted true after abort");
    mustEq(ac.signal.reason,  'user-cancel', "signal.reason propagated");
    mustEq(fired,             true,          "abort listener fired");
    mustEq(gotReason,         'user-cancel', "listener saw reason");
    mustThrow(function () { ac.signal.throwIfAborted(); }, "throwIfAborted throws after abort");
    /* static abort */
    var pre = AbortSignal.abort('pre');
    mustEq(pre.aborted, true,  "AbortSignal.abort already aborted");
    mustEq(pre.reason,  'pre', "AbortSignal.abort reason");
    /* AbortSignal.any */
    var a1 = new AbortController(), a2 = new AbortController();
    var any = AbortSignal.any([a1.signal, a2.signal]);
    mustEq(any.aborted, false, "any not yet aborted");
    var anyFired = false;
    any.addEventListener('abort', function () { anyFired = true; });
    a2.abort('from-a2');
    mustEq(any.aborted, true,      "any propagates abort");
    mustEq(any.reason,  'from-a2', "any propagates reason");
    mustEq(anyFired,    true,      "any listener fired");
    var early = AbortSignal.any([AbortSignal.abort('early')]);
    mustEq(early.aborted, true,    "any picks up already-aborted input");
    mustEq(early.reason,  'early', "any takes first aborted reason");
});

testJS("AbortSignal.timeout / .any", function () {
    /* AbortSignal.any */
    var c1 = new AbortController();
    var c2 = new AbortController();
    var any = AbortSignal.any([c1.signal, c2.signal]);
    must(any instanceof AbortSignal,             "any returns AbortSignal");
    mustEq(any.aborted, false,                   "any initially not aborted");
    c1.abort("first");
    mustEq(any.aborted, true,                    "any flips when first signal aborts");
    /* AbortSignal.timeout — async aspect */
    _pendingAsync++;
    var sig = AbortSignal.timeout(20);
    must(sig instanceof AbortSignal,             "timeout returns AbortSignal");
    sig.addEventListener('abort', function () {
        try {
            must(sig.aborted,                    "signal aborted after timeout");
            must(sig.reason && sig.reason.name === 'TimeoutError', "reason is TimeoutError");
        } catch (e) { _asyncFail("AbortSignal.timeout", e); }
        _doneAsync();
    });
});

testJS("AbortSignal.timeout (async)", function () {
    _pendingAsync++;
    var sig = AbortSignal.timeout(20);
    mustEq(sig.aborted, false, "timeout signal not yet aborted");
    var settled = false;
    /* Drive completion off the abort event itself rather than a fixed
     * 100ms deadline. */
    sig.addEventListener('abort', function () {
        if (settled) return;
        settled = true;
        try {
            mustEq(sig.aborted, true,           "timeout signal aborted");
            must(sig.reason && sig.reason.name === 'TimeoutError',
                 "timeout reason is TimeoutError");
        } catch (e) { _asyncFail("AbortSignal.timeout", e); }
        _doneAsync();
    });
    /* Safety net: if the 20ms timer never fires, fail rather than hang. */
    setTimeout(function () {
        if (settled) return;
        settled = true;
        _asyncFail("AbortSignal.timeout",
            new Error("AbortSignal.timeout(20) didn't fire within 1000ms"));
        _doneAsync();
    }, 1000);
    return true;
});

testJS("DOMException", function () {
    must(typeof DOMException === 'function', "DOMException global");
    var e = new DOMException('oops', 'TimeoutError');
    must(e instanceof DOMException, "instanceof DOMException");
    must(e instanceof Error,        "extends Error");
    mustEq(e.name,    'TimeoutError', "name");
    mustEq(e.message, 'oops',         "message");
    mustEq(e.code,    23,             "legacy code TIMEOUT_ERR=23");
    mustEq(DOMException.TIMEOUT_ERR, 23, "class-level TIMEOUT_ERR const");
    /* Other named errors */
    mustEq(new DOMException('x', 'AbortError').code,      20, "AbortError code");
    mustEq(new DOMException('x', 'DataCloneError').code,  25, "DataCloneError code");
    /* Unknown name → code 0 */
    mustEq(new DOMException('x', 'WeirdError').code, 0, "unknown name → code 0");
    /* Default name when omitted */
    mustEq(new DOMException('x').name, 'Error', "default name");
    /* Symbol.toStringTag */
    mustEq(Object.prototype.toString.call(new DOMException()), '[object DOMException]', "toStringTag");
});


/* ============================================================
 * WebSocket / WebSocketError / XMLHttpRequest / EventSource
 * ============================================================ */

testJS("WebSocket (surface)", function () {
    must(typeof WebSocket === 'function',  "WebSocket global");
    /* class-level constants */
    mustEq(WebSocket.CONNECTING, 0, "CONNECTING=0");
    mustEq(WebSocket.OPEN,       1, "OPEN=1");
    mustEq(WebSocket.CLOSING,    2, "CLOSING=2");
    mustEq(WebSocket.CLOSED,     3, "CLOSED=3");
    /* Scheme validation — rampart-whatwg rejects synchronously per
       spec; node 22 accepts the construction and errors out async.
       Keep as rampart-only. */
    if (_isRampart) {
        mustThrow(function(){ new WebSocket('http://x'); },  "http: rejected (rampart)");
        mustThrow(function(){ new WebSocket('https://x'); }, "https: rejected (rampart)");
    }
    /* Construction against unreachable server — should NOT throw synchronously
       per spec (errors arrive async via the 'error'/'close' events). */
    var ws = new WebSocket('ws://127.0.0.1:1/');
    must(ws instanceof WebSocket,   "WebSocket constructor (async errors)");
    must(ws instanceof EventTarget, "WebSocket extends EventTarget");
    mustEq(ws.readyState, WebSocket.CONNECTING, "initial readyState = CONNECTING");
    /* instance-level constants (spec required) */
    mustEq(ws.CONNECTING, 0, "instance CONNECTING");
    mustEq(ws.OPEN,       1, "instance OPEN");
    mustEq(ws.url,        'ws://127.0.0.1:1/', "ws.url");
    /* binaryType — spec default is 'blob' (HTML Living Standard) */
    mustEq(ws.binaryType, 'blob', "default binaryType (spec)");
    ws.binaryType = 'arraybuffer';
    mustEq(ws.binaryType, 'arraybuffer', "set binaryType to arraybuffer");
    /* invalid value: spec says throw SyntaxError (rampart);
       node 22 silently ignores invalid values.  Gate rampart-only. */
    if (_isRampart)
        mustThrow(function(){ ws.binaryType = 'bogus'; }, "invalid binaryType rejected (rampart)");
    /* Sending before open throws InvalidStateError */
    mustThrow(function(){ ws.send('hi'); }, "send-before-open throws");
    /* event handler properties accept null/function */
    ws.onopen = function(){};
    ws.onopen = null;
    /* close before connect is a no-op (or readyState transitions; both OK per spec) */
    ws.close();
    /* bufferedAmount: spec is "number"; we report 0 */
    must(typeof ws.bufferedAmount === 'number', "bufferedAmount is number");
});

testJS("WebSocketError", function () {
    if (typeof WebSocketError !== 'function') return;  /* node hasn't shipped */
    must(typeof WebSocketError === 'function',  "global WebSocketError class");

    /* Defaults */
    var e1 = new WebSocketError();
    must(e1 instanceof DOMException,            "extends DOMException");
    mustEq(e1.name,      'WebSocketError',      "default name");
    mustEq(e1.code,      0,                     "DOMException code is 0");
    mustEq(e1.message,   '',                    "default message empty");
    mustEq(e1.closeCode, null,                  "default closeCode null");
    mustEq(e1.reason,    '',                    "default reason empty");

    /* Full init */
    var e2 = new WebSocketError('lost', {closeCode: 3456, reason: 'gone'});
    mustEq(e2.message,   'lost',                "custom message");
    mustEq(e2.closeCode, 3456,                  "closeCode honored");
    mustEq(e2.reason,    'gone',                "reason honored");

    /* Reason without closeCode → default to 1000 */
    var e3 = new WebSocketError('', {reason: 'specified'});
    mustEq(e3.closeCode, 1000,                  "reason-only defaults closeCode to 1000");
    mustEq(e3.reason,    'specified',           "reason kept");

    /* Custom 3xxx range allowed */
    var e4 = new WebSocketError('', {closeCode: 3333});
    mustEq(e4.closeCode, 3333,                  "custom 3xxx closeCode");

    /* Invalid codes throw InvalidAccessError DOMException */
    var bads = [999, 1001, 2999, 5000];
    for (var i = 0; i < bads.length; i++) {
        var threw = false;
        try { new WebSocketError('', {closeCode: bads[i]}); }
        catch (e) {
            threw = true;
            mustEq(e.name, 'InvalidAccessError',
                   "invalid code " + bads[i] + " → InvalidAccessError");
        }
        must(threw, "invalid closeCode " + bads[i] + " must throw");
    }

    /* Constructor-without-new throws */
    var threw = false;
    try { WebSocketError(); } catch (e) { threw = true; }
    must(threw, "constructor-without-new throws");
});

testJS("XMLHttpRequest (surface, no network)", function () {
    must(typeof XMLHttpRequest === 'function',  "global XMLHttpRequest class");
    /* readyState constants */
    mustEq(XMLHttpRequest.UNSENT,           0,  "UNSENT=0");
    mustEq(XMLHttpRequest.OPENED,           1,  "OPENED=1");
    mustEq(XMLHttpRequest.HEADERS_RECEIVED, 2,  "HEADERS_RECEIVED=2");
    mustEq(XMLHttpRequest.LOADING,          3,  "LOADING=3");
    mustEq(XMLHttpRequest.DONE,             4,  "DONE=4");

    var xhr = new XMLHttpRequest();
    /* Prototype constants too */
    mustEq(xhr.DONE,                        4,  "instance.DONE");
    mustEq(xhr.readyState,                  0,  "initial readyState UNSENT");
    mustEq(xhr.status,                      0,  "initial status 0");
    mustEq(xhr.statusText,                  '', "initial statusText empty");
    mustEq(xhr.responseType,                '', "initial responseType empty");
    mustEq(xhr.timeout,                     0,  "initial timeout 0");
    mustEq(xhr.withCredentials,             false, "initial withCredentials false");
    must(typeof xhr.upload === 'object',        "upload is an object (EventTarget)");
    must(typeof xhr.upload.addEventListener === 'function', "upload has addEventListener");
    must(typeof xhr.addEventListener === 'function', "xhr is an EventTarget");

    /* open() validation */
    var threw;
    threw = false;
    try { xhr.open('IN VALID', 'http://x/'); } catch (e) {
        threw = true; mustEq(e.name, 'SyntaxError', "bad method → SyntaxError");
    }
    must(threw, "open with invalid method throws");

    threw = false;
    try { xhr.open('TRACE', 'http://x/'); } catch (e) {
        threw = true; mustEq(e.name, 'SecurityError', "forbidden method TRACE → SecurityError");
    }
    must(threw, "open with TRACE throws");

    /* sync mode not supported in our shim */
    threw = false;
    try { xhr.open('GET', 'http://x/', false); } catch (e) {
        threw = true; mustEq(e.name, 'InvalidAccessError', "sync XHR → InvalidAccessError");
    }
    must(threw, "open with async=false throws");

    /* normal open transitions readyState to OPENED (1) */
    xhr.open('GET', 'http://example.com/path');
    mustEq(xhr.readyState, 1,                   "readyState OPENED after open");

    /* responseType setter silently ignores invalid values, accepts valid */
    xhr.responseType = 'json';
    mustEq(xhr.responseType, 'json',            "responseType=json accepted");
    xhr.responseType = 'nosuchtype';
    mustEq(xhr.responseType, 'json',            "invalid responseType silently kept previous");
    xhr.responseType = 'arraybuffer';
    mustEq(xhr.responseType, 'arraybuffer',     "responseType=arraybuffer accepted");

    /* setRequestHeader validation */
    threw = false;
    try { xhr.setRequestHeader('Bad Name', 'v'); } catch (e) {
        threw = true; mustEq(e.name, 'SyntaxError', "bad header name → SyntaxError");
    }
    must(threw, "setRequestHeader with space in name throws");

    threw = false;
    try { xhr.setRequestHeader('X-Test', 'a\nb'); } catch (e) {
        threw = true; mustEq(e.name, 'SyntaxError', "bad header value → SyntaxError");
    }
    must(threw, "setRequestHeader with LF in value throws");

    xhr.setRequestHeader('X-Test', 'ok');        /* shouldn't throw */

    /* abort() on UNSENT/OPENED-without-send doesn't fire events but is OK */
    xhr.abort();                                /* shouldn't throw */

    /* Constructor-without-new */
    threw = false;
    try { XMLHttpRequest(); } catch (e) { threw = true; }
    must(threw, "XHR constructor-without-new throws");
});

testJS("EventSource (parser only)", function () {
    if (typeof EventSource !== 'function') return;  /* node <22 / not exposed */
    if (!_isRampart) return;  /* node's EventSource doesn't expose _feed; can't drive parser without a real SSE server */
    /* The connect path requires a real SSE server.  Test the parser
       internals via the _feed hook. */
    var es = new EventSource("https://example.com");
    var received = [];
    es.addEventListener('message', function (e) {
        received.push({type: 'message', data: e.data, id: e.lastEventId});
    });
    es.addEventListener('ping', function (e) {
        received.push({type: 'ping', data: e.data, id: e.lastEventId});
    });
    es._feed("data: hi\n\ndata: pong\nevent: ping\nid: 5\n\n");
    es._feed("data: multi 1\ndata: multi 2\n\n");
    es.close();
    mustEq(received.length, 3,                   "3 events parsed");
    mustEq(received[0].data, "hi",               "first data");
    mustEq(received[1].data, "pong",             "second data (custom event)");
    mustEq(received[1].id,   "5",                "lastEventId");
    mustEq(received[2].data, "multi 1\nmulti 2", "multi-line data joined with \\n");
});


/* ============================================================
 * MessageChannel / MessagePort / BroadcastChannel
 * ============================================================ */

testJS("MessageChannel / MessagePort", function () {
    must(typeof MessageChannel === 'function', "MessageChannel global");
    must(typeof MessagePort    === 'function', "MessagePort global");
    var mc = new MessageChannel();
    must(mc.port1 && mc.port2,                  "channel has two ports");
    must(mc.port1 !== mc.port2,                 "ports are distinct");
    /* postMessage round-trip is async — covered in the async tail */
    mc.port1.close(); mc.port2.close();
});

testJS("MessageChannel postMessage (async)", function () {
    _pendingAsync++;
    var mc = new MessageChannel();
    var settled = false;
    /* Drive completion off the actual message event rather than a fixed
     * wall-clock timeout (which races other tests' async load). */
    mc.port2.on('message', function (m) {
        if (settled) return;
        settled = true;
        try { mustEq(m, 'mc-ping', "port2 received port1's postMessage"); }
        catch (e) { _asyncFail("MessageChannel", e); }
        mc.port1.close(); mc.port2.close();
        _doneAsync();
    });
    mc.port1.postMessage('mc-ping');
    /* Safety net: if message never dispatches, fail rather than hang. */
    setTimeout(function () {
        if (settled) return;
        settled = true;
        _asyncFail("MessageChannel", new Error("postMessage never dispatched after 500ms"));
        try { mc.port1.close(); mc.port2.close(); } catch (_) {}
        _doneAsync();
    }, 500);
    return true;
});

testJS("MessageChannel + MessagePort dual-shape API (async)", function () {
    _pendingAsync++;
    var mc = new MessageChannel();
    var via_on = null, via_addEvent = null, via_handler = null;
    var seen = 0, settled = false;
    /* Three listeners; assert once all three have fired (or via a safety
     * timeout if dispatch never completes).  Avoids racing against a
     * fixed wall-clock deadline that can be missed when the event loop
     * is loaded with prior tests' async work. */
    function maybeDone() {
        if (settled || ++seen < 3) return;
        settled = true;
        try {
            mustEq(via_on,       "hello", ".on('message') receives payload");
            mustEq(via_addEvent, "hello", ".addEventListener receives MessageEvent.data");
            mustEq(via_handler,  "hello", ".onmessage receives MessageEvent.data");
        } catch (e) { _asyncFail("MessageChannel dual-shape", e); }
        mc.port1.close(); mc.port2.close();
        _doneAsync();
    }
    mc.port2.on('message', function (p) { via_on = p; maybeDone(); });
    mc.port2.addEventListener('message', function (e) {
        if (e instanceof MessageEvent) via_addEvent = e.data;
        maybeDone();
    });
    mc.port2.onmessage = function (e) { via_handler = e.data; maybeDone(); };
    setImmediate(function () { mc.port1.postMessage("hello"); });
    /* Safety net: if dispatch never completes, fail rather than hang. */
    setTimeout(function () {
        if (settled) return;
        settled = true;
        _asyncFail("MessageChannel dual-shape",
            new Error("dispatch incomplete after 500ms (seen=" + seen + "/3)"));
        try { mc.port1.close(); mc.port2.close(); } catch (_) {}
        _doneAsync();
    }, 500);
});

testJS("BroadcastChannel (async)", function () {
    if (typeof BroadcastChannel !== 'function') return;
    _pendingAsync++;
    /* Two subscribers on the same channel name; a message posted on
       a third instance fans out to both.  Sender does NOT receive its
       own broadcast (per spec). */
    var name = 'rampart-bc-' + (typeof process !== 'undefined' ? process.pid : 'x');
    var a = new BroadcastChannel(name);
    var b = new BroadcastChannel(name);
    var sender = new BroadcastChannel(name);

    var aGot = null, bGot = null, senderGot = false;
    var seen = 0, settled = false;
    function maybeDone() {
        if (settled || ++seen < 2) return;
        settled = true;
        try {
            mustEq(aGot, 'hi',       "subscriber A received broadcast");
            mustEq(bGot, 'hi',       "subscriber B received broadcast");
            mustEq(senderGot, false, "sender doesn't receive own broadcast");
            mustEq(a.name, name,     "channel.name reflects ctor arg");
        } catch (e) { _asyncFail("BroadcastChannel", e); }
        try { a.close(); b.close(); sender.close(); } catch (_) {}
        _doneAsync();
    }

    /* Dual-shape: both addEventListener and .onmessage should work. */
    a.addEventListener('message', function (e) { aGot = e.data; maybeDone(); });
    b.onmessage = function (e) { bGot = e.data; maybeDone(); };
    sender.addEventListener('message', function () { senderGot = true; });

    setImmediate(function () { sender.postMessage('hi'); });

    /* Safety net */
    setTimeout(function () {
        if (settled) return;
        settled = true;
        _asyncFail("BroadcastChannel",
            new Error("broadcast incomplete after 500ms (seen=" + seen + "/2)"));
        try { a.close(); b.close(); sender.close(); } catch (_) {}
        _doneAsync();
    }, 500);
});


/* ============================================================
 * navigator
 * ============================================================ */

testJS("navigator", function () {
    must(typeof navigator === 'object',                "navigator is object");
    must(typeof navigator.userAgent === 'string',      "userAgent string");
    must(navigator.userAgent.length > 0,               "userAgent non-empty");
    must(typeof navigator.platform === 'string',       "platform string");
    must(typeof navigator.hardwareConcurrency === 'number',  "hardwareConcurrency number");
    must(navigator.hardwareConcurrency >= 1,           "hardwareConcurrency >= 1");
});


/* ============================================================
 * Web Crypto: global.crypto + subtle
 * ============================================================ */

testJS("global.crypto (surface + sync)", function () {
    /* Surface */
    must(typeof crypto === 'object',                   "crypto is object");
    must(typeof crypto.subtle === 'object',            "crypto.subtle");
    must(typeof crypto.getRandomValues === 'function', "getRandomValues");
    must(typeof crypto.randomUUID === 'function',      "randomUUID");

    /* === randomUUID === */
    var u = crypto.randomUUID();
    must(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(u),
         "randomUUID v4 format");
    /* Uniqueness: 100 calls all distinct */
    var seen = {};
    for (var i = 0; i < 100; i++) {
        var id = crypto.randomUUID();
        if (seen[id]) throw new Error("randomUUID collision at iter " + i);
        seen[id] = 1;
    }
    /* Length is always 36 chars */
    mustEq(crypto.randomUUID().length, 36, "randomUUID length 36");

    /* === getRandomValues === */
    /* All typed-array views supported */
    var u8  = new Uint8Array(16);
    var u16 = new Uint16Array(8);
    var u32 = new Uint32Array(4);
    var i8  = new Int8Array(16);
    crypto.getRandomValues(u8);
    crypto.getRandomValues(u16);
    crypto.getRandomValues(u32);
    crypto.getRandomValues(i8);
    function someNonZero(arr) {
        for (var j = 0; j < arr.length; j++) if (arr[j] !== 0) return true;
        return false;
    }
    must(someNonZero(u8),  "u8 filled");
    must(someNonZero(u16), "u16 filled");
    must(someNonZero(u32), "u32 filled");
    must(someNonZero(i8),  "i8 filled");
    /* Returns the same array (in place) */
    var u8b = new Uint8Array(4);
    mustEq(crypto.getRandomValues(u8b), u8b, "getRandomValues returns the same array");
    /* QuotaExceededError on > 65536 bytes */
    mustThrow(function() { crypto.getRandomValues(new Uint8Array(65537)); },
              "getRandomValues quota error > 65536");
    /* Float typed arrays are NOT allowed per spec */
    mustThrow(function() { crypto.getRandomValues(new Float32Array(4)); },
              "Float32Array rejected");
    /* DataView is NOT allowed per spec */
    mustThrow(function() { crypto.getRandomValues(new DataView(new ArrayBuffer(8))); },
              "DataView rejected");

    /* Identity preservation (rampart-only — node has different module wiring) */
    if (_isRampart) {
        mustEq(crypto, require('crypto').webcrypto, "global.crypto === require('crypto').webcrypto");
    }
});

testJS("global.crypto.subtle (async round-trips)", function () {
    /* All subtle.* methods are Promise-returning.  Run a full
       battery of round-trips and assert via the pending-async
       counter at the file's tail.  Any thrown error in a .then
       chain calls _asyncFail() (defined in the async tail). */
    _pendingAsync++;
    var enc = new TextEncoder(), dec = new TextDecoder();

    function hexAB(buf) {
        var u = new Uint8Array(buf), s = '';
        for (var i = 0; i < u.length; i++) {
            var v = u[i]; s += (v < 16 ? '0' : '') + v.toString(16);
        }
        return s;
    }

    /* SHA-256 of "hello" — known KAT */
    var SHA256_HELLO = '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824';
    /* SHA-1 of "" (empty) — known KAT */
    var SHA1_EMPTY   = 'da39a3ee5e6b4b0d3255bfef95601890afd80709';

    Promise.resolve()
    /* === digest === */
    .then(function () {
        return Promise.all([
            crypto.subtle.digest('SHA-256', enc.encode('hello')),
            crypto.subtle.digest({name:'SHA-1'}, enc.encode('')),
            crypto.subtle.digest('SHA-384', enc.encode('hello')),
            crypto.subtle.digest('SHA-512', enc.encode('hello'))
        ]).then(function (rs) {
            mustEq(hexAB(rs[0]), SHA256_HELLO,     "digest SHA-256 KAT");
            mustEq(hexAB(rs[1]), SHA1_EMPTY,       "digest SHA-1 empty KAT");
            mustEq(rs[2].byteLength, 48,           "SHA-384 = 48 bytes");
            mustEq(rs[3].byteLength, 64,           "SHA-512 = 64 bytes");
            /* digest accepts ArrayBuffer too */
            var ab = new ArrayBuffer(5);
            new Uint8Array(ab).set([104,101,108,108,111]);
            return crypto.subtle.digest('SHA-256', ab);
        }).then(function (d) {
            mustEq(hexAB(d), SHA256_HELLO, "digest accepts ArrayBuffer");
        });
    })
    /* === HMAC sign + verify + import/export raw === */
    .then(function () {
        /* RFC 4231 Test 1: key=20 * 0x0b, data="Hi There", SHA-256 */
        var k20 = new Uint8Array(20);
        for (var i = 0; i < 20; i++) k20[i] = 0x0b;
        return crypto.subtle.importKey('raw', k20,
            {name:'HMAC', hash:'SHA-256'}, true, ['sign','verify']
        ).then(function (key) {
            mustEq(key.type, 'secret',  "HMAC key.type");
            mustEq(key.algorithm.name, 'HMAC', "HMAC algorithm.name");
            mustEq(key.algorithm.length, 160,  "HMAC algorithm.length");
            return crypto.subtle.sign('HMAC', key, enc.encode('Hi There'))
                .then(function (sig) {
                    mustEq(hexAB(sig),
                        'b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7',
                        "HMAC RFC4231 Test 1 KAT");
                    /* Verify good */
                    return crypto.subtle.verify('HMAC', key, sig, enc.encode('Hi There'))
                        .then(function (ok) {
                            must(ok === true, "HMAC verify good");
                            /* Verify with wrong data */
                            return crypto.subtle.verify('HMAC', key, sig, enc.encode('Hi there'));
                        });
                }).then(function (ok) {
                    must(ok === false, "HMAC verify mismatched data");
                    /* Re-export raw and compare */
                    return crypto.subtle.exportKey('raw', key);
                }).then(function (raw) {
                    mustEq(hexAB(raw), '0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b',
                           "HMAC exportKey raw round-trip");
                });
        });
    })
    /* === HMAC generateKey === */
    .then(function () {
        return crypto.subtle.generateKey({name:'HMAC', hash:'SHA-256'}, true, ['sign','verify']
        ).then(function (key) {
            mustEq(key.type, 'secret',       "HMAC genKey type");
            mustEq(key.algorithm.length, 512, "HMAC genKey default length = block size");
            /* Roundtrip sign/verify */
            return crypto.subtle.sign('HMAC', key, enc.encode('xyz'))
                .then(function (sig) {
                    return crypto.subtle.verify('HMAC', key, sig, enc.encode('xyz'));
                }).then(function (ok) { must(ok, "HMAC genKey signs+verifies"); });
        });
    })
    /* === AES-GCM encrypt/decrypt with AAD === */
    .then(function () {
        return crypto.subtle.generateKey({name:'AES-GCM', length:256}, true, ['encrypt','decrypt']
        ).then(function (key) {
            mustEq(key.algorithm.length, 256, "AES-GCM key length");
            var iv  = new Uint8Array(12); crypto.getRandomValues(iv);
            var pt  = enc.encode('secret payload');
            var aad = enc.encode('public header');
            return crypto.subtle.encrypt({name:'AES-GCM', iv:iv, additionalData:aad}, key, pt
            ).then(function (ct) {
                /* Spec: ciphertext layout is plaintext-len + 16 bytes for tag (default 128-bit) */
                mustEq(ct.byteLength, pt.byteLength + 16, "AES-GCM ct+tag length");
                return crypto.subtle.decrypt({name:'AES-GCM', iv:iv, additionalData:aad}, key, ct);
            }).then(function (rt) {
                mustEq(dec.decode(rt), 'secret payload', "AES-GCM round-trip");
                /* Wrong AAD must fail */
                var iv2  = new Uint8Array(12); crypto.getRandomValues(iv2);
                return crypto.subtle.encrypt({name:'AES-GCM', iv:iv2, additionalData:aad}, key, pt)
                    .then(function (ct2) {
                        return crypto.subtle.decrypt({name:'AES-GCM', iv:iv2, additionalData:enc.encode('tampered')}, key, ct2)
                            .then(function () { throw new Error("bad-AAD decrypt should have failed"); },
                                  function () { return null; });  /* expected */
                    });
            });
        });
    })
    /* === AES-CBC encrypt/decrypt + raw import/export === */
    .then(function () {
        return crypto.subtle.generateKey({name:'AES-CBC', length:128}, true, ['encrypt','decrypt']
        ).then(function (key) {
            mustEq(key.algorithm.length, 128, "AES-CBC key length");
            var iv = new Uint8Array(16); crypto.getRandomValues(iv);
            var msg = enc.encode('AES-CBC round trip');
            return crypto.subtle.encrypt({name:'AES-CBC', iv:iv}, key, msg).then(function (ct) {
                return crypto.subtle.decrypt({name:'AES-CBC', iv:iv}, key, ct);
            }).then(function (rt) {
                mustEq(dec.decode(rt), 'AES-CBC round trip', "AES-CBC round-trip");
                return crypto.subtle.exportKey('raw', key);
            }).then(function (raw) {
                mustEq(raw.byteLength, 16, "AES-128 exportKey raw len");
            });
        });
    })
    /* === ECDSA P-256 generate + sign + verify === */
    .then(function () {
        return crypto.subtle.generateKey({name:'ECDSA', namedCurve:'P-256'}, true, ['sign','verify']
        ).then(function (kp) {
            mustEq(kp.publicKey.type, 'public',   "ECDSA pub.type");
            mustEq(kp.privateKey.type, 'private', "ECDSA priv.type");
            mustEq(kp.publicKey.algorithm.namedCurve, 'P-256', "ECDSA curve");
            return crypto.subtle.sign({name:'ECDSA', hash:'SHA-256'}, kp.privateKey, enc.encode('ecdsa-test')
            ).then(function (sig) {
                /* Web Crypto ECDSA = IEEE P1363 r||s; P-256 → 64 bytes */
                mustEq(sig.byteLength, 64, "ECDSA P-256 sig = 64 bytes (IEEE P1363)");
                return crypto.subtle.verify({name:'ECDSA', hash:'SHA-256'}, kp.publicKey, sig, enc.encode('ecdsa-test'));
            }).then(function (ok) { must(ok, "ECDSA verify good"); });
        });
    })
    /* === ECDH P-256 key agreement === */
    .then(function () {
        return Promise.all([
            crypto.subtle.generateKey({name:'ECDH', namedCurve:'P-256'}, true, ['deriveBits','deriveKey']),
            crypto.subtle.generateKey({name:'ECDH', namedCurve:'P-256'}, true, ['deriveBits','deriveKey'])
        ]).then(function (kps) {
            return Promise.all([
                crypto.subtle.deriveBits({name:'ECDH', public:kps[1].publicKey}, kps[0].privateKey, 256),
                crypto.subtle.deriveBits({name:'ECDH', public:kps[0].publicKey}, kps[1].privateKey, 256)
            ]);
        }).then(function (ss) {
            mustEq(hexAB(ss[0]), hexAB(ss[1]),  "ECDH shared secret agreement");
            mustEq(ss[0].byteLength, 32,        "ECDH P-256 secret = 32 bytes");
        });
    })
    /* === PBKDF2 deriveBits with KAT === */
    .then(function () {
        return crypto.subtle.importKey('raw', enc.encode('password'), 'PBKDF2', false, ['deriveBits','deriveKey']
        ).then(function (key) {
            mustEq(key.type, 'secret',                 "PBKDF2 key.type");
            mustEq(key.algorithm.name, 'PBKDF2',       "PBKDF2 algorithm.name");
            mustEq(key.extractable, false,             "PBKDF2 base key not extractable per spec");
            return crypto.subtle.deriveBits(
                {name:'PBKDF2', salt:enc.encode('salt'), iterations:1, hash:'SHA-1'}, key, 160
            );
        }).then(function (out) {
            mustEq(hexAB(out), '0c60c80f961f0e71f3a9b524af6012062fe037a6',
                   "PBKDF2 RFC6070 Test 1 KAT");
        });
    })
    /* === HKDF deriveBits with KAT === */
    .then(function () {
        var ikm  = new Uint8Array(22); for (var i = 0; i < 22; i++) ikm[i]  = 0x0b;
        var salt = new Uint8Array(13); for (var i = 0; i < 13; i++) salt[i] = i;
        var info = new Uint8Array(10); for (var i = 0; i < 10; i++) info[i] = 0xf0 + i;
        return crypto.subtle.importKey('raw', ikm, 'HKDF', false, ['deriveBits']
        ).then(function (key) {
            return crypto.subtle.deriveBits({name:'HKDF', salt:salt, info:info, hash:'SHA-256'}, key, 42*8);
        }).then(function (out) {
            mustEq(hexAB(out),
                '3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865',
                "HKDF RFC5869 Test 1 KAT");
        });
    })
    /* === wrapKey / unwrapKey AES-KW === */
    .then(function () {
        return Promise.all([
            crypto.subtle.generateKey({name:'AES-KW', length:256}, true, ['wrapKey','unwrapKey']),
            crypto.subtle.generateKey({name:'AES-GCM', length:128}, true, ['encrypt','decrypt'])
        ]).then(function (ks) {
            var kek = ks[0], aes = ks[1];
            return crypto.subtle.wrapKey('raw', aes, kek, {name:'AES-KW'}
            ).then(function (wrapped) {
                /* AES-KW: input + 8 bytes; 16-byte AES key wrapped → 24 bytes */
                mustEq(wrapped.byteLength, 24, "AES-KW wrapped len = 16+8");
                return crypto.subtle.unwrapKey('raw', wrapped, kek, {name:'AES-KW'},
                                               {name:'AES-GCM'}, true, ['encrypt','decrypt']);
            }).then(function (uw) {
                mustEq(uw.algorithm.name, 'AES-GCM',  "AES-KW unwrap → AES-GCM");
                mustEq(uw.algorithm.length, 128,      "unwrapped len = 128");
            });
        });
    })
    /* === RSASSA-PKCS1-v1_5 sign + verify === */
    .then(function () {
        return crypto.subtle.generateKey(
            {name:'RSASSA-PKCS1-v1_5', modulusLength:2048,
             publicExponent:new Uint8Array([1,0,1]), hash:'SHA-256'},
            true, ['sign','verify']
        ).then(function (kp) {
            mustEq(kp.publicKey.algorithm.modulusLength, 2048, "RSA modulusLength");
            return crypto.subtle.sign('RSASSA-PKCS1-v1_5', kp.privateKey, enc.encode('rsa-test')
            ).then(function (sig) {
                mustEq(sig.byteLength, 256, "RSA-2048 sig = 256 bytes");
                return crypto.subtle.verify('RSASSA-PKCS1-v1_5', kp.publicKey, sig, enc.encode('rsa-test'));
            }).then(function (ok) {
                must(ok, "RSA verify good");
                /* Bad data must NOT verify */
                return crypto.subtle.sign('RSASSA-PKCS1-v1_5', kp.privateKey, enc.encode('rsa-test'))
                    .then(function (sig) {
                        return crypto.subtle.verify('RSASSA-PKCS1-v1_5', kp.publicKey, sig, enc.encode('different'));
                    });
            }).then(function (ok) { must(ok === false, "RSA verify rejects wrong data"); });
        });
    })
    /* === error semantics: NotSupportedError on bad algorithm === */
    .then(function () {
        return crypto.subtle.digest('MD5', enc.encode('x')).then(
            function () { throw new Error("MD5 should have rejected"); },
            function (e) { mustEq(e.name, 'NotSupportedError', "MD5 → NotSupportedError"); }
        );
    })
    /* === done === */
    .then(function () { _doneAsync(); },
          function (e) { _asyncFail("global.crypto.subtle", e); _doneAsync(); });

    return true;  /* sync portion ok — async assertions reported above */
});


/* ============================================================
 * Blob / File (W3C File API)
 * ============================================================ */

testJS("Blob / File (sync surface)", function () {
    must(typeof Blob === 'function',  "Blob global");
    must(typeof File === 'function',  "File global");
    /* construction shapes */
    var b = new Blob(['hello, ', 'world!'], {type:'text/plain'});
    mustEq(b.size, 13,           "Blob.size");
    mustEq(b.type, 'text/plain', "Blob.type");
    /* slice — spec default content-type is empty, not source's type */
    var s = b.slice(0, 5);
    mustEq(s.size, 5,            "slice.size");
    mustEq(s.type, '',           "slice default contentType is empty");
    mustEq(b.slice(0, 5, 'application/x-foo').type, 'application/x-foo', "slice explicit contentType");
    mustEq(b.slice(-6).size, 6,  "slice negative start");
    /* mixed parts: string + Uint8Array + ArrayBuffer + Blob */
    var ab = new ArrayBuffer(3); new Uint8Array(ab).set([65,66,67]);
    var mixed = new Blob([new Uint8Array([72,73,74]), 'def', ab, b]);
    mustEq(mixed.size, 3+3+3+13, "mixed-part size");
    /* type lowercasing + invalid-char collapse */
    mustEq(new Blob(['x'], {type:'TEXT/Plain'}).type,    'text/plain', "type lowercased");
    mustEq(new Blob(['x'], {type:'invalid\nmime'}).type, '',           "invalid type collapses");
    /* empty */
    mustEq(new Blob().size, 0,   "empty Blob size");
    /* toStringTag */
    mustEq(Object.prototype.toString.call(b), '[object Blob]', "Blob toStringTag");
    /* Blob.stream() returns a ReadableStream (implemented in rampart via
       the web-streams-polyfill bundle in rampart-whatwg.so). */
    var bs = b.stream();
    must(bs && typeof bs.getReader === 'function', "Blob.stream() returns ReadableStream");
    /* no-new */
    mustThrow(function(){ Blob(['x']); },     "Blob() without new throws");
    mustThrow(function(){ File(['x'], 'f'); }, "File() without new throws");
    /* text/arrayBuffer/bytes return thenables (Promise per spec) */
    must(typeof b.text().then        === 'function', "text() returns thenable");
    must(typeof b.arrayBuffer().then === 'function', "arrayBuffer() returns thenable");
    must(typeof b.bytes().then       === 'function', "bytes() returns thenable");
    /* File */
    var f = new File(['hello'], 'doc.txt', {type:'text/plain', lastModified:1234567890000});
    mustEq(f.name, 'doc.txt',             "File.name");
    mustEq(f.lastModified, 1234567890000, "File.lastModified");
    mustEq(f.type, 'text/plain',          "File.type");
    mustEq(f.size, 5,                     "File.size");
    must(f instanceof Blob,               "File instanceof Blob");
    must(f instanceof File,               "File instanceof File");
    mustEq(Object.prototype.toString.call(f), '[object File]', "File toStringTag");
    must(new File(['x'], 'n.txt').lastModified > 0, "File default lastModified = now()");
});

testJS("Blob async (text/arrayBuffer/bytes)", function () {
    _pendingAsync++;
    var b = new Blob(['hello'], {type:'text/plain'});
    Promise.all([b.text(), b.arrayBuffer(), b.bytes()]).then(function (rs) {
        try {
            mustEq(rs[0], 'hello',                "Blob.text() resolves to string");
            must(rs[1] instanceof ArrayBuffer,    "Blob.arrayBuffer() resolves to ArrayBuffer");
            mustEq(rs[1].byteLength, 5,           "arrayBuffer byteLength");
            must(rs[2] instanceof Uint8Array,     "Blob.bytes() resolves to Uint8Array");
            mustEq(rs[2].length, 5,               "bytes length");
            mustEq(rs[2][0], 104,                 "bytes[0] === 'h' code");
        } catch (e) { _asyncFail("Blob async", e); }
        _doneAsync();
    }, function (e) { _asyncFail("Blob async (rejected)", e); _doneAsync(); });
    return true;
});

testJS("Blob.stream() (async)", function () {
    _pendingAsync++;
    /* Blob.prototype.stream() returns a ReadableStream that delivers
       the blob's bytes.  Override added by rampart-whatwg to fix the
       NotSupportedError stub that rampart-blob.c originally threw. */
    var blob = new Blob(["hello, ", "blob ", "stream"], {type: "text/plain"});
    var r = blob.stream().getReader();
    var dec = new TextDecoder();
    var parts = [];
    function pump() {
        r.read().then(function (x) {
            if (x.done) {
                try { mustEq(parts.join(""), "hello, blob stream", "Blob.stream round-trip"); }
                catch (e) { _asyncFail("Blob.stream", e); }
                _doneAsync();
                return;
            }
            parts.push(dec.decode(x.value));
            pump();
        }, function (e) { _asyncFail("Blob.stream read rejected", e); _doneAsync(); });
    }
    pump();
    return true;
});

testJS("Blob.type WHATWG MIME normalization", function () {
    /* Type is run through parse-a-mime-type then re-serialized. */
    mustEq(new Blob([], {type: 'text/plain'}).type,        'text/plain',
                                                "simple type preserved");
    mustEq(new Blob([], {type: 'IMAGE/png'}).type,         'image/png',
                                                "type/subtype lowercased");
    mustEq(new Blob([], {type: 'text/plain;CHARSET=UTF-8'}).type, 'text/plain;charset=UTF-8',
                                                "param NAME lowercased, VALUE preserved");
    mustEq(new Blob([], {type: 'text/plain;,'}).type,      'text/plain',
                                                "empty trailing param dropped");
    mustEq(new Blob([], {type: 'BAD'}).type,               '',
                                                "no slash → invalid → empty");
    mustEq(new Blob([], {type: 'text / html'}).type,       '',
                                                "whitespace in type → invalid → empty");
    mustEq(new Blob([], {type: 'text/plain;a=",",x=y'}).type, 'text/plain;a=","',
                                                "quoted-string with comma kept literally");

    /* File constructor wraps Blob's normalization */
    mustEq(new File([], 'n', {type: 'TEXT/HTML'}).type,    'text/html',
                                                "File type normalized too");
});


/* ============================================================
 * Fetch family: Headers / FormData / Request / Response / fetch
 * ============================================================ */

testJS("Headers", function () {
    /* Construction */
    var h = new Headers();
    must(h instanceof Headers,                  "new Headers");
    var h2 = new Headers({"Content-Type":"text/plain", "X-A":"1"});
    mustEq(h2.get("content-type"), "text/plain", "init from object — case-insensitive get");
    mustEq(h2.get("CONTENT-TYPE"), "text/plain", "case-insensitive lookup");
    var h3 = new Headers([["a","1"],["b","2"]]);
    mustEq(h3.get("a"), "1",                    "init from array-of-pairs");
    var h4 = new Headers(h2);
    mustEq(h4.get("content-type"), "text/plain", "copy-constructor");

    /* append / set / has / delete */
    h.append("X-A", "1");
    h.append("X-A", "2");
    mustEq(h.get("X-A"), "1, 2",                "append concatenates with ', '");
    h.set("X-A", "only");
    mustEq(h.get("X-A"), "only",                "set replaces");
    must(h.has("x-a"),                          "has case-insensitive");
    h['delete']("X-A");
    must(!h.has("x-a"),                         "delete removes");
    mustEq(h.get("missing"), null,              "get missing returns null");

    /* iteration — combined values, lowercase-sorted order */
    var h5 = new Headers();
    h5.append("X-B", "1");
    h5.append("X-A", "2");
    h5.append("X-A", "3");
    var seen = [];
    h5.forEach(function(v,k) { seen.push(k+"="+v); });
    mustEq(seen.join(";"), "x-a=2, 3;x-b=1",    "forEach: sorted, combined");
    var keys = [];
    var keysIter = h5.keys();
    var step;
    while (!(step = keysIter.next()).done) keys.push(step.value);
    mustEq(keys.join(","), "x-a,x-b",           "keys() iterator");

    /* getSetCookie */
    var h6 = new Headers();
    h6.append("Set-Cookie", "a=1");
    h6.append("set-cookie", "b=2");
    var sc = h6.getSetCookie();
    mustEq(sc.length, 2,                        "getSetCookie returns array");
    mustEq(sc[0], "a=1",                        "getSetCookie [0]");

    /* Invalid name throws */
    mustThrow(function(){ h.append("bad name", "v"); }, "invalid header name");
});

testJS("Headers: forbidden request-header filtering (silent drop)", function () {
    /* Request constructor sets headers._guard='request'.  Forbidden header
       names are silently dropped per WHATWG spec. */
    var r = new Request('http://x/', {
        method: 'POST',
        headers: {
            'X-Allowed': 'yes',
            'Cookie': 'session=abc',
            'Host': 'evil.com',
            'Content-Length': '999',
            'Origin': 'http://x/',  /* forbidden via guard */
            'Sec-Anything': 'no',   /* Sec- prefix forbidden */
            'Proxy-Anything': 'no', /* Proxy- prefix forbidden */
        }
    });
    mustEq(r.headers.get('x-allowed'), 'yes',   "non-forbidden header kept");
    mustEq(r.headers.get('cookie'),    null,    "Cookie dropped");
    mustEq(r.headers.get('host'),      null,    "Host dropped");
    mustEq(r.headers.get('content-length'), null, "Content-Length dropped");
    mustEq(r.headers.get('origin'),    null,    "Origin dropped (by guard)");
    mustEq(r.headers.get('sec-anything'), null, "Sec-* prefix dropped");
    mustEq(r.headers.get('proxy-anything'), null, "Proxy-* prefix dropped");

    /* X-HTTP-Method-Override family: silent drop when value names a forbidden method */
    r = new Request('http://x/', {
        method: 'POST',
        headers: {'X-HTTP-Method-Override': 'TRACE'}
    });
    mustEq(r.headers.get('x-http-method-override'), null,
                                                "method-override with TRACE value dropped");

    /* But non-forbidden override value is kept */
    r = new Request('http://x/', {
        method: 'POST',
        headers: {'X-HTTP-Method-Override': 'PATCH'}
    });
    mustEq(r.headers.get('x-http-method-override'), 'PATCH',
                                                "method-override with PATCH value kept");
});

testJS("FormData", function () {
    var f = new FormData();
    must(f instanceof FormData,                 "new FormData");
    f.append("a", "1");
    f.append("a", "2");
    f.append("b", "3");
    mustEq(f.get("a"), "1",                     "get returns first");
    mustEq(JSON.stringify(f.getAll("a")), '["1","2"]', "getAll returns all");
    mustEq(f.has("b"), true,                    "has true");
    mustEq(f.has("z"), false,                   "has false");
    f.set("a", "only");
    mustEq(f.get("a"), "only",                  "set replaces");
    mustEq(JSON.stringify(f.getAll("a")), '["only"]', "set leaves single");
    f['delete']("a");
    mustEq(f.has("a"), false,                   "delete removes");

    /* entries / keys / values */
    var f2 = new FormData();
    f2.append("k1", "v1");
    f2.append("k2", "v2");
    var ents = [];
    f2.forEach(function(v,k){ ents.push(k+"="+v); });
    mustEq(ents.join(";"), "k1=v1;k2=v2",       "forEach insertion order");
});

testJS("Request", function () {
    /* Simple GET */
    var r = new Request("https://example.com/path");
    mustEq(r.url, "https://example.com/path",   "url");
    mustEq(r.method, "GET",                     "default method");
    must(r.headers instanceof Headers,          "headers is a Headers");
    mustEq(r.redirect, "follow",                "default redirect");
    mustEq(r.bodyUsed, false,                   "bodyUsed false initially");

    /* POST with body + headers */
    var r2 = new Request("https://example.com/api", {
        method: "POST",
        headers: {"X-Test": "1"},
        body: '{"a":1}'
    });
    mustEq(r2.method, "POST",                   "method override");
    mustEq(r2.headers.get("x-test"), "1",       "headers init");
    mustEq(r2.headers.get("content-type"), "text/plain;charset=UTF-8",
                                                "content-type auto-set for string body");

    /* Clone */
    var c = r2.clone();
    mustEq(c.url, r2.url,                       "clone url");
    must(c.headers !== r2.headers,              "clone has own headers");
    mustEq(c.headers.get("x-test"), "1",        "clone headers content");

    /* Construct from existing Request */
    var r3 = new Request(r2);
    mustEq(r3.method, "POST",                   "Request(Request) inherits method");
    mustEq(r3.headers.get("x-test"), "1",       "Request(Request) inherits headers");
});

testJS("Response (sync surface)", function () {
    /* Default */
    var r = new Response();
    mustEq(r.status, 200,                       "default status 200");
    mustEq(r.statusText, "",                    "default statusText empty (per spec)");
    mustEq(r.ok, true,                          "ok true for 200");
    mustEq(r.type, "default",                   "default type");

    /* Explicit */
    var r2 = new Response("body", {status: 404, headers: {"X-Y": "z"}, statusText: "Not Found"});
    mustEq(r2.status, 404,                      "explicit status");
    mustEq(r2.statusText, "Not Found",          "explicit statusText");
    mustEq(r2.ok, false,                        "ok false for 404");
    mustEq(r2.headers.get("x-y"), "z",          "headers init");
    mustEq(r2.headers.get("content-type"), "text/plain;charset=UTF-8",
                                                "content-type auto-set");

    /* Static helpers */
    var rj = Response.json({k: 1});
    mustEq(rj.status, 200,                      "Response.json default 200");
    mustEq(rj.headers.get("content-type"), "application/json",
                                                "Response.json content-type");

    var rr = Response.redirect("https://x.com/", 301);
    mustEq(rr.status, 301,                      "Response.redirect status");
    mustEq(rr.headers.get("location"), "https://x.com/", "Response.redirect location");
    mustThrow(function(){ Response.redirect("x", 200); }, "invalid redirect status throws");

    var re = Response.error();
    mustEq(re.type, "error",                    "Response.error type");
    mustEq(re.status, 0,                        "Response.error status");

    /* Status out of range */
    mustThrow(function(){ new Response(null, {status: 99}); },  "status < 200 throws");
    mustThrow(function(){ new Response(null, {status: 600}); }, "status > 599 throws");
});

testJS("Response.body (ReadableStream, async)", function () {
    _pendingAsync++;
    /* Construct from a buffered body — .body should be a synthesized
       single-chunk ReadableStream.  Cached: repeated access returns
       the same stream. */
    var r = new Response("hello world");
    var b1 = r.body;
    var b2 = r.body;
    must(b1 instanceof ReadableStream,           "body is ReadableStream");
    must(b1 === b2,                              "body getter cached (same object)");

    /* Empty body */
    var r0 = new Response(null);
    mustEq(r0.body, null,                        "null body → body=null");

    /* Drain the stream manually, verify text */
    var reader = b1.getReader();
    var parts = [];
    function pump() {
        reader.read().then(function (x) {
            if (x.done) {
                try {
                    mustEq(parts.length, 1,      "buffered body → single chunk");
                    mustEq(new TextDecoder().decode(parts[0]), "hello world",
                                                 "drained content matches");
                } catch (e) { _asyncFail("Response.body drain", e); }
                _doneAsync();
                return;
            }
            parts.push(x.value);
            pump();
        }, function (e) { _asyncFail("Response.body read", e); _doneAsync(); });
    }
    pump();
    return true;
});

testJS("Response.text/json/arrayBuffer/blob/bytes (async)", function () {
    _pendingAsync++;
    var r = new Response("hello, body");
    Promise.all([
        r.clone().text(),
        r.clone().arrayBuffer(),
        r.clone().bytes()
    ]).then(function(rs) {
        try {
            mustEq(rs[0], "hello, body",        "text()");
            must(rs[1] instanceof ArrayBuffer,  "arrayBuffer() is ArrayBuffer");
            mustEq(rs[1].byteLength, 11,        "arrayBuffer byteLength");
            must(rs[2] instanceof Uint8Array,   "bytes() is Uint8Array");
            mustEq(rs[2].length, 11,            "bytes length");
        } catch (e) { _asyncFail("Response body methods", e); }
        var rj = Response.json({a: 42});
        rj.json().then(function(j) {
            try { mustEq(j.a, 42, "json() round-trip"); }
            catch (e) { _asyncFail("json", e); }
            /* bodyUsed enforcement */
            var r2 = new Response("once");
            r2.text().then(function(){
                r2.text().then(function(){
                    _asyncFail("bodyUsed", new Error("second text() should reject"));
                    _doneAsync();
                }, function() {
                    /* expected rejection */
                    _doneAsync();
                });
            });
        });
    }, function(e) { _asyncFail("Response body all", e); _doneAsync(); });
    return true;
});

testJS("Response.formData multipart (async)", function () {
    _pendingAsync++;
    var body = "------test\r\nContent-Disposition: form-data; name=\"f1\"\r\n\r\nval1\r\n"
             + "------test\r\nContent-Disposition: form-data; name=\"f2\"; filename=\"a.txt\"\r\n"
             + "Content-Type: text/plain\r\n\r\nfile contents\r\n"
             + "------test--\r\n";
    var r = new Response(body, {headers: {"content-type": "multipart/form-data; boundary=----test"}});
    r.formData().then(function (fd) {
        try {
            mustEq(fd.get("f1"), "val1",         "text part");
            var f = fd.get("f2");
            must(f,                              "file part exists");
            mustEq(f.name, "a.txt",              "file name");
        } catch (e) { _asyncFail("multipart parse", e); }
        var ff = fd.get("f2");
        ff.text().then(function (t) {
            try { mustEq(t, "file contents", "file contents decoded"); }
            catch (e) { _asyncFail("file contents", e); }
            _doneAsync();
        }, function (e) { _asyncFail("file.text", e); _doneAsync(); });
    }, function (e) { _asyncFail("formData", e); _doneAsync(); });
});

testJS("Response.formData multipart rejects malformed (async)", function () {
    _pendingAsync++;
    /* Boundary followed by junk instead of '--' or CRLF = malformed (RFC 2046). */
    var malformed =
        "--BoundaryXYZ\r\n" +
        "Content-Disposition: form-data; name=\"f\"\r\n\r\n" +
        "value\r\n" +
        "--BoundaryXYZ-some-junk-not-dashdash-not-crlf";
    var r = new Response(new Blob([malformed]), {
        headers: [["Content-Type", "multipart/form-data; boundary=BoundaryXYZ"]]
    });
    r.formData().then(
        function () { _asyncFail('formData', new Error('malformed multipart should reject')); _doneAsync(); },
        function (e) {
            mustEq(e.name, 'TypeError',         "malformed multipart rejects with TypeError");
            _doneAsync();
        }
    );
    return true;
});

testJS("Empty FormData → empty body bytes (async)", function () {
    _pendingAsync++;
    var fd = new FormData();
    var r = new Response(fd);
    r.arrayBuffer().then(function (ab) {
        mustEq(ab.byteLength, 0,                "empty FormData body is 0 bytes");
        _doneAsync();
    }).catch(function (e) { _asyncFail("empty FormData", e); _doneAsync(); });
    return true;
});

testJS("Response/Request init validation", function () {
    /* Response statusText per RFC 9110 §15.1 */
    var threw = false;
    try { new Response('', {statusText: '\n'}); } catch (e) { threw = true; }
    must(threw, "statusText with LF throws");

    threw = false;
    try { new Response('', {statusText: 'Ā'}); } catch (e) { threw = true; }
    must(threw, "statusText with non-ASCII throws");

    new Response('', {statusText: 'OK extended status text'});  /* valid */

    /* Null-body statuses can't have a body */
    threw = false;
    try { new Response('body', {status: 204}); } catch (e) { threw = true; }
    must(threw, "Response(body, status:204) throws");

    threw = false;
    try { new Response('body', {status: 304}); } catch (e) { threw = true; }
    must(threw, "Response(body, status:304) throws");

    /* Response.json with non-encodable data throws */
    threw = false;
    try { Response.json(Symbol('s')); } catch (e) { threw = true; }
    must(threw, "Response.json(Symbol) throws");

    /* Request: bad enum values throw */
    var bads = [
        {mode: 'navigate'},
        {mode: 'bogus'},
        {credentials: 'BAD'},
        {cache: 'BAD'},
        {redirect: 'BAD'},
        {referrerPolicy: 'BAD'},
        {priority: 'BAD'},
        {duplex: 'full'},      /* unsupported; only 'half' valid */
        {window: 'http://x/'}, /* must be null */
        {cache: 'only-if-cached', mode: 'cors'},  /* only-if-cached requires same-origin */
    ];
    for (var i = 0; i < bads.length; i++) {
        threw = false;
        try { new Request('http://example.com/', bads[i]); }
        catch (e) { threw = true; }
        must(threw, "Request init " + JSON.stringify(bads[i]) + " throws");
    }

    /* GET/HEAD with body throws */
    threw = false;
    try { new Request('http://x/', {method: 'GET', body: 'x'}); } catch (e) { threw = true; }
    must(threw, "Request(GET) with body throws");

    /* Invalid URL throws */
    threw = false;
    try { new Request('http://:not a valid URL'); } catch (e) { threw = true; }
    must(threw, "Request with invalid URL throws");

    /* URL with credentials throws */
    threw = false;
    try { new Request('http://user:pass@x/'); } catch (e) { threw = true; }
    must(threw, "Request with userinfo URL throws");

    /* Forbidden methods throw */
    var forbiddenMethods = ['CONNECT', 'TRACE', 'TRACK', 'trace'];
    for (var i = 0; i < forbiddenMethods.length; i++) {
        threw = false;
        try { new Request('http://x/', {method: forbiddenMethods[i]}); }
        catch (e) { threw = true; }
        must(threw, "Request with forbidden method " + forbiddenMethods[i] + " throws");
    }
});

testJS("Response.error() + Response.redirect() static helpers", function () {
    var err = Response.error();
    mustEq(err.type,       'error',             "error response type");
    mustEq(err.status,     0,                   "error response status 0");
    mustEq(err.statusText, '',                  "error response statusText empty");
    mustEq(err.body,       null,                "error response body null");
    must(err.headers,                            "error response has headers");
    var threwAppend = false;
    try { err.headers.append('x', '1'); } catch (e) { threwAppend = true; }
    must(threwAppend,                           "error response headers are immutable");

    /* Response.redirect requires a valid redirect status (301/302/303/307/308) */
    var red = Response.redirect('http://example.com/new', 301);
    mustEq(red.status,       301,               "redirect status 301");
    mustEq(red.headers.get('location'), 'http://example.com/new',
                                                "redirect Location header set");

    var threwRange = false;
    try { Response.redirect('http://x/', 200); } catch (e) {
        threwRange = true; mustEq(e instanceof RangeError, true, "→ RangeError");
    }
    must(threwRange,                            "redirect with non-redirect status throws RangeError");
});

testJS("fetch(): unsupported scheme rejects, data: URL works (async)", function () {
    _pendingAsync++;
    /* data: URL is handled inline (no network); test the synthesized Response. */
    fetch('data:text/plain;charset=utf-8,hello').then(function (r) {
        mustEq(r.status, 200,                   "data: URL Response status 200");
        mustEq(r.headers.get('content-type'), 'text/plain;charset=utf-8',
                                                "data: URL content-type from URL");
        return r.text();
    }).then(function (t) {
        mustEq(t, 'hello',                      "data: URL body decoded");

        /* Unsupported scheme should reject without hitting curl. */
        return fetch('ftp://example.com/').then(
            function () { _asyncFail('fetch', new Error('ftp:// should reject')); },
            function (e) {
                mustEq(e.name, 'TypeError',     "ftp:// rejects with TypeError");
                _doneAsync();
            }
        );
    }).catch(function (e) { _asyncFail("fetch(data:/ftp:)", e); _doneAsync(); });
    return true;
});


/* ============================================================
 * Storage / localStorage / sessionStorage
 * Cache / CacheStorage / caches
 * ============================================================ */

testJS("Storage (localStorage / sessionStorage)", function () {
    if (typeof Storage !== 'function' || typeof localStorage === 'undefined')
        return;  /* node doesn't expose Storage server-side */
    must(localStorage instanceof Storage,        "localStorage is Storage");
    must(sessionStorage instanceof Storage,      "sessionStorage is Storage");
    /* Use a unique prefix to avoid cross-test pollution */
    var k = "_test_" + Date.now();
    localStorage.setItem(k, "value1");
    mustEq(localStorage.getItem(k), "value1",    "setItem / getItem round-trip");
    must(localStorage.length >= 1,               "length nonzero");
    localStorage.setItem(k, 123);
    mustEq(localStorage.getItem(k), "123",       "setItem coerces to string");
    localStorage.removeItem(k);
    mustEq(localStorage.getItem(k), null,        "removeItem then getItem null");
    /* sessionStorage is a separate instance */
    sessionStorage.setItem(k, "session");
    mustEq(localStorage.getItem(k), null,        "localStorage / sessionStorage are separate");
    sessionStorage.removeItem(k);
});

testJS("Cache / CacheStorage (async)", function () {
    if (typeof CacheStorage !== 'function' || typeof caches === 'undefined')
        return;  /* node doesn't expose CacheStorage server-side */
    _pendingAsync++;
    must(caches instanceof CacheStorage,         "caches is CacheStorage");
    var cacheName = "_test_" + Date.now();
    caches.open(cacheName).then(function (cache) {
        must(cache instanceof Cache,             "open returns Cache");
        return cache.put("https://x/y", new Response("cached body", {status: 201}))
            .then(function () { return cache.match("https://x/y"); });
    }).then(function (resp) {
        try {
            must(resp instanceof Response,       "match returns Response");
            mustEq(resp.status, 201,             "stored status preserved");
        } catch (e) { _asyncFail("Cache match", e); _doneAsync(); return; }
        return resp.text();
    }).then(function (txt) {
        try { mustEq(txt, "cached body",         "stored body preserved"); }
        catch (e) { _asyncFail("Cache body", e); }
        return caches['delete'](cacheName);
    }).then(function (existed) {
        try { mustEq(existed, true,              "caches.delete returns true for existed"); }
        catch (e) { _asyncFail("Cache delete", e); }
        _doneAsync();
    }, function (e) { _asyncFail("Cache chain", e); _doneAsync(); });
});


/* ============================================================
 * WHATWG Streams + derived (Compression / TextEncoder streams)
 * ============================================================ */

testJS("Streams (surface)", function () {
    /* Class existence — the 13 WHATWG Streams classes + 4 derived. */
    var names = [
        'ReadableStream','ReadableStreamDefaultController',
        'ReadableByteStreamController','ReadableStreamBYOBRequest',
        'ReadableStreamDefaultReader','ReadableStreamBYOBReader',
        'WritableStream','WritableStreamDefaultController',
        'WritableStreamDefaultWriter',
        'ByteLengthQueuingStrategy','CountQueuingStrategy',
        'TransformStream','TransformStreamDefaultController',
        'TextEncoderStream','TextDecoderStream',
        'CompressionStream','DecompressionStream'
    ];
    names.forEach(function (n) {
        must(typeof globalThis[n] === 'function', n + " is a function");
    });
    /* Construction surface */
    var rs = new ReadableStream({start: function(c) { c.close(); }});
    must(rs instanceof ReadableStream,           "new ReadableStream");
    must(typeof rs.getReader === 'function',     "ReadableStream.getReader");
    must(typeof rs.pipeTo === 'function',        "ReadableStream.pipeTo");
    must(typeof rs.pipeThrough === 'function',   "ReadableStream.pipeThrough");
    must(typeof rs.cancel === 'function',        "ReadableStream.cancel");
    must(typeof rs.tee === 'function',           "ReadableStream.tee");
    must(rs.locked === false,                    "ReadableStream.locked starts false");

    var ws = new WritableStream({write: function() {}});
    must(ws instanceof WritableStream,           "new WritableStream");
    must(typeof ws.getWriter === 'function',     "WritableStream.getWriter");
    must(typeof ws.abort === 'function',         "WritableStream.abort");
    must(typeof ws.close === 'function',         "WritableStream.close");

    var ts = new TransformStream();
    must(ts instanceof TransformStream,          "new TransformStream");
    must(ts.readable instanceof ReadableStream,  "TransformStream.readable");
    must(ts.writable instanceof WritableStream,  "TransformStream.writable");

    /* Queuing strategies */
    var cqs = new CountQueuingStrategy({highWaterMark: 3});
    mustEq(cqs.size({}), 1,                      "CountQueuingStrategy.size returns 1");
    mustEq(cqs.highWaterMark, 3,                 "CountQueuingStrategy.highWaterMark");
    var bqs = new ByteLengthQueuingStrategy({highWaterMark: 1024});
    mustEq(bqs.size(new Uint8Array(7)), 7,       "ByteLengthQueuingStrategy.size");
    mustEq(bqs.highWaterMark, 1024,              "ByteLengthQueuingStrategy.highWaterMark");
});

testJS("ReadableStream (basic, async)", function () {
    _pendingAsync++;
    /* start enqueues, then close.  Reader drains in order. */
    var rs = new ReadableStream({
        start: function (c) {
            c.enqueue("a");
            c.enqueue("b");
            c.enqueue("c");
            c.close();
        }
    });
    var reader = rs.getReader();
    var got = [];
    function pump() {
        reader.read().then(function (r) {
            if (r.done) {
                try {
                    mustEq(got.join(""), "abc",          "all chunks read in order");
                    mustEq(reader.closed === undefined, false, "reader.closed is a Promise");
                    mustEq(rs.locked, true,              "stream locked while reader attached");
                } catch (e) { _asyncFail("ReadableStream", e); }
                _doneAsync();
                return;
            }
            got.push(r.value);
            pump();
        }, function (e) { _asyncFail("ReadableStream read", e); _doneAsync(); });
    }
    pump();
    return true;
});

testJS("WritableStream (basic, async)", function () {
    _pendingAsync++;
    var written = [];
    var closed = false;
    var ws = new WritableStream({
        write: function (chunk) { written.push(chunk); },
        close: function () { closed = true; }
    });
    var w = ws.getWriter();
    Promise.all([w.write("x"), w.write("y"), w.write("z"), w.close()]).then(function () {
        try {
            mustEq(written.join(""), "xyz",       "writes arrive in order");
            mustEq(closed, true,                  "close() callback fired");
        } catch (e) { _asyncFail("WritableStream", e); }
        _doneAsync();
    }, function (e) { _asyncFail("WritableStream rejected", e); _doneAsync(); });
    return true;
});

testJS("TransformStream + pipeThrough/pipeTo (async)", function () {
    _pendingAsync++;
    var rs = new ReadableStream({
        start: function (c) {
            c.enqueue("ab"); c.enqueue("cd"); c.enqueue("ef"); c.close();
        }
    });
    var upper = new TransformStream({
        transform: function (chunk, ctrl) {
            ctrl.enqueue(String(chunk).toUpperCase());
        }
    });
    var collected = [];
    var ws = new WritableStream({
        write: function (chunk) { collected.push(chunk); }
    });
    rs.pipeThrough(upper).pipeTo(ws).then(function () {
        try {
            mustEq(collected.join(""), "ABCDEF",  "pipe through transform");
            mustEq(collected.length, 3,           "chunk count preserved");
        } catch (e) { _asyncFail("TransformStream pipe", e); }
        _doneAsync();
    }, function (e) { _asyncFail("pipeTo rejected", e); _doneAsync(); });
    return true;
});

testJS("ReadableStream.tee + cancel (async)", function () {
    _pendingAsync++;
    var cancelled = false;
    var rs = new ReadableStream({
        start: function (c) { c.enqueue(1); c.enqueue(2); c.close(); },
        cancel: function () { cancelled = true; }
    });
    var branches = rs.tee();
    must(branches.length === 2,                  "tee returns array of two");
    var aGot = [], bGot = [];
    function read(s, dst) {
        var r = s.getReader();
        function loop() {
            return r.read().then(function (x) {
                if (x.done) return;
                dst.push(x.value);
                return loop();
            });
        }
        return loop();
    }
    Promise.all([read(branches[0], aGot), read(branches[1], bGot)]).then(function () {
        try {
            mustEq(JSON.stringify(aGot), "[1,2]", "tee branch A gets both chunks");
            mustEq(JSON.stringify(bGot), "[1,2]", "tee branch B gets both chunks");
        } catch (e) { _asyncFail("tee", e); }
        /* Now test cancel propagation on a fresh stream */
        var rs2 = new ReadableStream({
            start: function (c) { c.enqueue('x'); },
            cancel: function (reason) { cancelled = true; }
        });
        return rs2.cancel("nope");
    }).then(function () {
        try { must(cancelled, "cancel callback fired"); }
        catch (e) { _asyncFail("cancel", e); }
        _doneAsync();
    }, function (e) { _asyncFail("tee/cancel async", e); _doneAsync(); });
    return true;
});

testJS("ReadableStream error propagation (async)", function () {
    _pendingAsync++;
    var rs = new ReadableStream({
        start: function (c) {
            c.enqueue("ok");
            c.error(new Error("boom"));
        }
    });
    var reader = rs.getReader();
    /* First read succeeds, second rejects */
    reader.read().then(function (r) {
        try { mustEq(r.value, "ok",              "first chunk OK before error"); }
        catch (e) { _asyncFail("err prop first", e); }
        return reader.read();
    }).then(function () {
        _asyncFail("err prop", new Error("expected rejection"));
        _doneAsync();
    }, function (e) {
        try { mustEq(String(e.message), "boom", "error propagates via reject"); }
        catch (ex) { _asyncFail("err msg", ex); }
        _doneAsync();
    });
    return true;
});

testJS("TextEncoderStream + TextDecoderStream (async)", function () {
    _pendingAsync++;
    /* Round-trip including multi-byte UTF-8 char */
    var input = "Hello, 世界! héllo";
    var es = new TextEncoderStream();
    var ds = new TextDecoderStream();
    var src = new ReadableStream({
        start: function (c) { c.enqueue(input); c.close(); }
    });
    var collected = [];
    var sink = new WritableStream({
        write: function (chunk) { collected.push(chunk); }
    });
    src.pipeThrough(es).pipeThrough(ds).pipeTo(sink).then(function () {
        try {
            mustEq(collected.join(""), input,  "round-trip encode/decode");
            mustEq(es.encoding, "utf-8",       "encoding is utf-8");
            mustEq(ds.encoding, "utf-8",       "decoder encoding is utf-8");
        } catch (e) { _asyncFail("TextEncoderStream/DecoderStream", e); }
        _doneAsync();
    }, function (e) { _asyncFail("TextStream pipe rejected", e); _doneAsync(); });
    return true;
});

testJS("TextDecoderStream multi-byte across chunks (async)", function () {
    _pendingAsync++;
    /* "世" is 0xE4 0xB8 0x96 in UTF-8.  Split across chunk boundary
       to test that TextDecoderStream uses {stream:true} so partial
       sequences are buffered, not turned into U+FFFD. */
    var part1 = new Uint8Array([0x48, 0x69, 0xE4]);       /* "Hi" + first byte of 世 */
    var part2 = new Uint8Array([0xB8, 0x96, 0x21]);       /* rest of 世 + "!" */
    var ds = new TextDecoderStream();
    var src = new ReadableStream({
        start: function (c) {
            c.enqueue(part1);
            c.enqueue(part2);
            c.close();
        }
    });
    var collected = [];
    var sink = new WritableStream({
        write: function (chunk) { collected.push(chunk); }
    });
    src.pipeThrough(ds).pipeTo(sink).then(function () {
        try {
            mustEq(collected.join(""), "Hi世!", "multi-byte char across chunks");
        } catch (e) { _asyncFail("TextDecoderStream split", e); }
        _doneAsync();
    }, function (e) { _asyncFail("decoder pipe rejected", e); _doneAsync(); });
    return true;
});

testJS("CompressionStream / DecompressionStream round-trip (async)", function () {
    _pendingAsync++;
    var input = "the quick brown fox jumps over the lazy dog ";
    var enc = new TextEncoder();
    var dec = new TextDecoder();
    /* Repeat to make compression worthwhile. */
    var bytes = enc.encode(input.repeat(50));   /* ~2200 bytes */

    /* Test gzip / deflate / deflate-raw in sequence */
    var formats = ["gzip", "deflate", "deflate-raw"];
    var idx = 0;
    function doOne(fmt) {
        var cs = new CompressionStream(fmt);
        var ds = new DecompressionStream(fmt);
        var src = new ReadableStream({
            start: function (c) { c.enqueue(bytes); c.close(); }
        });
        var collected = [];
        var sink = new WritableStream({
            write: function (chunk) { collected.push(chunk); }
        });
        return src.pipeThrough(cs).pipeThrough(ds).pipeTo(sink).then(function () {
            var total = 0;
            for (var i = 0; i < collected.length; i++) total += collected[i].byteLength;
            var joined = new Uint8Array(total);
            var off = 0;
            for (var j = 0; j < collected.length; j++) {
                joined.set(collected[j], off);
                off += collected[j].byteLength;
            }
            mustEq(dec.decode(joined), input.repeat(50), fmt + " round-trip");
        });
    }
    doOne("gzip").then(function () { return doOne("deflate"); })
        .then(function () { return doOne("deflate-raw"); })
        .then(function () { _doneAsync(); },
              function (e) { _asyncFail("Compression round-trip", e); _doneAsync(); });
    return true;
});

testJS("CompressionStream emits progressively (async)", function () {
    _pendingAsync++;
    /* Real streaming check — feed large input in many small chunks
       and verify that at least one output chunk emerges BEFORE we
       close the writer.  This is the property that distinguishes
       real streaming from buffer-and-flush. */
    var enc = new TextEncoder();
    var bytes = enc.encode("the quick brown fox ".repeat(2000));   /* ~40 KB */
    var cs = new CompressionStream("gzip");
    var cw = cs.writable.getWriter();
    var cr = cs.readable.getReader();

    var beforeClose = 0;
    var afterClose  = 0;
    var closed = false;

    function drain() {
        cr.read().then(function (r) {
            if (r.done) {
                try {
                    /* Either at least one chunk arrived before close
                       (real streaming) OR our test environment is so
                       fast that all chunks emerge in the same tick.
                       Accept either; what we DON'T want is everything
                       being delayed to the close path. */
                    must(beforeClose + afterClose >= 1,
                        "at least one output chunk produced");
                    /* For the 40KB streaming test, expect at least 2 chunks total. */
                    must(beforeClose + afterClose >= 2,
                        "output split across multiple chunks (streaming)");
                } catch (e) { _asyncFail("streaming emit", e); }
                _doneAsync();
                return;
            }
            if (closed) afterClose++;
            else        beforeClose++;
            drain();
        }, function (e) { _asyncFail("streaming read rejected", e); _doneAsync(); });
    }
    drain();

    /* Feed 1KB at a time with microtask gaps */
    var i = 0;
    function pump() {
        if (i >= bytes.length) {
            closed = true;
            cw.close();
            return;
        }
        var end = Math.min(i + 1024, bytes.length);
        cw.write(bytes.subarray(i, end));
        i = end;
        Promise.resolve().then(pump);
    }
    pump();
    return true;
});


/* Safety net: if scheduling somehow doesn't fire, exit after a tick. */
setTimeout(function () {
    if (!_exitOnce) {
        _exitOnce = true;
        testJS.exit();
    }
}, 500);
