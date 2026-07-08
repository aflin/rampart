/* JavaScriptCore (JSC) test for rampart-webview.
 *
 * Exercises the headless JSC interfaces the module exposes on Linux
 * (via WebKitGTK) and macOS (via the system JavaScriptCore):
 *
 *   webview.jscExec(code)   — one-shot evaluation, deep-converting the
 *                             result to a native Duktape value.
 *   new webview.JSCContext  — persistent interpreter with proxy-backed
 *                             objects, set()/getGlobal(), .toValue(), etc.
 *
 * The focus is the JSC -> Duktape translation layer: every rich type in
 * the documented mapping (Date, RegExp, Error, TypedArray, ArrayBuffer,
 * Map, Set, nested objects/arrays, Function) is checked round-trip.
 * Map and Set in particular convert to genuine Duktape `Map`/`Set`
 * objects (entries/values converted recursively), not arrays.
 *
 * Modeled after rampart's other test/*-test.js files using
 * test-feature.js as the harness; each group bundles many assertions
 * and reports a single pass/fail.
 *
 * Self-contained: no external library files are loaded (the project's
 * own webview-jsc-test.js additionally exercises lodash/marked/ajv/etc.
 * from a libs/ directory; that is intentionally omitted here).
 *
 * Rampart-only, and the webview module is optional — if it (or its
 * WebKit/JSC backend) can't be loaded the suite skips cleanly rather
 * than failing run_tests.sh.
 */

var testFeature = new (require('./test-feature.js'))({
    prefix: "webview-jsc"
});
var must         = testFeature.must;
var mustEq       = testFeature.mustEq;
var mustThrow    = testFeature.mustThrow;
var mustContain  = testFeature.mustContain;

rampart.globalize(rampart.utils);

/* JSC is not available on Windows builds (WebView GUI works there, but
   not jscExec/JSCContext).  Skip the whole suite. */
if (rampart.buildPlatform.indexOf("windows") !== -1 ||
    rampart.buildPlatform.indexOf("MSYS") !== -1) {
    testFeature.skip("webview JSC suite", "no JavaScriptCore on Windows");
    testFeature.exit();
}

/* rampart-webview needs a WebKitGTK / system-JSC backend present.  If
   the require fails, skip rather than fail the run. */
var wv;
try { wv = require("rampart-webview"); }
catch (e) {
    fprintf(stderr,
        "Could not load rampart-webview: %s\nSKIPPING WEBVIEW JSC TESTS\n",
        e.message);
    testFeature.exit();
}

/* A WebView-only build (no JSC) would lack jscExec; skip if so. */
if (typeof wv.jscExec !== "function") {
    testFeature.skip("webview JSC suite", "module built without JSC");
    testFeature.exit();
}

/* ============================================================
   1. jscExec — primitives and numeric edge cases
   ============================================================ */

testFeature("jscExec: primitives + numeric edge cases", function() {
    mustEq(wv.jscExec("40 + 2"),            42,       "number");
    mustEq(wv.jscExec("'a' + 'b'"),         "ab",     "string");
    mustEq(wv.jscExec("true"),              true,     "boolean true");
    mustEq(wv.jscExec("false"),             false,    "boolean false");
    mustEq(wv.jscExec("null"),              null,     "null");
    mustEq(wv.jscExec("undefined"),         undefined,"undefined");
    must(isNaN(wv.jscExec("NaN")),                    "NaN preserved");
    mustEq(wv.jscExec("1/0"),               Infinity, "Infinity");
    mustEq(wv.jscExec("-1/0"),              -Infinity,"-Infinity");
    mustEq(wv.jscExec("Math.PI"),           Math.PI,  "double precision");
});

/* ============================================================
   2. jscExec — Date / RegExp / Error rich types
   ============================================================ */

testFeature("jscExec: Date / RegExp / Error", function() {
    var d = wv.jscExec("new Date('2026-04-15T12:30:00Z')");
    must(d instanceof Date,                          "Date instanceof");
    mustEq(d.getTime(), Date.parse("2026-04-15T12:30:00Z"),
                                                     "Date timestamp preserved");

    var re = wv.jscExec("/^hello$/gi");
    must(re instanceof RegExp,                       "RegExp instanceof");
    mustEq(re.source, "^hello$",                     "RegExp source preserved");
    must(re.global && re.ignoreCase,                 "RegExp flags preserved");
    must(re.test("HELLO"),                           "RegExp matches");

    var err = wv.jscExec("new TypeError('boom')");
    must(err instanceof Error,                       "Error instanceof");
    mustEq(err.name, "TypeError",                    "Error name preserved");
    mustContain(err.message, "boom",                 "Error message preserved");
});

