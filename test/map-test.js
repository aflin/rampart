/* This file runs under both rampart and node:
 *     rampart map-test.js
 *     node    map-test.js
 * Under rampart it additionally tests cross-thread Map/Set copy.
 */

/* `testFeature`'s closure won't survive `.exec()` into a child thread
   (rampart copies globals, not closures), so we publish a recipe and
   rebuild testFeature on demand if missing.  The worker re-requires
   the harness on first use. */
var _tfRecipe = {prefix: "map", allowNode: true};
function _makeTF() {
    return new (require('./test-feature.js'))(_tfRecipe);
}
var testFeature = _makeTF();
var IS_RAMPART = testFeature.isRampart;
if (IS_RAMPART) {
    /* Make the recipe + factory visible to child threads. */
    global._tfRecipe = _tfRecipe;
    global._makeTF   = _makeTF;
}


/* ================================================================
   Global Map and Set for thread copy testing
   ================================================================ */

var gMap = new Map([
    ["name", "Alice"],
    ["age", 30],
    ["scores", [10, 20, 30]]
]);

var gObjKey = {id: 42};
var gMapObj = new Map();
gMapObj.set(gObjKey, "object-keyed value");
gMapObj.set("fallback", "string value");

var gSet = new Set([10, 20, 30, 40, 50]);

var gSetMixed = new Set();
gSetMixed.add("hello");
gSetMixed.add(42);
gSetMixed.add(true);
gSetMixed.add(null);

var gMapChain = new Map();
gMapChain.set("a", 1).set("b", 2).set("c", 3).set("d", 4).set("e", 5);

/* Map and Set with plain own properties set via direct assignment.
   These bypass .set/.add and live as regular properties on the
   instance (not in the internal map_store).  The thread copy must
   preserve them. */
var gMapWithProps = new Map();
gMapWithProps.set("entryKey", "entryVal");
gMapWithProps.plainProp     = "plain-string";
gMapWithProps.numProp       = 12345;
gMapWithProps.deepProp      = {nested: {deeper: [7, 8, 9]}};
gMapWithProps[7]            = "indexed-prop";
gMapWithProps["[object Object]"] = "object-stringified-key";

var gSetWithProps = new Set();
gSetWithProps.add("a"); gSetWithProps.add("b");
gSetWithProps.label   = "labeled-set";
gSetWithProps.payload = {flag: true};
gSetWithProps[0]      = "indexed-set-prop";

/* RegExps — top-level, nested in object, and nested inside a Map.
   The thread-copy code must rebuild each as a real RegExp via
   `new RegExp(source, flags)` in the target context. */
var gRegExp        = /foo/gi;
gRegExp.lastIndex  = 4;
gRegExp.tag        = "labelled";
var gRegExpPlain   = /bar/m;
var gRegExpNested  = { r: /^baz$/, list: [/a/g, /b/i] };
var gRegExpInMap   = new Map([["pat", /xyz/g]]);

/* Errors — base, subclass, with own props, thrown (has stack), nested. */
var gError         = new Error("topE");
gError.code        = "EBAD";
var gTypeError     = new TypeError("type error");
var gRangeError    = new RangeError("range error");
var gErrorThrown;
try { throw new SyntaxError("syntax bad"); } catch (e) { gErrorThrown = e; }
var gErrorNested   = { err: new Error("nested"), list: [new TypeError("a"), new RangeError("b")] };
var gErrorInMap    = new Map([["e", new Error("inMap")]]);

/* Errors — base, subclass, with own props, thrown (has stack), nested. */
var gError         = new Error("topE");
gError.code        = "EBAD";
var gTypeError     = new TypeError("type error");
var gRangeError    = new RangeError("range error");
var gErrorThrown;
try { throw new SyntaxError("syntax bad"); } catch (e) { gErrorThrown = e; }
var gErrorNested   = { err: new Error("nested"), list: [new TypeError("a"), new RangeError("b")] };
var gErrorInMap    = new Map([["e", new Error("inMap")]]);


/* ================================================================
   Test suite — runs once in main, once in thread
   ================================================================ */

