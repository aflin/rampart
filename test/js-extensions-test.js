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


testJS("Array statics", function() {
    /* Array.from on various input shapes */
    mustEq(Array.from('abc').join(','), 'a,b,c', "from string");
    mustEq(Array.from([1, 2, 3]).join(','), '1,2,3', "from array");
    mustEq(Array.from([1, 2, 3], function(x) { return x * 10; }).join(','),
           '10,20,30', "from array + mapFn");
    mustEq(Array.from('abc', function(c) { return c.toUpperCase(); }).join(''),
           'ABC', "from string + mapFn");
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


testJS("Promise (vanilla rampart)", function() {
    /* Rampart installs Promise eagerly at context init
       (src/duktape/globals/rampart-promise.c) so vanilla
       `./rampart script.js` has it without any `-t` / `-b`. */
    must(typeof Promise === 'function', "Promise is a function");
    must(typeof Promise.resolve === 'function', "Promise.resolve");
    must(typeof Promise.reject === 'function', "Promise.reject");
    must(typeof Promise.all === 'function', "Promise.all");
    must(typeof Promise.race === 'function', "Promise.race");
    must(typeof Promise.allSettled === 'function', "Promise.allSettled (ES2020)");
    must(typeof Promise.any === 'function', "Promise.any (ES2021)");
    must(typeof Promise.prototype.then === 'function', "Promise.prototype.then");
    must(typeof Promise.prototype['catch'] === 'function', "Promise.prototype.catch");
    must(typeof Promise.prototype['finally'] === 'function', "Promise.prototype.finally (ES2018)");

    /* Constructor shape — `new Promise(executor)` */
    var p = new Promise(function(resolve){ resolve("ok"); });
    must(p && typeof p.then === "function", "new Promise returns thenable");
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
 * WHATWG / W3C Web platform standards (rampart-whatwg.so, lazy)
 * Migrated from the now-deleted node-compat-test.js.  All of these
 * trigger the lazy load on first access; subsequent accesses use the
 * installed real values.
 * ============================================================ */

testJS("URL / URLSearchParams", function () {
    var u = new URL('https://user:pass@example.com:8080/path?q=1#frag');
    mustEq(u.protocol, 'https:',       "URL.protocol");
    mustEq(u.hostname, 'example.com',  "URL.hostname");
    mustEq(u.port,     '8080',         "URL.port");
    mustEq(u.pathname, '/path',        "URL.pathname");
    mustEq(u.search,   '?q=1',         "URL.search");
    mustEq(u.hash,     '#frag',        "URL.hash");
    var sp = new URLSearchParams('a=1&b=2');
    mustEq(sp.get('a'), '1', "URLSearchParams.get");
    sp.append('c', '3');
    must(sp.toString().indexOf('c=3') >= 0, "URLSearchParams.append");
    /* identity: require('url').URL === global URL (rampart only;
       node uses different module wiring) */
    if (_isRampart) {
        var modURL = require('url').URL;
        mustEq(modURL, URL, "require('url').URL === global URL");
    }
});

testJS("performance (extras + timeOrigin)", function () {
    /* performance.now() is duktape native; performance.mark/measure/
       getEntries* etc. trigger rampart-whatwg lazy load. */
    must(typeof performance.now === 'function', "performance.now");
    must(typeof performance.mark === 'function', "performance.mark");
    must(typeof performance.measure === 'function', "performance.measure");
    must(typeof performance.getEntries === 'function', "performance.getEntries");
    must(typeof performance.timeOrigin === 'number' && performance.timeOrigin > 0,
         "performance.timeOrigin > 0");
    /* mark/measure round-trip */
    performance.mark('whatwg-test-a');
    performance.mark('whatwg-test-b');
    var meas = performance.measure('whatwg-test-ab', 'whatwg-test-a', 'whatwg-test-b');
    must(meas && typeof meas.duration === 'number', "measure returns entry with .duration");
    var entries = performance.getEntriesByName('whatwg-test-ab');
    must(entries.length >= 1, "getEntriesByName finds measure");
    performance.clearMarks('whatwg-test-a');
    performance.clearMarks('whatwg-test-b');
    performance.clearMeasures('whatwg-test-ab');
});

testJS("atob / btoa", function () {
    mustEq(btoa('hello'),    'aGVsbG8=', "btoa(hello)");
    mustEq(atob('aGVsbG8='), 'hello',    "atob(aGVsbG8=)");
    /* round-trip with all 256 byte values */
    var bin = '';
    for (var i = 0; i < 256; i++) bin += String.fromCharCode(i);
    mustEq(atob(btoa(bin)), bin, "atob/btoa 256-byte round-trip");
});

testJS("reportError", function () {
    /* node v22 still doesn't ship reportError as a global — gate. */
    if (typeof reportError !== 'function') {
        if (_isRampart) throw new Error("reportError missing in rampart-whatwg");
        return true;  /* node gap — accept */
    }
    /* Should NOT throw — silence console.error during the call. */
    var origErr = console.error;
    console.error = function () {};
    try { reportError(new Error('report-test')); }
    finally { console.error = origErr; }
});

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

testJS("MessageChannel / MessagePort", function () {
    must(typeof MessageChannel === 'function', "MessageChannel global");
    must(typeof MessagePort    === 'function', "MessagePort global");
    var mc = new MessageChannel();
    must(mc.port1 && mc.port2,                  "channel has two ports");
    must(mc.port1 !== mc.port2,                 "ports are distinct");
    /* postMessage round-trip is async — covered in the async tail */
    mc.port1.close(); mc.port2.close();
});

testJS("navigator", function () {
    must(typeof navigator === 'object',                "navigator is object");
    must(typeof navigator.userAgent === 'string',      "userAgent string");
    must(navigator.userAgent.length > 0,               "userAgent non-empty");
    must(typeof navigator.platform === 'string',       "platform string");
    must(typeof navigator.hardwareConcurrency === 'number',  "hardwareConcurrency number");
    must(navigator.hardwareConcurrency >= 1,           "hardwareConcurrency >= 1");
});

testJS("global.crypto (Web Crypto sanity)", function () {
    must(typeof crypto === 'object',                  "crypto is object");
    must(typeof crypto.subtle === 'object',           "crypto.subtle");
    must(typeof crypto.getRandomValues === 'function', "getRandomValues");
    must(typeof crypto.randomUUID === 'function',      "randomUUID");
    var u = crypto.randomUUID();
    must(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(u),
         "randomUUID v4 format");
    var arr = new Uint8Array(8);
    crypto.getRandomValues(arr);
    must(Array.prototype.some.call(arr, function (v) { return v !== 0; }),
         "getRandomValues fills");
});


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
    /* stream() throws NotSupportedError on rampart (no stream/web yet);
       node returns a real ReadableStream — gate the assertion. */
    if (_isRampart)
        mustThrow(function(){ b.stream(); }, "rampart: Blob.stream() throws NotSupportedError");
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


/* ---------- async tail ----------
   testJS is sync-only by design; some WHATWG bits return Promises or
   schedule timers.  Use a counter — each async test increments
   _pendingAsync at start, calls _doneAsync() when its async work
   resolves; when counter hits 0 we testJS.exit(). */
var _pendingAsync = 0;
var _exitOnce = false;
function _doneAsync() {
    if (--_pendingAsync <= 0 && !_exitOnce) {
        _exitOnce = true;
        testJS.exit();
    }
}
function _asyncFail(label, e) {
    if (typeof printf === 'function')
        printf("  %s: async assertion failed: %s\n", label, (e && e.message) || String(e));
    else if (typeof console !== 'undefined') console.error(label, e);
    try { if (typeof process !== 'undefined' && process.exit) process.exit(1); } catch (_) {}
}

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

testJS("MessageChannel postMessage (async)", function () {
    _pendingAsync++;
    var mc = new MessageChannel();
    var got = null;
    mc.port2.on('message', function (m) { got = m; });
    mc.port1.postMessage('mc-ping');
    setTimeout(function () {
        try { mustEq(got, 'mc-ping', "port2 received port1's postMessage"); }
        catch (e) { _asyncFail("MessageChannel", e); }
        mc.port1.close(); mc.port2.close();
        _doneAsync();
    }, 50);
    return true;
});

testJS("AbortSignal.timeout (async)", function () {
    _pendingAsync++;
    var sig = AbortSignal.timeout(20);
    mustEq(sig.aborted, false, "timeout signal not yet aborted");
    var fired = false;
    sig.addEventListener('abort', function () { fired = true; });
    setTimeout(function () {
        try {
            mustEq(sig.aborted, true,           "timeout signal aborted");
            mustEq(fired,       true,           "timeout listener fired");
            must(sig.reason && sig.reason.name === 'TimeoutError',
                 "timeout reason is TimeoutError");
        } catch (e) { _asyncFail("AbortSignal.timeout", e); }
        _doneAsync();
    }, 100);
    return true;
});

/* Safety net: if scheduling somehow doesn't fire, exit after a tick. */
setTimeout(function () {
    if (!_exitOnce) {
        _exitOnce = true;
        testJS.exit();
    }
}, 500);
