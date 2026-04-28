/* This file runs under both rampart and node:
 *     rampart map-test.js
 *     node    map-test.js
 * Under rampart it additionally tests cross-thread Map/Set copy.
 */

var IS_RAMPART = (typeof global !== "undefined") && !!global.rampart;

var _write;
if (IS_RAMPART) {
    rampart.globalize(rampart.utils);
    _write = function(s) { printf("%s", s); fflush(stdout); };
} else {
    _write = function(s) { process.stdout.write(s); };
}

function _pad(s, n) {
    s = String(s);
    while (s.length < n) s += " ";
    return s;
}

var _nfailed = 0;

function testFeature(name, test)
{
    var error=false;
    _write("testing " + _pad(name, 60) + " - ");
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    if(test)
        _write("passed\n");
    else
    {
        _write(">>>>> FAILED <<<<<\n");
        _nfailed++;
    }
    if(error) console.log(error);
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


/* ================================================================
   Test suite — runs once in main, once in thread
   ================================================================ */

function runTests(label) {

    _write("\n=== " + label + " ===\n\n");

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
}


function _finish() {
    _write("\n");
    if (_nfailed)
        _write(_nfailed + " test(s) FAILED\n");
    else
        _write("All tests passed.\n");
    process.exit(_nfailed ? 1 : 0);
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
        runTests("Child thread");
    }, null, function(result, err) {
        if (err)
            console.log("Thread error:", err);
        _finish();
    });
} else {
    _finish();
}