function runTests(label) {

    if (typeof printf !== 'undefined')
        printf("\n=== %s ===\n\n", label);
    else
        process.stdout.write("\n=== " + label + " ===\n\n");

    /* ---- Map basics ---- */

    testFeature(label + " - Map constructor from entries", function() {
        return gMap.size === 3;
    });

    testFeature(label + " - Map.get string key", function() {
        return gMap.get("name") === "Alice";
    });

    testFeature(label + " - Map.get number key", function() {
        return gMap.get("age") === 30;
    });

    testFeature(label + " - Map.get array value", function() {
        var s = gMap.get("scores");
        return Array.isArray(s) && s.length === 3 && s[0] === 10;
    });

    testFeature(label + " - Map.has", function() {
        return gMap.has("name") && gMap.has("age") && !gMap.has("missing");
    });

    /* ---- Map with object key ---- */

    testFeature(label + " - Map object key size", function() {
        return gMapObj.size === 2;
    });

    testFeature(label + " - Map.get string key in mixed map", function() {
        return gMapObj.get("fallback") === "string value";
    });

    /* Object key only works in the creating process (pointer-based).
       In a thread, the object is a copy with a different pointer. */

    /* ---- Map chaining ---- */

    testFeature(label + " - Map chained set", function() {
        return gMapChain.size === 5;
    });

    testFeature(label + " - Map chained values", function() {
        return gMapChain.get("a") === 1
            && gMapChain.get("c") === 3
            && gMapChain.get("e") === 5;
    });

    /* ---- Map iteration ---- */

    testFeature(label + " - Map.forEach order", function() {
        var keys = [];
        gMap.forEach(function(val, key) {
            keys.push(key);
        });
        return JSON.stringify(keys) === '["name","age","scores"]';
    });

    testFeature(label + " - Map.forEach values", function() {
        var vals = [];
        gMapChain.forEach(function(val) {
            vals.push(val);
        });
        return JSON.stringify(vals) === "[1,2,3,4,5]";
    });

    testFeature(label + " - Map.forEach thisArg", function() {
        var ctx = {sum: 0};
        gMapChain.forEach(function(val) {
            this.sum += val;
        }, ctx);
        return ctx.sum === 15;
    });

    /* ---- Map iterators ---- */

    testFeature(label + " - Array.from(map) entries", function() {
        var e = Array.from(gMap);
        return e.length === 3
            && e[0][0] === "name" && e[0][1] === "Alice"
            && e[1][0] === "age" && e[1][1] === 30;
    });

    testFeature(label + " - Array.from(map.keys())", function() {
        var k = Array.from(gMapChain.keys());
        return JSON.stringify(k) === '["a","b","c","d","e"]';
    });

    testFeature(label + " - Array.from(map.values())", function() {
        var v = Array.from(gMapChain.values());
        return JSON.stringify(v) === "[1,2,3,4,5]";
    });

    testFeature(label + " - Array.from(map.entries())", function() {
        var e = Array.from(gMapChain.entries());
        return e.length === 5 && e[2][0] === "c" && e[2][1] === 3;
    });

    /* ---- Map mutation (local copy, doesn't affect global) ---- */

    testFeature(label + " - Map set/get new key", function() {
        var m = new Map([["x", 1]]);
        m.set("y", 2);
        return m.size === 2 && m.get("y") === 2;
    });

    testFeature(label + " - Map overwrite preserves order", function() {
        var m = new Map([["a", 1], ["b", 2], ["c", 3]]);
        m.set("a", 99);
        var k = Array.from(m.keys());
        return JSON.stringify(k) === '["a","b","c"]' && m.get("a") === 99;
    });

    testFeature(label + " - Map delete", function() {
        var m = new Map([["a", 1], ["b", 2], ["c", 3]]);
        var r = m.delete("b");
        return r === true && m.size === 2 && !m.has("b")
            && JSON.stringify(Array.from(m.keys())) === '["a","c"]';
    });

    testFeature(label + " - Map delete missing returns false", function() {
        var m = new Map([["a", 1]]);
        return m.delete("z") === false;
    });

    testFeature(label + " - Map clear", function() {
        var m = new Map([["a", 1], ["b", 2]]);
        m.clear();
        return m.size === 0 && !m.has("a");
    });

    /* ---- Map special keys ---- */

    testFeature(label + " - Map NaN key", function() {
        var m = new Map();
        m.set(NaN, "nan value");
        return m.get(NaN) === "nan value" && m.has(NaN);
    });

    testFeature(label + " - Map -0 equals +0", function() {
        var m = new Map();
        m.set(0, "zero");
        m.set(-0, "neg zero");
        return m.size === 1 && m.get(0) === "neg zero";
    });

    testFeature(label + " - Map null/undefined keys", function() {
        var m = new Map();
        m.set(null, "null val");
        m.set(undefined, "undef val");
        return m.get(null) === "null val"
            && m.get(undefined) === "undef val"
            && m.size === 2;
    });

    testFeature(label + " - Map boolean keys", function() {
        var m = new Map();
        m.set(true, "yes");
        m.set(false, "no");
        return m.get(true) === "yes" && m.get(false) === "no" && m.size === 2;
    });

    testFeature(label + " - Map number keys precision", function() {
        var m = new Map();
        m.set(0.1 + 0.2, "float");
        return m.get(0.30000000000000004) === "float";
    });

    testFeature(label + " - Map Infinity keys", function() {
        var m = new Map();
        m.set(Infinity, "inf");
        m.set(-Infinity, "ninf");
        return m.get(Infinity) === "inf" && m.get(-Infinity) === "ninf" && m.size === 2;
    });

    testFeature(label + " - Map object key identity", function() {
        var m = new Map();
        var o1 = {a: 1}, o2 = {a: 1};
        m.set(o1, "first");
        m.set(o2, "second");
        return m.size === 2 && m.get(o1) === "first" && m.get(o2) === "second";
    });

    /* ---- Set basics ---- */

    testFeature(label + " - Set constructor dedup", function() {
        return gSet.size === 5;
    });

    testFeature(label + " - Set.has", function() {
        return gSet.has(10) && gSet.has(50) && !gSet.has(99);
    });

    testFeature(label + " - Set mixed types", function() {
        return gSetMixed.size === 4
            && gSetMixed.has("hello")
            && gSetMixed.has(42)
            && gSetMixed.has(true)
            && gSetMixed.has(null);
    });

    /* ---- Set iteration ---- */

    testFeature(label + " - Set.forEach", function() {
        var vals = [];
        gSet.forEach(function(v) { vals.push(v); });
        return JSON.stringify(vals) === "[10,20,30,40,50]";
    });

    testFeature(label + " - Set.forEach thisArg", function() {
        var ctx = {sum: 0};
        gSet.forEach(function(v) { this.sum += v; }, ctx);
        return ctx.sum === 150;
    });

    testFeature(label + " - Array.from(set)", function() {
        return JSON.stringify(Array.from(gSet)) === "[10,20,30,40,50]";
    });

    testFeature(label + " - Array.from(set.values())", function() {
        return JSON.stringify(Array.from(gSet.values())) === "[10,20,30,40,50]";
    });

    testFeature(label + " - Array.from(set.keys())", function() {
        /* Set.keys() === Set.values() per spec */
        return JSON.stringify(Array.from(gSet.keys())) === "[10,20,30,40,50]";
    });

    /* ---- Set mutation ---- */

    testFeature(label + " - Set add + dedup", function() {
        var s = new Set([1, 2, 3]);
        s.add(2).add(4);
        return s.size === 4 && s.has(4);
    });

    testFeature(label + " - Set delete", function() {
        var s = new Set([1, 2, 3]);
        var r = s.delete(2);
        return r === true && s.size === 2 && !s.has(2);
    });

    testFeature(label + " - Set delete missing", function() {
        var s = new Set([1]);
        return s.delete(99) === false;
    });

    testFeature(label + " - Set clear", function() {
        var s = new Set([1, 2, 3]);
        s.clear();
        return s.size === 0;
    });

    testFeature(label + " - Set NaN dedup", function() {
        var s = new Set([NaN, NaN, NaN]);
        return s.size === 1 && s.has(NaN);
    });

    testFeature(label + " - Set object identity", function() {
        var o1 = {x: 1}, o2 = {x: 1};
        var s = new Set([o1, o2, o1]);
        return s.size === 2;
    });

    /* ---- Set.entries() yields [value, value] pairs (not [value, true]) ---- */

    testFeature(label + " - Set.entries() yields [v,v] pairs", function() {
        var s = new Set(["x", "y", "z"]);
        var e = Array.from(s.entries());
        return e.length === 3
            && e[0][0] === "x" && e[0][1] === "x"
            && e[1][0] === "y" && e[1][1] === "y"
            && e[2][0] === "z" && e[2][1] === "z";
    });

    testFeature(label + " - Set.entries() iterator next()", function() {
        var s = new Set([1, 2]);
        var it = s.entries();
        var n1 = it.next();
        var n2 = it.next();
        var n3 = it.next();
        return !n1.done && n1.value[0] === 1 && n1.value[1] === 1
            && !n2.done && n2.value[0] === 2 && n2.value[1] === 2
            &&  n3.done;
    });

    /* ---- Plain own properties survive thread copy ---- */

    testFeature(label + " - Map preserves plain own properties", function() {
        return gMapWithProps.plainProp === "plain-string"
            && gMapWithProps.numProp   === 12345
            && gMapWithProps[7]        === "indexed-prop"
            && gMapWithProps["[object Object]"] === "object-stringified-key";
    });

    testFeature(label + " - Map preserves nested object property", function() {
        var d = gMapWithProps.deepProp;
        return d && d.nested && Array.isArray(d.nested.deeper)
            && d.nested.deeper[0] === 7
            && d.nested.deeper[2] === 9;
    });

    testFeature(label + " - Map entries unaffected by plain props", function() {
        return gMapWithProps.size === 1
            && gMapWithProps.get("entryKey") === "entryVal";
    });

    testFeature(label + " - Set preserves plain own properties", function() {
        return gSetWithProps.label === "labeled-set"
            && gSetWithProps.payload && gSetWithProps.payload.flag === true
            && gSetWithProps[0] === "indexed-set-prop";
    });

    testFeature(label + " - Set entries unaffected by plain props", function() {
        return gSetWithProps.size === 2
            && gSetWithProps.has("a")
            && gSetWithProps.has("b");
    });

    /* ---- RegExp (verifies cross-thread copy in the Child pass) ---- */

    testFeature(label + " - RegExp keeps prototype", function() {
        return gRegExp instanceof RegExp
            && typeof gRegExp.test === "function";
    });

    testFeature(label + " - RegExp source + flags preserved", function() {
        return gRegExp.source === "foo"
            && gRegExp.flags  === "gi"
            && gRegExpPlain.source === "bar"
            && gRegExpPlain.flags  === "m";
    });

    testFeature(label + " - RegExp lastIndex preserved", function() {
        return gRegExp.lastIndex === 4;
    });

    testFeature(label + " - RegExp user-added own prop preserved", function() {
        return gRegExp.tag === "labelled";
    });

    testFeature(label + " - RegExp .test() works", function() {
        var r = /^hello/i;
        return r.test("Hello world") === true
            && r.test("nope")        === false;
    });

    testFeature(label + " - RegExp nested in object", function() {
        return gRegExpNested.r instanceof RegExp
            && gRegExpNested.r.source === "^baz$"
            && Array.isArray(gRegExpNested.list)
            && gRegExpNested.list[0] instanceof RegExp
            && gRegExpNested.list[0].flags === "g"
            && gRegExpNested.list[1].flags === "i";
    });

    testFeature(label + " - RegExp inside Map value", function() {
        var r = gRegExpInMap.get("pat");
        return r instanceof RegExp && r.source === "xyz" && r.flags === "g";
    });

    /* ---- Error (verifies cross-thread copy in the Child pass) ---- */

    testFeature(label + " - Error keeps prototype", function() {
        return gError instanceof Error
            && typeof gError.message === "string";
    });

    testFeature(label + " - Error message + name preserved", function() {
        return gError.message === "topE"
            && gError.name    === "Error";
    });

    testFeature(label + " - Error user-added own prop preserved", function() {
        return gError.code === "EBAD";
    });

    testFeature(label + " - TypeError subclass preserved", function() {
        return gTypeError instanceof TypeError
            && gTypeError instanceof Error
            && gTypeError.name    === "TypeError"
            && gTypeError.message === "type error";
    });

    testFeature(label + " - RangeError subclass preserved", function() {
        return gRangeError instanceof RangeError
            && gRangeError instanceof Error
            && gRangeError.name === "RangeError";
    });

    testFeature(label + " - thrown Error preserves stack", function() {
        return gErrorThrown instanceof SyntaxError
            && typeof gErrorThrown.stack === "string"
            && gErrorThrown.stack.indexOf("SyntaxError") === 0;
    });

    testFeature(label + " - Error nested in object", function() {
        return gErrorNested.err instanceof Error
            && gErrorNested.err.message === "nested"
            && Array.isArray(gErrorNested.list)
            && gErrorNested.list[0] instanceof TypeError
            && gErrorNested.list[1] instanceof RangeError;
    });

    testFeature(label + " - Error inside Map value", function() {
        var em = gErrorInMap.get("e");
        return em instanceof Error && em.message === "inMap";
    });

    /* ---- rampart.utils.deepCopy: Map/Set support (rampart only) ---- */

    if (!IS_RAMPART) return;

    var deepCopy = rampart.utils.deepCopy;

    testFeature(label + " - deepCopy: nested Map keeps prototype", function() {
        var src = { tag: "outer", m: new Map([["k", 1], ["k2", 2]]) };
        var c = deepCopy({}, src);
        return c.m instanceof Map && c.m.size === 2 && c.m.get("k") === 1;
    });

    testFeature(label + " - deepCopy: nested Set keeps prototype", function() {
        var src = { s: new Set([10, 20, 30]) };
        var c = deepCopy({}, src);
        return c.s instanceof Set && c.s.size === 3 && c.s.has(20);
    });

    testFeature(label + " - deepCopy: Map -> Map merge entries + plain props", function() {
        var m1 = new Map([["a", 1], ["b", 2]]);
        m1.tagProp = "tag";
        var m2 = deepCopy(new Map(), m1);
        return m2 instanceof Map
            && m2.size === 2
            && m2.get("a") === 1 && m2.get("b") === 2
            && m2.tagProp === "tag";
    });

    testFeature(label + " - deepCopy: Map clone is independent", function() {
        var m1 = new Map([["a", 1], ["b", 2]]);
        var m2 = deepCopy(new Map(), m1);
        m2.set("c", 3);
        m2.delete("a");
        return m1.size === 2 && m1.has("a") && !m1.has("c");
    });

    testFeature(label + " - deepCopy: object-keyed Map re-hashes correctly", function() {
        var ko = {a: 1};
        var src = new Map();
        src.set(ko, "obj-val");
        src.set("strk", "str-val");
        var c = deepCopy(new Map(), src);

        var clonedKey = null;
        c.forEach(function(v, k) {
            if (typeof k === "object" && k !== null) clonedKey = k;
        });
        return c.size === 2
            && c.get("strk") === "str-val"
            && clonedKey && clonedKey !== ko
            && c.has(clonedKey)
            && c.get(clonedKey) === "obj-val";
    });

    testFeature(label + " - deepCopy: nested object values inside Map are deep", function() {
        var inner = {n: 1, arr: [10, 20]};
        var m = new Map([["k", inner]]);
        var c = deepCopy(new Map(), m);
        inner.n = 999;
        inner.arr.push(30);
        var got = c.get("k");
        return got && got.n === 1
            && Array.isArray(got.arr)
            && got.arr.length === 2;
    });

    testFeature(label + " - deepCopy: deeply nested Map containing Set", function() {
        var src = { a: { b: new Map([["x", new Set([1, 2, 3])]]) }};
        var c = deepCopy({}, src);
        var s = c.a.b instanceof Map && c.a.b.get("x");
        return c.a.b instanceof Map
            && s instanceof Set
            && s.size === 3 && s.has(2);
    });

    testFeature(label + " - deepCopy: Set -> Set merge + plain prop", function() {
        var s1 = new Set([1, 2, 3]);
        s1.note = "labelled";
        var s2 = deepCopy(new Set(), s1);
        return s2 instanceof Set
            && s2.size === 3
            && s2.has(1) && s2.has(3)
            && s2.note === "labelled";
    });

    testFeature(label + " - deepCopy: plain target does not transmute to Map", function() {
        var m = new Map([["k", "v"]]);
        m.tag = "t";
        var c = deepCopy({}, m);
        /* Plain target stays plain.  Plain own props propagate; the Map's
           internal entries do NOT (target isn't a Map). */
        return !(c instanceof Map) && c.tag === "t"
            && typeof c.get !== "function"
            && c.size === undefined;
    });
}


function _finish() {
    testFeature.exit();
}

/* ================================================================
   Run in main (always)
   ================================================================ */

runTests(IS_RAMPART ? "Main thread" : "Node");


/* ================================================================
   Run in child thread (rampart only — globals are copied)
   ================================================================ */

if (IS_RAMPART) {
    var thr = new rampart.thread();
    thr.exec(function() {
        /* Rebuild testFeature in the child context — the parent's
           closure didn't cross the thread boundary. */
        testFeature = _makeTF();
        runTests("Child thread");
    }, null, function(result, err) {
        if (err)
            console.log("Thread error:", err);
        _finish();
    });
} else {
    _finish();
}