/* ============================================================
   3. jscExec — TypedArray + ArrayBuffer -> Buffer
   ============================================================ */

testFeature("jscExec: TypedArray + ArrayBuffer", function() {
    var u8 = wv.jscExec("new Uint8Array([10,20,30])");
    must(u8 instanceof Uint8Array,                   "Uint8Array instanceof");
    mustEq(u8.length, 3,                             "Uint8Array length");
    mustEq(u8[0], 10,                                "Uint8Array[0]");
    mustEq(u8[2], 30,                                "Uint8Array[2]");

    var i32 = wv.jscExec("new Int32Array([-5, 1000000])");
    must(i32 instanceof Int32Array,                  "Int32Array instanceof");
    mustEq(i32[0], -5,                               "Int32Array signed value");
    mustEq(i32[1], 1000000,                          "Int32Array large value");

    var f64 = wv.jscExec("new Float64Array([1.5, 2.5])");
    must(f64 instanceof Float64Array,                "Float64Array instanceof");
    mustEq(f64[1], 2.5,                              "Float64Array value");

    var buf = wv.jscExec("new ArrayBuffer(8)");
    must(Buffer.isBuffer(buf),                       "ArrayBuffer => Buffer");
    mustEq(buf.length, 8,                            "Buffer length");
});

/* ============================================================
   4. jscExec — Map => Duktape Map (recursive conversion)
   ============================================================ */

testFeature("jscExec: Map => Duktape Map (recursive)", function() {
    var m = wv.jscExec("new Map([['a',1],['b',2]])");
    must(m instanceof Map,                           "Map instanceof");
    mustEq(m.size, 2,                                "Map size");
    mustEq(m.get("a"), 1,                            "Map get('a')");
    mustEq(m.get("b"), 2,                            "Map get('b')");

    /* values converted recursively */
    var md = wv.jscExec("new Map([['d', new Date(0)], ['arr', [1,2,3]]])");
    must(md.get("d") instanceof Date,                "Map value: Date preserved");
    mustEq(md.get("d").getTime(), 0,                 "Map value: Date timestamp");
    mustEq(md.get("arr")[2], 3,                      "Map value: array preserved");

    /* keys converted recursively (object identity key retained) */
    var mk = wv.jscExec("var k={id:7}; new Map([[k,'v']])");
    mustEq(mk.size, 1,                               "Map object-key size");
    mustEq(Array.from(mk.keys())[0].id, 7,           "Map object key preserved");

    /* nested Map */
    var mm = wv.jscExec("new Map([['inner', new Map([['x',9]])]])");
    must(mm.get("inner") instanceof Map,             "nested Map preserved");
    mustEq(mm.get("inner").get("x"), 9,              "nested Map value");
});

/* ============================================================
   5. jscExec — Set => Duktape Set (recursive conversion)
   ============================================================ */

testFeature("jscExec: Set => Duktape Set (recursive)", function() {
    var s = wv.jscExec("new Set([10,20,30,10])");
    must(s instanceof Set,                           "Set instanceof");
    mustEq(s.size, 3,                                "Set dedup size");
    must(s.has(10) && s.has(30),                     "Set has members");
    must(!s.has(99),                                 "Set !has non-member");

    /* values converted recursively */
    var so = wv.jscExec("new Set([{a:1},{a:2}])");
    mustEq(so.size, 2,                               "Set of objects size");
    mustEq(Array.from(so)[0].a, 1,                   "Set object value preserved");

    var sd = wv.jscExec("new Set([new Date(0)])");
    must(Array.from(sd)[0] instanceof Date,          "Set Date value preserved");
});

/* ============================================================
   6. jscExec — nested objects / arrays / mixed rich types
   ============================================================ */

