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


testJS.exit();
