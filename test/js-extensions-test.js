/* Basic-functionality test for rampart's standard-JavaScript extensions
   (catalog: src/duktape/standard-js-extensions.md).
   Map and Set are covered by map-test.js; everything else lives here.

   Runs under both rampart and node so we can cross-validate that
   rampart's installs behave like the standards they target:

       rampart  js-extensions-test.js
       node     js-extensions-test.js

   One line per category.  Subtests only print on failure (via the
   test-feature harness's exception path). */

var testJS = new (require('./test-feature.js'))({
    prefix:    "js-ext",
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
        /* Also emit a recognisable FAILED line so run_tests.sh's
         * `grep '>>>>> FAILED <<<<<'` counts the async failure.
         * Mirror test-feature.js's right-alignment so the FAILED text
         * lines up with sync passed/FAILED rows in the same column. */
        var FAILED_TEXT = ">>>>> FAILED <<<<<";
        var tfState     = global._tfState || {};
        var prefix      = tfState.label || "js-ext";
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


testJS("JSON.parse", function() {
    /* Standard parse */
    mustEq(JSON.parse('{"a":1}').a, 1, "basic object");
    mustEq(JSON.parse('[1,2,3]')[1], 2, "array");
    mustEq(JSON.parse('null'), null, "null literal");
    mustEq(JSON.parse('"x"'), 'x', "string literal");
    mustEq(JSON.parse('42'), 42, "number");
    mustEq(JSON.parse('true'), true, "boolean");
    /* Reviver function path still works (standard) */
    var rev = JSON.parse('{"a":2}', function(k, v) {
        return (typeof v === 'number') ? v * 10 : v;
    });
    mustEq(rev.a, 20, "reviver function");

    /* Rampart extensions: Buffer input + cyclic-ref restore */
    if (_isRampart) {
        var b = Buffer.from('{"a":2,"b":"x"}');
        mustEq(JSON.parse(b).a, 2, "Buffer input");

        var s1 = '{"a":1,"self":{"_cyclic_ref":"$"}}';
        var r1 = JSON.parse(s1, true);
        must(r1.self === r1, "cyclic restore: self-ref to root");

        var s2 = '{"items":[10,20,30],"alias":{"_cyclic_ref":"$.items"}}';
        var r2 = JSON.parse(s2, true);
        must(r2.alias === r2.items, "cyclic restore: alias to sibling");
        mustEq(r2.alias[1], 20, "alias resolved deeply");

        /* Round-trip via sprintf %!J */
        var orig = {a: 1};
        orig.self = orig;
        var enc = rampart.utils.sprintf("%!J", orig);
        var dec = JSON.parse(enc, true);
        must(dec.self === dec, "round-trip sprintf %!J");

        /* `JSON._parse_orig` retained on JSON object */
        must(typeof JSON._parse_orig === 'function', "_parse_orig present");
    }
});


testJS("Object methods", function() {
    /* values */
    mustEq(Object.values({a: 1, b: 2, c: 3}).join(','), '1,2,3', "values object");
    mustEq(Object.values([]).length, 0, "values empty array");
    mustEq(Object.values('abc').join(','), 'a,b,c', "values string -> chars");

    /* hasOwn */
    must(Object.hasOwn({a: 1}, 'a'), "hasOwn own prop");
    must(!Object.hasOwn({a: 1}, 'b'), "hasOwn missing");
    must(!Object.hasOwn({}, 'toString'), "hasOwn excludes inherited");
    must(Object.hasOwn(Object.create(null, {x: {value: 1}}), 'x'), "hasOwn on null-proto");

    /* fromEntries */
    var o = Object.fromEntries([['a', 1], ['b', 2]]);
    mustEq(o.a, 1, "fromEntries a");
    mustEq(o.b, 2, "fromEntries b");
    mustEq(Object.keys(Object.fromEntries([])).length, 0, "fromEntries empty");
});


testJS("Array.prototype", function() {
    /* find / findIndex */
    mustEq([1, 2, 3, 4].find(function(x) { return x > 2; }), 3, "find first match");
    mustEq([1, 2, 3].find(function(x) { return x > 99; }), undefined, "find no match");
    mustEq([1, 2, 3, 4].findIndex(function(x) { return x > 2; }), 2, "findIndex first");
    mustEq([1, 2, 3].findIndex(function(x) { return x > 99; }), -1, "findIndex none");

    /* includes (with NaN handling) */
    must([1, 2, 3].includes(2), "includes positive");
    must(![1, 2, 3].includes(99), "includes negative");
    must([NaN].includes(NaN), "includes NaN (SameValueZero)");
    must(!['1'].includes(1), "includes strict, not loose");

    /* flat */
    mustEq([1, [2, [3]]].flat().join(','), '1,2,3', "flat depth=1 (default)");
    mustEq([1, [2, [3]]].flat(2).join(','), '1,2,3', "flat depth=2");
    mustEq([1, [2, [3, [4]]]].flat(Infinity).join(','), '1,2,3,4', "flat Infinity");
    mustEq([1, [2]].flat(0).join(','), '1,2', "flat depth=0 returns copy");

    /* flatMap */
    mustEq([1, 2, 3].flatMap(function(x) { return [x, x * 2]; }).join(','),
           '1,2,2,4,3,6', "flatMap pair-out");
    mustEq([1, 2, 3].flatMap(function(x) { return x * 2; }).join(','),
           '2,4,6', "flatMap non-array out");

    /* at */
    mustEq([1, 2, 3].at(0), 1, "at(0)");
    mustEq([1, 2, 3].at(-1), 3, "at(-1)");
    mustEq([1, 2, 3].at(-3), 1, "at(-len)");
    mustEq([1, 2, 3].at(99), undefined, "at(oob)");

    /* findLast / findLastIndex */
    mustEq([1, 2, 3, 4].findLast(function(x) { return x < 4; }), 3, "findLast");
    mustEq([1, 2, 3, 4].findLastIndex(function(x) { return x < 3; }), 1, "findLastIndex");
    mustEq([].findLast(function() { return true; }), undefined, "findLast empty");
    mustEq([].findLastIndex(function() { return true; }), -1, "findLastIndex empty");
});


testJS("Array iteration (values/keys/entries/[Symbol.iterator])", function() {
    /* Per spec: Array.prototype[Symbol.iterator] === Array.prototype.values */
    must(Array.prototype.values === Array.prototype[Symbol.iterator],
         "values is the same function as [Symbol.iterator]");

    var a = [10, 20, 30];

    /* keys */
    var ks = Array.from(a.keys());
    mustEq(ks.join(','), '0,1,2', "keys");

    /* values */
    var vs = Array.from(a.values());
    mustEq(vs.join(','), '10,20,30', "values");

    /* entries */
    var es = [];
    var it = a.entries(), step;
    while (!(step = it.next()).done) es.push(step.value[0] + ':' + step.value[1]);
    mustEq(es.join(','), '0:10,1:20,2:30', "entries");

    /* iterator is self-iterable (so for-of can re-call [Symbol.iterator]) */
    var v = a.values();
    must(v[Symbol.iterator]() === v, "iterator returns itself from [Symbol.iterator]");

    /* Iterator protocol: {value, done} shape, terminal step done=true */
    var it2 = [7].values();
    var s1 = it2.next();
    var s2 = it2.next();
    mustEq(s1.value, 7,    "step 1 value");
    must(s1.done === false, "step 1 done=false");
    must(s2.value === undefined, "step 2 value=undefined");
    must(s2.done === true,  "step 2 done=true");

    /* new Set(arr) — Set's constructor consumes via Symbol.iterator
       when present (works in both runtimes; under rampart, vanilla
       duktape can't parse `for-of`/spread so the iterable path is
       exercised here through Set's construction, not via syntax). */
    var s = new Set([1, 1, 2, 3, 3]);
    mustEq(s.size, 3, "new Set(arr) dedupes");

    /* Non-enumerable installs: for-in only iterates index keys */
    var k2 = [];
    var arr = [11, 22, 33];
    for (var k in arr) k2.push(k);
    mustEq(k2.join(','), '0,1,2', "for-in: iter methods are non-enum");
    mustEq(Object.keys(arr).join(','), '0,1,2', "Object.keys: clean");

    /* JSON.stringify unaffected */
    mustEq(JSON.stringify([1, 2, 3]), '[1,2,3]', "JSON.stringify unaffected");
});


testJS("String iteration (Symbol.iterator)", function() {
    must(typeof String.prototype[Symbol.iterator] === "function",
        "String.prototype[Symbol.iterator] is callable");

    /* BMP-only: yields one code unit per step */
    var it = "héllo"[Symbol.iterator]();
    var parts = [];
    var step;
    while (!(step = it.next()).done) parts.push(step.value);
    mustEq(parts.join(","), "h,é,l,l,o",            "BMP per-codepoint");
    mustEq(parts.length, 5,                          "BMP step count");

    /* Iterator self-iterable (so it can be re-handed to for-of) */
    var it2 = "ab"[Symbol.iterator]();
    must(it2[Symbol.iterator]() === it2,
        "iterator is self-iterable");

    /* Supplementary-plane (emoji): surrogate pair combined into ONE step */
    var it3 = "a👍b"[Symbol.iterator]();
    var parts3 = [];
    while (!(step = it3.next()).done) parts3.push(step.value);
    mustEq(parts3.length, 3,                         "emoji 3 code-points");
    mustEq(parts3[0], "a",                           "emoji step 0");
    mustEq(parts3[1].length, 2,
        "emoji step 1 keeps surrogate pair (length 2 code units)");
    mustEq(parts3[2], "b",                           "emoji step 2");

    /* Unpaired high surrogate (lone high w/o following low) — emits as
       its own 1-code-unit step.  Doesn't crash. */
    var lone = "\uD83D" + "x";
    var partsLone = [];
    var itL = lone[Symbol.iterator]();
    while (!(step = itL.next()).done) partsLone.push(step.value);
    mustEq(partsLone.length, 2,                     "unpaired surrogate emits separately");

    /* Empty string */
    var itE = ""[Symbol.iterator]();
    mustEq(itE.next().done, true,                    "empty string: done immediately");
});


testJS("Array statics", function() {
    /* Array.from on various input shapes */
    mustEq(Array.from('abc').join(','), 'a,b,c', "from string");
    mustEq(Array.from([1, 2, 3]).join(','), '1,2,3', "from array");
    mustEq(Array.from([1, 2, 3], function(x) { return x * 10; }).join(','),
           '10,20,30', "from array + mapFn");
    mustEq(Array.from('abc', function(c) { return c.toUpperCase(); }).join(''),
           'ABC', "from string + mapFn");
    /* String iteration is per code-point: emoji combines surrogate pair */
    mustEq(Array.from('a👍b').length, 3,
           "from string: emoji is one code-point step (not two surrogate halves)");
    mustEq(Array.from('a👍b')[1].length, 2,
           "from string: emoji step preserves surrogate-pair (2 code units)");
    mustEq(Array.from({length: 3, 0: 'x', 1: 'y', 2: 'z'}).join(','),
           'x,y,z', "from array-like");
    mustEq(Array.from(new Set([1, 2, 3])).join(','), '1,2,3', "from Set (iterable)");
    mustEq(Array.from(new Map([['a', 1], ['b', 2]])).length, 2, "from Map (iterable)");
    mustEq(Array.from({length: 0}).length, 0, "from empty array-like");

    /* Array.of */
    mustEq(Array.of(1, 2, 3).length, 3, "of length");
    mustEq(Array.of(7).length, 1, "of single arg (vs new Array(7))");
    mustEq(Array.of(7)[0], 7, "of single arg value");
    mustEq(Array.of().length, 0, "of zero args");
});


testJS("String.prototype", function() {
    /* trimStart / trimEnd */
    mustEq('  hi  '.trimStart(), 'hi  ', "trimStart spaces");
    mustEq('  hi  '.trimEnd(),   '  hi', "trimEnd spaces");
    mustEq('\t\nhi'.trimStart(), 'hi', "trimStart tab+nl");
    mustEq('hi\r\n'.trimEnd(),   'hi', "trimEnd CRLF");
    mustEq('   '.trimStart(),    '',   "trimStart all-ws");

    /* replaceAll */
    mustEq('a-b-c'.replaceAll('-', '_'),   'a_b_c', "replaceAll basic");
    mustEq('abc'.replaceAll('x', 'y'),     'abc',   "replaceAll no match");
    mustEq('aaa'.replaceAll('a', 'bc'),    'bcbcbc', "replaceAll grow");
    mustEq('aaa'.replaceAll('', '-'),      '-a-a-a-', "replaceAll empty needle");

    /* matchAll (polyfill installed in register.c) */
    var m = 'foo1bar22baz333'.matchAll(/(\w)(\d+)/g);
    var captures = [];
    var v;
    while (!(v = m.next()).done) captures.push(v.value[2]);
    mustEq(captures.join(','), '1,22,333', "matchAll group capture");

    /* Iterator must throw on non-global regex (spec) */
    mustThrow(function() { 'abc'.matchAll(/x/); }, "matchAll non-global -> TypeError");

    /* String argument auto-globalizes */
    var sm = 'aaa'.matchAll('a');
    var n = 0;
    while (!sm.next().done) n++;
    mustEq(n, 3, "matchAll string arg auto-global");

    /* Iterator itself is iterable */
    if (typeof Symbol !== 'undefined' && Symbol.iterator) {
        var it = 'aaa'.matchAll(/a/g);
        must(typeof it[Symbol.iterator] === 'function', "matchAll iterator has [Symbol.iterator]");
    }
});


testJS("Object.groupBy", function() {
    /* Basic numeric grouping */
    var r = Object.groupBy([1.1, 2.5, 1.7, 3.2, 2.8], function(n) {
        return Math.floor(n);
    });
    mustEq(r[1].join(','), '1.1,1.7', "bucket 1");
    mustEq(r[2].join(','), '2.5,2.8', "bucket 2");
    mustEq(r[3].join(','), '3.2', "bucket 3");

    /* Null-prototype result (spec) */
    must(Object.getPrototypeOf(r) === null, "result has null prototype");

    /* Sets and other iterables work via Symbol.iterator path */
    var s = new Set([1, 2, 3, 4, 5, 6]);
    var g = Object.groupBy(s, function(n) { return n % 2 ? 'odd' : 'even'; });
    mustEq(g.odd.join(','),  '1,3,5', "Set iterable: odd bucket");
    mustEq(g.even.join(','), '2,4,6', "Set iterable: even bucket");

    /* Array-like fallback (no Symbol.iterator) — rampart-only.
       The spec requires items to be iterable; node throws on a plain
       array-like.  Rampart's polyfill is more permissive because plain
       Arrays in duktape don't even carry Symbol.iterator, so a strict
       check would block the common case. */
    if (_isRampart) {
        var likeArr = {length: 3, 0: 'a', 1: 'b', 2: 'a'};
        var gl = Object.groupBy(likeArr, function(v) { return v; });
        mustEq(gl.a.length, 2, "array-like: a count");
        mustEq(gl.b.length, 1, "array-like: b count");
    }

    /* keyFn receives index as second arg */
    var seen = [];
    Object.groupBy(['x', 'y', 'z'], function(v, i) { seen.push(i); return v; });
    mustEq(seen.join(','), '0,1,2', "keyFn receives index");

    /* Type errors */
    mustThrow(function() { Object.groupBy(null, function() {}); },        "null items -> TypeError");
    mustThrow(function() { Object.groupBy(undefined, function() {}); },   "undefined items -> TypeError");
    mustThrow(function() { Object.groupBy([1], null); },                  "non-callable keyFn -> TypeError");
});


testJS("globals + eval + Function", function() {
    must(typeof globalThis === 'object', "globalThis is object");
    must(typeof global === 'object',     "global is object");
    must(globalThis === global,          "globalThis === global");

    /* eval still evaluates a simple expression */
    mustEq(eval('1 + 1'),   2,       "eval arithmetic");
    mustEq(eval('"abc"'),   'abc',   "eval string literal");
    mustEq(eval('[1,2][1]'), 2,      "eval array index");

    /* new Function builds callable functions */
    var f = new Function('a', 'b', 'return a + b;');
    mustEq(f(2, 3), 5, "new Function 2-arg + body");

    var g = new Function('return 42;');
    mustEq(g(), 42, "new Function body only");

    /* Function.length / typeof */
    must(typeof f === 'function', "new Function returns function");
    mustEq(f.length, 2, "new Function arity");
});


testJS("Proxy.revocable", function() {
    if (typeof Proxy === 'undefined') { must(false, "Proxy not present"); return; }

    var target = {x: 1};
    var rv = Proxy.revocable(target, {
        get: function(t, k) { return (k in t) ? t[k] : 'missing'; },
        set: function(t, k, v) { t[k] = v; return true; }
    });

    must(typeof rv.proxy === 'object', "returns {proxy, revoke}");
    must(typeof rv.revoke === 'function', "revoke is callable");

    mustEq(rv.proxy.x, 1,        "proxy get existing");
    mustEq(rv.proxy.y, 'missing', "proxy get via trap");

    rv.proxy.z = 9;
    mustEq(target.z, 9, "proxy set delegates");

    rv.revoke();
    mustThrow(function() { return rv.proxy.x; },     "post-revoke get throws");
    mustThrow(function() { rv.proxy.x = 1; },        "post-revoke set throws");

    /* Revoke is idempotent (no throw on second call) */
    rv.revoke();
    must(true, "double-revoke is no-op");

    /* Argument validation */
    mustThrow(function() { Proxy.revocable(null, {}); },   "null target throws");
    mustThrow(function() { Proxy.revocable({}, null); },   "null handler throws");
});


testJS("Intl", function() {
    must(typeof Intl !== 'undefined', "Intl is defined");

    var ctors = ['DateTimeFormat', 'NumberFormat', 'Collator', 'PluralRules',
                 'RelativeTimeFormat', 'ListFormat', 'DisplayNames', 'Locale'];
    for (var i = 0; i < ctors.length; i++)
        must(typeof Intl[ctors[i]] === 'function', "Intl." + ctors[i] + " is constructor");

    /* NumberFormat */
    mustEq(new Intl.NumberFormat('en-US').format(1234), '1,234',
           "NumberFormat en-US thousand-sep");

    /* DateTimeFormat: just check we get a non-empty string */
    var df = new Intl.DateTimeFormat('en-US',
                {year: 'numeric', month: 'short', day: 'numeric'});
    var s = df.format(new Date(2026, 0, 15));
    must(typeof s === 'string' && s.length > 0, "DateTimeFormat formats");

    /* PluralRules */
    var pr = new Intl.PluralRules('en-US');
    mustEq(pr.select(1), 'one',   "PluralRules en-US 1 -> one");
    mustEq(pr.select(2), 'other', "PluralRules en-US 2 -> other");

    /* Collator */
    must(new Intl.Collator('en').compare('a', 'b') < 0, "Collator a<b");

    /* getCanonicalLocales */
    if (typeof Intl.getCanonicalLocales === 'function') {
        var canon = Intl.getCanonicalLocales(['EN-us', 'fr']);
        mustEq(canon.length, 2, "getCanonicalLocales length");
        mustEq(canon[0].toLowerCase(), 'en-us', "getCanonicalLocales[0]");
    }
});


testJS("Promise (surface + sync)", function () {
    /* Rampart installs Promise eagerly at context init
       (src/duktape/globals/rampart-promise.c) so vanilla rampart
       has it without -t / -b.  This block tests surface + sync
       construction; async semantics (chaining, .all/.race/etc.,
       error propagation, microtask order) live in the async
       block right below. */
    must(typeof Promise === 'function',                    "Promise constructor");
    must(typeof Promise.resolve === 'function',            "Promise.resolve");
    must(typeof Promise.reject === 'function',             "Promise.reject");
    must(typeof Promise.all === 'function',                "Promise.all");
    must(typeof Promise.race === 'function',               "Promise.race");
    must(typeof Promise.allSettled === 'function',         "Promise.allSettled (ES2020)");
    must(typeof Promise.any === 'function',                "Promise.any (ES2021)");
    must(typeof Promise.prototype.then === 'function',     "Promise.prototype.then");
    must(typeof Promise.prototype['catch'] === 'function', "Promise.prototype.catch");
    must(typeof Promise.prototype['finally'] === 'function', "Promise.prototype.finally (ES2018)");

    /* Constructor — executor invoked synchronously */
    var execRan = false;
    var p = new Promise(function (resolve) { execRan = true; resolve("ok"); });
    must(execRan, "executor called synchronously");
    must(p && typeof p.then === 'function', "new Promise returns thenable");
    must(p instanceof Promise, "constructed instanceof Promise");

    /* `new Promise(non-function)` must throw TypeError */
    mustThrow(function () { new Promise(); },     "Promise() requires an executor");
    mustThrow(function () { new Promise(null); }, "Promise(null) throws");

    /* Promise() without `new` must throw TypeError per spec */
    mustThrow(function () { Promise(function(){}); }, "Promise without new throws");

    /* Promise.resolve / reject return Promise instances */
    must(Promise.resolve(1) instanceof Promise, "Promise.resolve(x) is a Promise");
    must(Promise.reject(1).catch(function(){}) instanceof Promise, "Promise.reject(x).catch is a Promise");

    /* Promise.resolve(thenable) adopts its state — checked async */

    /* then/catch/finally return new Promises (not the same) */
    var p1 = Promise.resolve(1);
    var p2 = p1.then(function(x) { return x + 1; });
    must(p2 instanceof Promise, "then returns a Promise");
    must(p2 !== p1,             "then returns a NEW promise");
});

testJS("Promise (async semantics)", function () {
    _pendingAsync++;
    var enc = (typeof TextEncoder === 'function') ? new TextEncoder() : null;

    Promise.resolve()
    /* === resolve / value-pass / then chain === */
    .then(function () {
        return Promise.resolve(42).then(function (v) {
            mustEq(v, 42, "Promise.resolve(42) → 42");
            return v * 2;
        }).then(function (v) {
            mustEq(v, 84, "then-chain pass-through");
            /* Returning a Promise from then() flattens */
            return Promise.resolve(v + 1);
        }).then(function (v) {
            mustEq(v, 85, "returning Promise from then flattens");
            /* Returning a thenable also flattens */
            return {then: function (res) { res('thenable'); }};
        }).then(function (v) {
            mustEq(v, 'thenable', "returning a thenable flattens");
        });
    })
    /* === reject + catch === */
    .then(function () {
        return Promise.reject(new Error('boom')).catch(function (e) {
            mustEq(e.message, 'boom', "Promise.reject → catch");
            return 'recovered';
        }).then(function (v) {
            mustEq(v, 'recovered', "catch returns a value → next .then sees it");
        });
    })
    /* === throw in then propagates to catch === */
    .then(function () {
        return Promise.resolve(1).then(function () {
            throw new Error('from-then');
        }).then(function () {
            throw new Error("should have skipped this then");
        }, null).catch(function (e) {
            mustEq(e.message, 'from-then', "throw in then → propagates to catch");
        });
    })
    /* === finally — runs on both paths, doesn't change value === */
    .then(function () {
        var finallyRan = 0;
        return Promise.resolve('keep').finally(function () { finallyRan++; })
            .then(function (v) {
                mustEq(v, 'keep', "finally preserves resolve value");
                mustEq(finallyRan, 1, "finally fired once after resolve");
                return Promise.reject(new Error('bad')).finally(function () { finallyRan++; });
            }).catch(function (e) {
                mustEq(e.message, 'bad', "finally preserves reject value");
                mustEq(finallyRan, 2, "finally fired once after reject");
            });
    })
    /* === Promise.all — resolves array; rejects on first failure === */
    .then(function () {
        return Promise.all([Promise.resolve(1), 2, Promise.resolve(3)]).then(function (vs) {
            mustEq(JSON.stringify(vs), '[1,2,3]', "Promise.all resolves array (non-promises passed through)");
            return Promise.all([Promise.resolve(1), Promise.reject(new Error('x')), Promise.resolve(3)])
                .then(function () { throw new Error("Promise.all should reject"); },
                      function (e) { mustEq(e.message, 'x', "Promise.all rejects on first failure"); });
        });
    })
    /* === Promise.all preserves order === */
    .then(function () {
        var slow = new Promise(function (r) { setTimeout(function () { r('slow'); }, 30); });
        var fast = Promise.resolve('fast');
        return Promise.all([slow, fast]).then(function (vs) {
            mustEq(JSON.stringify(vs), '["slow","fast"]', "Promise.all preserves input order, not resolution order");
        });
    })
    /* === Promise.all([]) → [] === */
    .then(function () {
        return Promise.all([]).then(function (vs) {
            mustEq(JSON.stringify(vs), '[]', "Promise.all([]) resolves to []");
        });
    })
    /* === Promise.race — resolves with first to settle === */
    .then(function () {
        var slow = new Promise(function (r) { setTimeout(function () { r('slow'); }, 50); });
        var fast = new Promise(function (r) { setTimeout(function () { r('fast'); }, 10); });
        return Promise.race([slow, fast]).then(function (v) {
            mustEq(v, 'fast', "Promise.race takes the first to resolve");
        });
    })
    /* === Promise.race — first reject also wins === */
    .then(function () {
        var slow = new Promise(function (r) { setTimeout(function () { r('slow'); }, 50); });
        var bad  = new Promise(function (_, j) { setTimeout(function () { j('failed-fast'); }, 10); });
        return Promise.race([slow, bad]).then(function () {
            throw new Error("Promise.race should reject");
        }, function (e) { mustEq(e, 'failed-fast', "Promise.race first reject wins"); });
    })
    /* === Promise.allSettled — never rejects, returns status array === */
    .then(function () {
        return Promise.allSettled([
            Promise.resolve('ok'),
            Promise.reject(new Error('bad')),
            Promise.resolve(123)
        ]).then(function (rs) {
            mustEq(rs.length, 3,                   "allSettled returns 3 entries");
            mustEq(rs[0].status, 'fulfilled',      "[0].status");
            mustEq(rs[0].value,  'ok',             "[0].value");
            mustEq(rs[1].status, 'rejected',       "[1].status");
            mustEq(rs[1].reason.message, 'bad',    "[1].reason");
            mustEq(rs[2].status, 'fulfilled',      "[2].status");
            mustEq(rs[2].value,  123,              "[2].value");
        });
    })
    /* === Promise.any — first resolved wins; rejects all → AggregateError === */
    .then(function () {
        return Promise.any([Promise.reject(1), Promise.resolve('won'), Promise.reject(2)]).then(function (v) {
            mustEq(v, 'won', "Promise.any takes first fulfilled");
            return Promise.any([Promise.reject('a'), Promise.reject('b')])
                .then(function () { throw new Error("any should reject when all reject"); },
                      function (e) {
                          /* e is an AggregateError with .errors = ['a','b'] */
                          must(Array.isArray(e.errors), "any reject reason has .errors array");
                          mustEq(JSON.stringify(e.errors), '["a","b"]', "any.errors content");
                      });
        });
    })
    /* === Promise.resolve(thenable) adopts state === */
    .then(function () {
        var thenable = {then: function (res) { setTimeout(function () { res('adopted'); }, 10); }};
        return Promise.resolve(thenable).then(function (v) {
            mustEq(v, 'adopted', "Promise.resolve adopts thenable state");
        });
    })
    /* === Resolving with itself → infinite-loop guard (rejects) === */
    .then(function () {
        /* `p = Promise(res); res(p)` should reject with TypeError per spec. */
        var p;
        p = new Promise(function (res) { setTimeout(function () { res(p); }, 5); });
        return p.then(function () { throw new Error("self-resolve should reject"); },
                      function (e) {
                          must(e instanceof TypeError || /chain|cycle|itself|TypeError/i.test(String(e)),
                               "self-resolve rejects with TypeError");
                      });
    })
    /* === Microtask ordering: then() callbacks run before next setTimeout === */
    .then(function () {
        var order = [];
        return new Promise(function (res) {
            setTimeout(function () { order.push('timer'); res(); }, 1);
        }).then(function () {
            /* Schedule a microtask via Promise.resolve().then; it must
               run before the next setTimeout-scheduled callback. */
            order.push('then1');
            Promise.resolve().then(function () { order.push('microtask'); });
            return new Promise(function (res) {
                setTimeout(function () { order.push('timer2'); res(); }, 5);
            });
        }).then(function () {
            /* By now: timer, then1, microtask should have all run
               BEFORE timer2.  We can't guarantee microtask was before
               timer2 if the engine doesn't drain microtasks between
               tasks — but spec requires it. */
            mustEq(order[0], 'timer',    "first: timer");
            mustEq(order[1], 'then1',    "second: then1 (microtask after timer)");
            mustEq(order[2], 'microtask', "third: nested microtask before timer2");
            mustEq(order[3], 'timer2',   "fourth: timer2");
        });
    })
    /* === done === */
    .then(function () { _doneAsync(); },
          function (e) { _asyncFail("Promise (async)", e); _doneAsync(); });

    return true;
});


testJS("Buffer (statics)", function() {
    /* Buffer must be a global */
    must(typeof Buffer === 'function', "Buffer is global");

    /* alloc */
    mustEq(Buffer.alloc(4).length, 4, "alloc length");
    mustEq(Buffer.alloc(4)[0],     0, "alloc zero-filled");
    mustEq(Buffer.alloc(3, 0x61).toString(), 'aaa', "alloc with fill byte");

    /* allocUnsafe is the same allocator in rampart; node may differ on
       initial contents but length + writability are guaranteed. */
    must(Buffer.allocUnsafe(8).length === 8, "allocUnsafe length");

    /* from(string), from(array), from(hex) */
    mustEq(Buffer.from('hello').toString(),         'hello', "from string");
    mustEq(Buffer.from([72, 105]).toString(),       'Hi',    "from byte array");
    mustEq(Buffer.from('48656c6c6f', 'hex').toString(), 'Hello', "from hex");
    mustEq(Buffer.from('aGVsbG8=', 'base64').toString(), 'hello', "from base64");

    /* byteLength */
    mustEq(Buffer.byteLength('hi'),         2, "byteLength ascii");
    mustEq(Buffer.byteLength('é', 'utf8'),  2, "byteLength utf8 multibyte");

    /* isEncoding (canonical names are accepted by both runtimes) */
    must(Buffer.isEncoding('utf8'),     "isEncoding utf8");
    must(Buffer.isEncoding('hex'),      "isEncoding hex");
    must(Buffer.isEncoding('base64'),   "isEncoding base64");
    must(!Buffer.isEncoding('bogus'),   "isEncoding rejects bogus");
});


testJS("Buffer (prototype)", function() {
    var b = Buffer.from('hello world');

    /* toString with encoding + slice */
    mustEq(b.toString(),                'hello world', "toString default");
    mustEq(b.toString('utf8', 0, 5),    'hello',       "toString slice");
    mustEq(b.toString('hex'),           '68656c6c6f20776f726c64', "toString hex");
    mustEq(b.toString('base64'),        'aGVsbG8gd29ybGQ=', "toString base64");

    /* indexOf / lastIndexOf / includes (string-aware) */
    mustEq(b.indexOf('world'),     6, "indexOf string");
    mustEq(b.indexOf('xyz'),      -1, "indexOf miss");
    mustEq(b.lastIndexOf('l'),     9, "lastIndexOf");
    must(b.includes('hello'),       "includes match");
    must(!b.includes('xyz'),        "includes miss");

    /* subarray shares backing memory; mutate src and observe in subarray */
    var src = Buffer.from('abcdef');
    var sub = src.subarray(1, 4);
    mustEq(sub.toString(), 'bcd', "subarray view");
    src[1] = 0x42;  /* 'B' */
    mustEq(sub.toString(), 'Bcd', "subarray shares memory");

    /* keys/values/entries: per spec these return iterators (an object
       with .next() and [Symbol.iterator]), not Arrays.  Buffer
       inherits all three from %TypedArray%.prototype, which the
       rampart-buffer.c JS polyfill installs with iterator semantics. */
    var ki = b.keys();
    must(typeof ki.next === 'function', "keys() returns iterator (has .next)");
    must(!Array.isArray(ki),            "keys() result is not an Array");
    var step = ki.next();
    must(step.done === false && step.value === 0, "keys.next step 1");

    var vi = b.values();
    must(typeof vi.next === 'function', "values() returns iterator");
    mustEq(vi.next().value, 0x68, "values.next 'h'");

    var ei = b.entries();
    must(typeof ei.next === 'function', "entries() returns iterator");
    var e0 = ei.next().value;
    mustEq(e0[0], 0,    "entries.next [0] index");
    mustEq(e0[1], 0x68, "entries.next [0] value");

    /* Iterators are self-iterable */
    if (typeof Symbol !== 'undefined' && Symbol.iterator) {
        var it = b.values();
        must(typeof it[Symbol.iterator] === 'function', "iterator has [Symbol.iterator]");
        must(it[Symbol.iterator]() === it, "self-iterable");
    }

    /* Array.from over the iterator yields the full set */
    mustEq(Array.from(b.keys()).length, 11, "Array.from(keys) length");
    mustEq(Array.from(b.values())[10], 0x64, "Array.from(values) last byte");
    mustEq(Array.from(b.entries()).length, 11, "Array.from(entries) length");

    /* swap16 / swap32: even / multiple-of-4 length required */
    var s16 = Buffer.from([1, 2, 3, 4]);
    s16.swap16();
    mustEq(s16[0], 2, "swap16 byte 0");
    mustEq(s16[1], 1, "swap16 byte 1");

    var s32 = Buffer.from([1, 2, 3, 4]);
    s32.swap32();
    mustEq(s32[0], 4, "swap32 byte 0");
    mustEq(s32[3], 1, "swap32 byte 3");

    /* inspect: format varies between node/rampart but both contain
       'Buffer' or the angle-brackets. */
    var insp = b.inspect();
    must(insp.indexOf('Buffer') >= 0, "inspect contains 'Buffer'");
});


testJS("TypedArray prototype", function() {
    /* These are installed on the shared %TypedArray% prototype, so all
       typed arrays inherit them.  Test on Uint8Array. */
    var u = new Uint8Array([10, 20, 30, 40]);

    /* iteration helpers */
    var sum = 0;
    u.forEach(function(v) { sum += v; });
    mustEq(sum, 100, "forEach");

    var m = u.map(function(v) { return v * 2; });
    must(m instanceof Uint8Array, "map returns same TypedArray type");
    mustEq(m[0], 20, "map[0]");
    mustEq(m[3], 80, "map[3]");

    var f = u.filter(function(v) { return v >= 20; });
    mustEq(f.length, 3, "filter length");
    mustEq(f[0], 20, "filter[0]");

    mustEq(u.reduce(function(a, b) { return a + b; }),         100,  "reduce");
    mustEq(u.reduce(function(a, b) { return a + b; }, 1000),   1100, "reduce w/initial");
    mustEq(u.reduceRight(function(a, b) { return a + ',' + b; }), '40,30,20,10', "reduceRight order");

    must(u.some(function(v)  { return v > 30; }),  "some true");
    must(!u.some(function(v) { return v > 99; }),  "some false");
    must(u.every(function(v) { return v > 0; }),   "every true");
    must(!u.every(function(v){ return v > 30; }),  "every false");

    mustEq(u.find(function(v)         { return v > 15; }), 20, "find");
    mustEq(u.findIndex(function(v)    { return v > 15; }),  1, "findIndex");
    mustEq(u.findLast(function(v)     { return v < 35; }), 30, "findLast");
    mustEq(u.findLastIndex(function(v){ return v < 35; }),  2, "findLastIndex");

    /* at */
    mustEq(u.at(-1),  40, "at(-1)");
    mustEq(u.at(0),   10, "at(0)");

    /* slice / fill / copyWithin */
    var sl = u.slice(1, 3);
    mustEq(sl.length, 2,  "slice length");
    mustEq(sl[0],     20, "slice[0]");

    var fi = new Uint8Array(4);
    fi.fill(7);
    mustEq(fi[0], 7, "fill");
    mustEq(fi[3], 7, "fill last");

    var cw = new Uint8Array([1, 2, 3, 4, 5]);
    cw.copyWithin(0, 3);            /* copy from idx 3 onward to idx 0 */
    mustEq(cw[0], 4, "copyWithin[0]");
    mustEq(cw[1], 5, "copyWithin[1]");
    mustEq(cw[2], 3, "copyWithin keeps tail");

    /* reverse / sort */
    var rv = new Uint8Array([1, 2, 3]);
    rv.reverse();
    mustEq(rv[0], 3, "reverse[0]");

    var so = new Uint8Array([5, 1, 3, 2]);
    so.sort();
    mustEq(so[0], 1, "sort numeric (not lex)");
    mustEq(so[3], 5, "sort numeric tail");

    /* iterators + [Symbol.iterator] */
    var arr = [];
    var v2, it = u.values();
    while (!(v2 = it.next()).done) arr.push(v2.value);
    mustEq(arr.join(','), '10,20,30,40', "values iterator");

    var es = [], it2 = u.entries();
    while (!(v2 = it2.next()).done) es.push(v2.value[0] + ':' + v2.value[1]);
    mustEq(es.join(','), '0:10,1:20,2:30,3:40', "entries iterator");

    if (typeof Symbol !== 'undefined' && Symbol.iterator) {
        must(typeof u[Symbol.iterator] === 'function', "[Symbol.iterator]");
        /* for-of via the typed-array */
        var collect = [];
        var iter = u[Symbol.iterator]();
        var step;
        while (!(step = iter.next()).done) collect.push(step.value);
        mustEq(collect.join(','), '10,20,30,40', "for-of equivalent");
    }

    /* join */
    mustEq(u.join('-'), '10-20-30-40', "join");
});


testJS("TypedArray @@toStringTag (ES2015 22.2.3.31)", function() {
    /* Spec installs an accessor descriptor on %TypedArray%.prototype
       whose getter returns the subtype name or undefined for non-TA
       receivers (does NOT throw).  Upstream duktape produces
       "[object X]" via the class-number table but doesn't expose the
       symbol property at all; rampart's DUK_RP_USE_TYPEDARRAY_EXTRAS
       installs the accessor to match node + the spec. */

    var TAproto = Object.getPrototypeOf(Int8Array.prototype);
    var d = Object.getOwnPropertyDescriptor(TAproto, Symbol.toStringTag);

    /* Descriptor shape: accessor, non-enumerable, configurable, no setter. */
    must(d,                                   "descriptor present on %TypedArray%.prototype");
    must(typeof d.get === 'function',         "accessor (getter present)");
    must(d.set === undefined,                 "no setter");
    must(d.enumerable === false,              "not enumerable");
    must(d.configurable === true,             "configurable");
    must(!('value' in d) && !('writable' in d),
                                              "not a data descriptor");

    /* Subtype dispatch: each instance returns its constructor name. */
    mustEq(new Int8Array()[Symbol.toStringTag],         "Int8Array",         "Int8Array tag");
    mustEq(new Uint8Array()[Symbol.toStringTag],        "Uint8Array",        "Uint8Array tag");
    mustEq(new Uint8ClampedArray()[Symbol.toStringTag], "Uint8ClampedArray", "Uint8ClampedArray tag");
    mustEq(new Float64Array()[Symbol.toStringTag],      "Float64Array",      "Float64Array tag");

    /* Spec edge: non-TypedArray receivers must yield undefined, NOT throw. */
    var get = d.get;
    mustEq(get.call({}),                  undefined, "plain object -> undefined");
    mustEq(get.call(null),                undefined, "null -> undefined (no throw)");
    mustEq(get.call(undefined),           undefined, "undefined -> undefined (no throw)");
    mustEq(get.call(42),                  undefined, "primitive -> undefined");
    mustEq(get.call(new ArrayBuffer(8)),  undefined, "ArrayBuffer -> undefined");
    mustEq(get.call(new DataView(new ArrayBuffer(8))),
                                          undefined, "DataView -> undefined");
    mustEq(get.call(Int8Array.prototype), undefined, "prototype itself -> undefined");

    /* The accessor is installed on %TypedArray%.prototype only;
       per-subtype prototypes inherit it (no own descriptor). */
    mustEq(Object.getOwnPropertyDescriptor(Int8Array.prototype,    Symbol.toStringTag), undefined, "no own descriptor on Int8Array.prototype");
    mustEq(Object.getOwnPropertyDescriptor(Float64Array.prototype, Symbol.toStringTag), undefined, "no own descriptor on Float64Array.prototype");

    /* Object.prototype.toString still works (rampart's class-number
       path is independent of the accessor; spec now routes through
       the accessor but the result is the same). */
    mustEq(Object.prototype.toString.call(new Int8Array()),    "[object Int8Array]",    "toString.call(Int8Array)");
    mustEq(Object.prototype.toString.call(new Float64Array()), "[object Float64Array]", "toString.call(Float64Array)");
});


testJS("TextEncoder / TextDecoder", function() {
    must(typeof TextEncoder === 'function', "TextEncoder is global");
    must(typeof TextDecoder === 'function', "TextDecoder is global");

    /* encode */
    var enc = new TextEncoder();
    var bytes = enc.encode('hello');
    must(bytes instanceof Uint8Array, "encode returns Uint8Array");
    mustEq(bytes.length, 5, "encode length ascii");
    mustEq(bytes[0],   0x68, "encode 'h'");

    /* utf-8 multibyte */
    var b2 = enc.encode('é');
    mustEq(b2.length, 2, "encode utf8 multibyte length");

    /* decode utf-8 (default) */
    var dec = new TextDecoder();
    mustEq(dec.decode(bytes), 'hello', "decode utf-8 default");

    /* decode utf-16le */
    var dec16 = new TextDecoder('utf-16le');
    var d16 = new Uint8Array([0x68, 0, 0x69, 0]);
    mustEq(dec16.decode(d16), 'hi', "decode utf-16le");

    /* decode latin1 */
    var declat = new TextDecoder('iso-8859-1');
    mustEq(declat.decode(new Uint8Array([0xe9])), 'é', "decode iso-8859-1");

    /* WHATWG aliases */
    must(new TextDecoder('latin1') instanceof TextDecoder, "alias latin1");
    must(new TextDecoder('ascii') instanceof TextDecoder, "alias ascii");

    /* Unknown encoding throws RangeError */
    mustThrow(function() { new TextDecoder('not-a-real-encoding'); },
              "unknown encoding -> throws");
});


testJS("console extras", function() {
    must(typeof console.time          === 'function', "console.time");
    must(typeof console.timeEnd       === 'function', "console.timeEnd");
    must(typeof console.timeLog       === 'function', "console.timeLog");
    must(typeof console.count         === 'function', "console.count");
    must(typeof console.countReset    === 'function', "console.countReset");
    must(typeof console.group         === 'function', "console.group");
    must(typeof console.groupEnd      === 'function', "console.groupEnd");
    must(typeof console.groupCollapsed === 'function', "console.groupCollapsed");
    must(typeof console.clear         === 'function', "console.clear");
    must(typeof console.table         === 'function', "console.table");

    /* Exercise without polluting the output: temporarily swallow the
       log/info methods, call the timing/count/group APIs, restore. */
    var origLog  = console.log;
    var origInfo = console.info;
    var origWarn = console.warn;
    console.log = function() {};
    console.info = function() {};
    console.warn = function() {};
    var threw = null;
    try {
        console.time('t');
        console.timeLog('t');
        console.timeEnd('t');
        console.count('c');
        console.count('c');
        console.countReset('c');
        console.group('g');
        console.groupEnd();
    } catch (e) {
        threw = e;
    } finally {
        console.log  = origLog;
        console.info = origInfo;
        console.warn = origWarn;
    }
    if (threw) throw new Error("time/count/group threw: " + threw.message);
});



/* ============================================================
 * WHATWG / W3C Web platform globals are tested in whatwg-test.js
 * (URL/Headers/Request/Response/fetch/Blob/File/Streams/Web Crypto/
 * EventTarget/WebSocket/XMLHttpRequest/MessagePort/etc.) — that
 * file is the home for everything rampart-whatwg.so installs.
 * Only ECMAScript/duktape/rampart-core extensions live below.
 * ============================================================ */

/* ============================================================
 * Grouped subtest suites
 *
 * These three categories aggregate many subtests under one
 * testJS call.  Each subtest uses a local _sub() helper that
 * collects failures; at the end of the testJS body we call
 * must() with the concatenated failure list, so the harness
 * prints exactly one line per category (pass or fail with
 * the failing subtest name).
 *
 * Sourced from:
 *   - claude-work/weakrefs/weakref-test.js   (57 assertions)
 *   - claude-work/native-promise/promise-test.js (21 assertions)
 *   - claude-work/duktape-symbol-gaps/probe.js (21 assertions)
 *
 * All three exercise rampart-specific duktape extensions and
 * are skipped under node (node has its own native impls).
 * ============================================================ */

testJS("WeakRef family (WeakRef/WeakMap/WeakSet/FinalizationRegistry, 57 subtests)", function() {
    if (!_isRampart) return;  /* rampart-specific GC integration */

    var fails = [];
    function _sub(name, actual, expected) {
        var ok;
        if (typeof expected === "function") {
            ok = expected(actual);
        } else {
            ok = (actual === expected);
        }
        if (!ok) {
            fails.push(name + " (got " + JSON.stringify(actual) +
                       " want " + (typeof expected === 'function' ? '<predicate>' : JSON.stringify(expected)) + ")");
        }
    }

    /* --- WeakRef --- */
    _sub("WeakRef typeof", typeof WeakRef, "function");
    _sub("WeakRef.name",   WeakRef.name,   "WeakRef");
    _sub("WeakRef.length", WeakRef.length, 1);
    var o = {x: 42};
    var w = new WeakRef(o);
    _sub("instanceof WeakRef", w instanceof WeakRef, true);
    _sub("WeakRef toString tag", Object.prototype.toString.call(w), "[object WeakRef]");
    _sub("WeakRef deref live", w.deref(), o);
    var threw;
    threw = false; try { WeakRef({}); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakRef call without new throws", threw, true);
    threw = false; try { new WeakRef(5); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakRef primitive target throws", threw, true);
    threw = false; try { new WeakRef(null); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakRef null target throws", threw, true);
    threw = false; try { WeakRef.prototype.deref.call({}); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakRef deref wrong receiver", threw, true);
    var w2;
    (function() {
        var t = {payload: "rc-zero"};
        w2 = new WeakRef(t);
    })();
    _sub("WeakRef refcount-zero clears deref", w2.deref(), undefined);
    var w3 = new WeakRef(o);
    _sub("WeakRef deref before GC", w3.deref(), o);
    o = null;
    Duktape.gc();
    _sub("WeakRef deref after target GC'd", w3.deref(), undefined);

    /* --- WeakMap --- */
    _sub("WeakMap typeof", typeof WeakMap, "function");
    _sub("WeakMap.name",   WeakMap.name,   "WeakMap");
    var m = new WeakMap();
    _sub("WeakMap toString tag", Object.prototype.toString.call(m), "[object WeakMap]");
    var k1 = {n:1}, k2 = {n:2};
    m.set(k1, "one");
    m.set(k2, "two");
    _sub("WeakMap get k1",         m.get(k1),         "one");
    _sub("WeakMap get k2",         m.get(k2),         "two");
    _sub("WeakMap has k1",         m.has(k1),         true);
    _sub("WeakMap get missing",    m.get({}),         undefined);
    _sub("WeakMap has missing",    m.has({}),         false);
    _sub("WeakMap set returns this", m.set({}, 1) instanceof WeakMap, true);
    m.set(k1, "one-new");
    _sub("WeakMap update in place", m.get(k1), "one-new");
    _sub("WeakMap delete returns true",  m.delete(k1), true);
    _sub("WeakMap delete missing false", m.delete(k1), false);
    _sub("WeakMap k1 gone after delete", m.has(k1),    false);
    _sub("WeakMap k2 still here",        m.has(k2),    true);
    threw = false;
    try { m.set("str", 1); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakMap primitive key throws", threw, true);
    var k3 = {n:3}, k4 = {n:4};
    var m2 = new WeakMap([[k3, "three"], [k4, "four"]]);
    _sub("WeakMap iter-ctor k3", m2.get(k3), "three");
    _sub("WeakMap iter-ctor k4", m2.get(k4), "four");
    var m3 = new WeakMap(null);
    _sub("WeakMap null-arg ctor", m3.has({}), false);
    var m4 = new WeakMap(undefined);
    _sub("WeakMap undef-arg ctor", m4.has({}), false);
    var m5 = new WeakMap();
    var alive = {a:1};
    m5.set(alive, "kept");
    (function() {
        var dead = {d:1};
        m5.set(dead, "gone");
    })();
    Duktape.gc();
    _sub("WeakMap alive entry after GC", m5.get(alive), "kept");
    var k5 = {n:5}; m5.set(k5, "five");
    _sub("WeakMap after dead-key GC, new set", m5.get(k5), "five");

    /* --- WeakSet --- */
    _sub("WeakSet typeof", typeof WeakSet, "function");
    _sub("WeakSet.name",   WeakSet.name,   "WeakSet");
    var s = new WeakSet();
    _sub("WeakSet toString tag", Object.prototype.toString.call(s), "[object WeakSet]");
    var a = {n:1}, b = {n:2};
    s.add(a);
    _sub("WeakSet has a after add",  s.has(a), true);
    _sub("WeakSet has b not added",  s.has(b), false);
    s.add(b);
    _sub("WeakSet add returns this", s.add({}) instanceof WeakSet, true);
    s.add(a); s.add(a);
    _sub("WeakSet dedupe via add", s.has(a), true);
    _sub("WeakSet delete a true",   s.delete(a), true);
    _sub("WeakSet delete a again",  s.delete(a), false);
    _sub("WeakSet has a after del", s.has(a),    false);
    _sub("WeakSet has b still",     s.has(b),    true);
    threw = false;
    try { s.add(5); } catch(e) { threw = e instanceof TypeError; }
    _sub("WeakSet primitive add throws", threw, true);
    var c = {c:1}, d = {d:1};
    var s2 = new WeakSet([c, d]);
    _sub("WeakSet iter-ctor c", s2.has(c), true);
    _sub("WeakSet iter-ctor d", s2.has(d), true);

    /* --- FinalizationRegistry --- */
    _sub("FinReg typeof", typeof FinalizationRegistry, "function");
    _sub("FinReg.name",   FinalizationRegistry.name,   "FinalizationRegistry");
    threw = false;
    try { new FinalizationRegistry({}); } catch(e) { threw = e instanceof TypeError; }
    _sub("FinReg non-fn ctor throws", threw, true);
    threw = false;
    try { FinalizationRegistry(function(){}); } catch(e) { threw = e instanceof TypeError; }
    _sub("FinReg no-new throws", threw, true);
    var fr = new FinalizationRegistry(function() {});
    _sub("FinReg toString tag", Object.prototype.toString.call(fr), "[object FinalizationRegistry]");
    var token1 = {tag: "t1"};
    var token2 = {tag: "t2"};
    var t1 = {n:1}, t2 = {n:2};
    fr.register(t1, "held-1", token1);
    fr.register(t2, "held-2", token2);
    _sub("FinReg unregister t1 found",       fr.unregister(token1), true);
    _sub("FinReg unregister t1 twice false", fr.unregister(token1), false);
    _sub("FinReg unregister unknown false",  fr.unregister({}),     false);
    threw = false;
    try { fr.register(5, "x"); } catch(e) { threw = e instanceof TypeError; }
    _sub("FinReg primitive target throws", threw, true);

    must(fails.length === 0, "subtest failures: " + fails.join("; "));
});

/* Native Promise tests run async: we schedule Promise chains
 * synchronously and let the libevent microtask drain settle them
 * naturally between event-loop turns.  A two-hop setTimeout defers the
 * final verdict; failures are reported via _asyncFail (which also
 * emits a FAILED line for run_tests.sh to grep). */
_pendingAsync++;
testJS("Native Promise (constructor/then/catch/finally/resolve/reject/all/race/allSettled/any, 21 subtests)", function() {
    if (!_isRampart) { _doneAsync(); return; }

    var fails = [];
    function _sub(name, got, want) {
        if (got !== want) fails.push(name + " (got " + JSON.stringify(got) +
                                     " want " + JSON.stringify(want) + ")");
    }

    /* Basic resolve/then */
    Promise.resolve(1).then(function(v) { _sub("resolve.then", v, 1); });
    /* Reject/catch */
    Promise.reject("bad").catch(function(r) { _sub("reject.catch", r, "bad"); });
    /* Chain: then returning value */
    Promise.resolve(10).then(function(v) {
        return v + 1;
    }).then(function(v) { _sub("chain returning value", v, 11); });
    /* Chain: then returning thrown error becomes rejection */
    Promise.resolve(1).then(function(v) {
        throw new Error("boom");
    }).catch(function(e) { _sub("then throw -> catch", e.message, "boom"); });
    /* Chain: then returning Promise */
    Promise.resolve(1).then(function(v) {
        return Promise.resolve(v * 100);
    }).then(function(v) { _sub("then returning Promise", v, 100); });
    /* Constructor with executor */
    new Promise(function(resolve, reject) {
        resolve("from-exec");
    }).then(function(v) { _sub("ctor resolve", v, "from-exec"); });
    /* Executor throw -> rejection */
    new Promise(function(resolve, reject) {
        throw new Error("exec-throw");
    }).catch(function(e) { _sub("exec throw -> catch", e.message, "exec-throw"); });
    /* Promise.all */
    Promise.all([Promise.resolve(1), Promise.resolve(2), 3]).then(function(arr) {
        _sub("all length", arr.length, 3);
        _sub("all[0]", arr[0], 1);
        _sub("all[1]", arr[1], 2);
        _sub("all[2]", arr[2], 3);
    });
    /* Promise.all empty */
    Promise.all([]).then(function(arr) { _sub("all empty", arr.length, 0); });
    /* Promise.race */
    Promise.race([Promise.resolve(99), Promise.resolve(2)]).then(function(v) {
        _sub("race", v, 99);
    });
    /* Promise.allSettled */
    Promise.allSettled([Promise.resolve(1), Promise.reject("err")]).then(function(arr) {
        _sub("allSettled length",        arr.length,      2);
        _sub("allSettled[0].status",     arr[0].status,   "fulfilled");
        _sub("allSettled[0].value",      arr[0].value,    1);
        _sub("allSettled[1].status",     arr[1].status,   "rejected");
        _sub("allSettled[1].reason",     arr[1].reason,   "err");
    });
    /* Promise.any */
    Promise.any([Promise.reject(1), Promise.resolve(42)]).then(function(v) {
        _sub("any", v, 42);
    });
    /* finally pass-through (fulfilled) */
    Promise.resolve(5).finally(function() {})
        .then(function(v) { _sub("finally fulfill pass-through", v, 5); });
    /* finally pass-through (rejected) */
    Promise.reject("rj").finally(function() {})
        .catch(function(r) { _sub("finally reject pass-through", r, "rj"); });
    /* Defensive: then.call(undefined) — the NDE.40 trigger.  Polyfill
     * version threw; native version should adopt and behave gracefully. */
    try {
        Promise.prototype.then.call(undefined, function(v){}, function(){})
            .catch(function(e) { _sub("detached then -> rejection", true, true); });
    } catch(e) {
        fails.push("detached then crashed: " + e.message);
    }

    /* Defer the verdict through the event loop so all chained
     * reactions have settled.  Two hops cover chains that themselves
     * enqueue further reactions (Promise.all, then chains, etc.). */
    setTimeout(function() {
        setTimeout(function() {
            try {
                must(fails.length === 0, "subtest failures: " + fails.join("; "));
            } catch(e) {
                _asyncFail("Native Promise", e);
            }
            _doneAsync();
        }, 0);
    }, 0);
});

testJS("Symbol-key spec gaps (Object.assign/Reflect.*/getOwnPropertyDescriptors/spread/etc, 21 subtests)", function() {
    if (!_isRampart) return;  /* rampart-specific duktape spec-conformance fixes */

    var fails = [];
    function _sub(name, got, want) {
        var ok = (got === want);
        if (!ok) fails.push(name + " (got " + JSON.stringify(got) +
                            " want " + JSON.stringify(want) + ")");
    }
    function _wrap(label, fn) {
        try { fn(); } catch(e) { fails.push(label + " threw: " + e.message); }
    }

    var s1 = Symbol("s1"), s2 = Symbol("s2"), s3 = Symbol("s3");

    /* Object.assign: SHOULD copy symbol keys (rampart-fixed). */
    _wrap("Object.assign", function() {
        var src = {x: 1};
        src[s1] = "v1";
        src[s2] = "v2";
        var dst = {};
        Object.assign(dst, src);
        _sub("assign: copies string", dst.x, 1);
        _sub("assign: copies symbol s1", dst[s1], "v1");
        _sub("assign: copies symbol s2", dst[s2], "v2");
        var src2 = {};
        Object.defineProperty(src2, s3, {value: "ne", enumerable: false});
        var dst2 = {};
        Object.assign(dst2, src2);
        _sub("assign: skips non-enumerable symbol", dst2[s3], undefined);
    });

    /* Object.keys: should NOT include symbols (spec). */
    _wrap("Object.keys", function() {
        var src = {x: 1};
        src[s1] = "v1";
        var keys = Object.keys(src);
        _sub("Object.keys excludes symbol", keys.length === 1 && keys[0] === "x", true);
    });

    /* Object.values: should NOT include symbol-keyed values. */
    _wrap("Object.values", function() {
        var src = {x: 1};
        src[s1] = "v1";
        var values = Object.values(src);
        _sub("Object.values excludes symbol value", values.length === 1 && values[0] === 1, true);
    });

    /* Object.entries: should NOT include symbol entries. */
    _wrap("Object.entries", function() {
        var src = {x: 1};
        src[s1] = "v1";
        var entries = Object.entries(src);
        _sub("Object.entries excludes symbol", entries.length === 1 && entries[0][0] === "x", true);
    });

    /* Object.getOwnPropertyNames: strings only. */
    _wrap("Object.getOwnPropertyNames", function() {
        var src = {x: 1};
        src[s1] = "v1";
        var names = Object.getOwnPropertyNames(src);
        _sub("getOwnPropertyNames is strings only", names.length === 1 && names[0] === "x", true);
    });

    /* Object.getOwnPropertySymbols: symbols only. */
    _wrap("Object.getOwnPropertySymbols", function() {
        var src = {x: 1};
        src[s1] = "v1";
        src[s2] = "v2";
        _sub("getOwnPropertySymbols count", Object.getOwnPropertySymbols(src).length, 2);
    });

    /* Object.getOwnPropertyDescriptors (rampart-added, ES2017). */
    _wrap("Object.getOwnPropertyDescriptors", function() {
        var src = {x: 1};
        src[s1] = "v1";
        var descs = Object.getOwnPropertyDescriptors(src);
        _sub("descriptors: string key present", descs.x.value, 1);
        _sub("descriptors: symbol key present",
             descs[s1] !== undefined && descs[s1].value === "v1", true);
    });

    /* Reflect.ownKeys: both strings and symbols. */
    _wrap("Reflect.ownKeys", function() {
        var src = {x: 1, y: 2};
        src[s1] = "v1";
        var keys = Reflect.ownKeys(src);
        _sub("Reflect.ownKeys count (2 strings + 1 symbol)", keys.length, 3);
        _sub("Reflect.ownKeys contains s1", keys.indexOf(s1) >= 0, true);
    });

    /* Spread {...src}: native syntax not supported by raw rampart parser;
     * the transpiler's __spreadO cascade is verified in the transpiler
     * suite.  Here we just confirm Object.assign (which __spreadO uses
     * downstream via _objectAddchain) does the right thing — already
     * covered above. */

    /* JSON.stringify: skips symbol keys and values. */
    _wrap("JSON.stringify", function() {
        var src = {x: 1};
        src[s1] = "v1";
        _sub("JSON.stringify excludes symbol key", JSON.stringify(src), '{"x":1}');
        var src2 = {x: 1, y: Symbol("v")};
        _sub("JSON.stringify drops symbol value", JSON.stringify(src2), '{"x":1}');
    });

    /* for-in: skips symbols. */
    _wrap("for-in", function() {
        var src = {x: 1, y: 2};
        src[s1] = "v1";
        var seen = [];
        for (var k in src) seen.push(k);
        _sub("for-in keys are strings only",
             seen.length === 2 && seen.indexOf("x") >= 0 && seen.indexOf("y") >= 0, true);
    });

    /* Map with symbol keys. */
    _wrap("Map symbol keys", function() {
        var m = new Map();
        m.set(s1, "v1");
        m.set(s2, "v2");
        _sub("Map symbol key get", m.get(s1), "v1");
        _sub("Map symbol key has", m.has(s2), true);
        _sub("Map size", m.size, 2);
    });

    /* Object.fromEntries with symbol keys (rampart-added, ES2019). */
    _wrap("Object.fromEntries", function() {
        var entries = [["x", 1], [s1, "sv1"]];
        var obj = Object.fromEntries(entries);
        _sub("fromEntries: string entry", obj.x, 1);
        _sub("fromEntries: symbol entry", obj[s1], "sv1");
    });

    must(fails.length === 0, "subtest failures: " + fails.join("; "));
});

/* Safety net: if scheduling somehow doesn't fire, exit after a tick. */
setTimeout(function () {
    if (!_exitOnce) {
        _exitOnce = true;
        testJS.exit();
    }
}, 500);