testFeature("jscExec: nested objects + arrays + mixed", function() {
    var o = wv.jscExec("({a:1, b:{c:[2,3]}})");
    mustEq(o.a, 1,                                   "object scalar");
    mustEq(o.b.c[0], 2,                              "nested array elem");
    mustEq(o.b.c[1], 3,                              "nested array elem 2");

    var arr = wv.jscExec("[1, 'two', true, null, {x:9}]");
    mustEq(arr.length, 5,                            "array length");
    mustEq(arr[1], "two",                            "array string elem");
    mustEq(arr[4].x, 9,                              "object inside array");

    /* Map and Set nested inside a plain object */
    var mix = wv.jscExec("({m:new Map([['k',1]]), s:new Set([7,8])})");
    must(mix.m instanceof Map,                       "Map nested in object");
    must(mix.s instanceof Set,                       "Set nested in object");
    mustEq(mix.m.get("k"), 1,                        "nested Map value");
    mustEq(mix.s.size, 2,                            "nested Set size");
});

/* ============================================================
   7. jscExec — Function returns its source text
   ============================================================ */

testFeature("jscExec: Function => source text", function() {
    var fn = wv.jscExec("(function add(a,b){return a+b;})");
    mustEq(typeof fn, "string",                      "function returned as string");
    mustContain(fn, "return a+b",                    "function source preserved");
});

/* ============================================================
   8. jscExec — exception + syntax-error propagation
   ============================================================ */

testFeature("jscExec: exception + syntax-error propagation", function() {
    var msg = null;
    try { wv.jscExec("throw new TypeError('boom')"); }
    catch (e) { msg = e.message; }
    must(msg !== null,                               "thrown error propagates");
    mustContain(msg, "boom",                         "thrown message preserved");

    var smsg = null;
    try { wv.jscExec("var = 5"); }
    catch (e) { smsg = e.message; }
    must(smsg !== null,                              "syntax error throws");
    mustContain(smsg, "SyntaxError",                 "syntax error reported");
});

/* ============================================================
   9. jscExec — modern JS (ES2020+) evaluates
   ============================================================ */

testFeature("jscExec: modern JS (ES2020+) evaluates", function() {
    mustEq(wv.jscExec("[1,2,3].map(x => x*2).reduce((a,b)=>a+b,0)"),
                                            12,      "arrow + reduce");
    mustEq(wv.jscExec("`a${1+1}b`"),        "a2b",   "template literal");
    mustEq(wv.jscExec("({a:{b:5}})?.a?.b ?? 0"),
                                            5,       "optional chaining + nullish");
    mustEq(wv.jscExec("[...new Set([1,1,2,3])].join(',')"),
                                            "1,2,3", "spread of Set");
    mustEq(wv.jscExec("(function(){let s=0; for (const n of [1,2,3]) s+=n; return s;})()"),
                                            6,       "let/const/for-of");
});

/* ============================================================
   10. JSCContext — eval + persistent state
   ============================================================ */

testFeature("JSCContext: eval + persistent state", function() {
    var jsc = new wv.JSCContext();
    mustEq(jsc.eval("2 + 2"), 4,                     "eval returns value");
    jsc.eval("var counter = 0");
    jsc.eval("counter += 10");
    jsc.eval("counter += 32");
    mustEq(jsc.eval("counter"), 42,                  "state persists across eval");
    jsc.destroy();
});

/* ============================================================
   11. JSCContext — set(): global / object / buffer / date
   ============================================================ */

testFeature("JSCContext: set (global/object/buffer/date)", function() {
    var jsc = new wv.JSCContext();
    jsc.set("greeting", "hello");
    mustEq(jsc.eval("greeting"), "hello",            "set string");
    jsc.set("data", {x:1, y:[2,3]});
    mustEq(jsc.eval("data.x + data.y[1]"), 4,        "set object");
    jsc.set("buf", new Buffer("ABC"));
    mustEq(jsc.eval("new Uint8Array(buf)[0]"), 65,   "set buffer (byte 'A')");
    jsc.set("d", new Date("2026-01-01T00:00:00Z"));
    mustEq(jsc.eval("d.getUTCFullYear()"), 2026,     "set date");
    jsc.destroy();
});

/* ============================================================
   12. JSCContext — getGlobal + proxy access/method/nested
   ============================================================ */

testFeature("JSCContext: getGlobal + proxy access", function() {
    var jsc = new wv.JSCContext();
    jsc.eval("var gObj = {a:1, b:2}");
    var o = jsc.getGlobal("gObj");
    mustEq(o.a, 1,                                   "proxy property a");
    mustEq(o.b, 2,                                   "proxy property b");

    jsc.eval("var obj = {add: function(a,b){return a+b;}}");
    mustEq(jsc.getGlobal("obj").add(3,4), 7,         "proxy method call");

    jsc.eval("var deep = {a:{b:{c:99}}}");
    mustEq(jsc.getGlobal("deep").a.b.c, 99,          "proxy nested access");
    jsc.destroy();
});

/* ============================================================
   13. JSCContext — .toValue() / .toString()
   ============================================================ */

testFeature("JSCContext: .toValue() / .toString()", function() {
    var jsc = new wv.JSCContext();
    jsc.eval("var tv = {x:[1,2,3], y:true}");
    mustEq(JSON.stringify(jsc.getGlobal("tv").toValue()),
           '{"x":[1,2,3],"y":true}',                "toValue plain object");
    jsc.eval("var ts = {a:1}");
    mustEq(jsc.getGlobal("ts").toString(), "[object Object]",
                                                     "toString");
    jsc.destroy();
});

/* ============================================================
   14. JSCContext — pass a JSC object back into JSC
   ============================================================ */

testFeature("JSCContext: pass JSC object back into JSC", function() {
    var jsc = new wv.JSCContext();
    jsc.eval("function mkObj(n){return {val:n};}");
    jsc.eval("function rdObj(o){return o.val*2;}");
    var o = jsc.getGlobal("mkObj")(21);
    mustEq(jsc.getGlobal("rdObj")(o), 42,            "JSC arg unwrapped automatically");
    jsc.destroy();
});

/* ============================================================
   15. JSCContext — rich-type returns (Date / RegExp / TypedArray)
   ============================================================ */

testFeature("JSCContext: rich-type returns", function() {
    var jsc = new wv.JSCContext();
    var d = jsc.eval("new Date('2026-06-15')");
    must(d instanceof Date && d.getFullYear() === 2026, "Date return");
    var re = jsc.eval("/test/i");
    must(re instanceof RegExp && re.test("TEST"),       "RegExp return");
    var a = jsc.eval("new Float64Array([1.5, 2.5])");
    must(a[0] === 1.5 && a[1] === 2.5,                  "TypedArray return");
    jsc.destroy();
});

/* ============================================================
   16. JSCContext — exceptions (eval + call) + state survives
   ============================================================ */

testFeature("JSCContext: exceptions + state survives", function() {
    var jsc = new wv.JSCContext();
    jsc.eval("var keep = 7");

    var em = null;
    try { jsc.eval("throw new Error('eval boom')"); } catch (e) { em = e.message; }
    mustContain(em, "eval boom",                     "eval exception propagates");

    jsc.eval("function thrower(){ throw new Error('call boom'); }");
    var cm = null;
    try { jsc.getGlobal("thrower")(); } catch (e) { cm = e.message; }
    mustContain(cm, "call boom",                     "call exception propagates");

    mustEq(jsc.eval("keep"), 7,                      "state survives exceptions");
    jsc.destroy();
});

/* ============================================================
   17. JSCContext — private fields / async-await / fn-object props
   ============================================================ */

testFeature("JSCContext: classes / async-await / fn props", function() {
    var jsc = new wv.JSCContext();

    jsc.eval("class Animal { #name; constructor(n){this.#name=n;} get name(){return this.#name;} }");
    jsc.eval("var a = new Animal('Rex')");
    mustEq(jsc.getGlobal("a").name, "Rex",           "private class field");

    jsc.eval("async function aAdd(a,b){ return a+b; }");
    jsc.eval("var aResult; aAdd(3,4).then(function(v){ aResult=v; })");
    mustEq(jsc.eval("aResult"), 7,                   "async/await microtask drained");

    jsc.eval("function myFunc(){ return 1; }");
    jsc.eval("myFunc.extra = 42");
    var f = jsc.getGlobal("myFunc");
    must(f() === 1 && f.extra === 42,                "function-object property");
    jsc.destroy();
});

/* ============================================================
   18. JSCContext — destroy invalidates the context
   ============================================================ */

testFeature("JSCContext: destroy invalidates context", function() {
    var jsc = new wv.JSCContext();
    mustEq(jsc.eval("1 + 1"), 2,                     "works before destroy");
    jsc.destroy();
    mustThrow(function(){ jsc.eval("1 + 1"); },      "eval after destroy throws");
});

testFeature.exit();
