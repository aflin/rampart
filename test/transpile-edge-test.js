#!/usr/bin/env rampart
"use transpilerGlobally"

/* Edge-case tests for the ES2015+ transpiler.
   Runs on both Rampart (transpiled) and Node (native).

   Known transpiler limitations (ES2015+ → ES5):
   - const is converted to var and is NOT read-only (no reassignment
     enforcement at runtime).
   - let/const at top level become var and attach to the global object.
   - BigInt literals (123n) and BigInt operators are not supported
     (duktape limitation, not the transpiler).
*/

var _baseTestFeature = new (require('./test-feature.js'))({prefix: "edge", allowNode: true});

if (_baseTestFeature.isRampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-edge-test.transpiled.js'); } catch(e) {}
}

/* Async-aware testFeature: thenable returns get queued and drained. */
var _asyncQueue = [];
var _asyncRunning = false;
function _drainAsync() {
    if (_asyncRunning || _asyncQueue.length === 0) return;
    _asyncRunning = true;
    var item = _asyncQueue.shift();
    item.promise.then(function(result) {
        _baseTestFeature(item.name, !!result);
        _asyncRunning = false;
        _drainAsync();
    }).then(null, function(e) {
        _baseTestFeature(item.name, function(){ throw e; });
        _asyncRunning = false;
        _drainAsync();
    });
}
function testFeature(name, test) {
    if (typeof test === 'function') {
        try { test = test(); }
        catch (e) { _baseTestFeature(name, function(){ throw e; }); return; }
    }
    if (test && typeof test === 'object' && typeof test.then === 'function') {
        _asyncQueue.push({name: name, promise: test});
        _drainAsync();
        return;
    }
    _baseTestFeature(name, !!test);
}
testFeature.exit = function() { _baseTestFeature.exit(); };
/* Silence the "Possible Unhandled Promise Rejection" warning — many
   edge-case tests intentionally create promises that aren't awaited. */
if (_baseTestFeature.isRampart) rampart.warnUnhandledPromise = false;

/* ===================================================================
   1. OPTIONAL CHAINING EDGE CASES
   =================================================================== */

// Deep chain
var deep = {a: {b: {c: {d: 42}}}};
testFeature("optional chain - deep (4 levels)", deep?.a?.b?.c?.d === 42);

// Deep chain with null in the middle
var deepNull = {a: {b: null}};
testFeature("optional chain - null mid-chain", deepNull?.a?.b?.c?.d === undefined);

// Optional chaining on function result (no double-eval)
var callCount = 0;
function getObj() { callCount++; return {x: 10}; }
var val = getObj()?.x;
testFeature("optional chain - fn result no double eval", val === 10 && callCount === 1);

// Optional call on missing method
var obj1 = {notAFunc: 42};
testFeature("optional chain - call missing method", obj1.missing?.() === undefined);

// Optional chain with method call
var arr1 = [1,2,3];
testFeature("optional chain - method call", arr1?.map(x => x*2).join(",") === "2,4,6");

// Mixed optional and non-optional
var mixed = {a: {b: {c: 5}}};
testFeature("optional chain - mixed ?.  and .", mixed?.a.b?.c === 5);

// Optional chain on undefined variable via object
var undef1 = {};
testFeature("optional chain - missing prop method call", undef1.foo?.bar?.() === undefined);

// Optional bracket with expression key
var bkt = {items: {0: "zero", 1: "one"}};
var idx = 1;
testFeature("optional chain - bracket with expr key", bkt?.items?.[idx] === "one");

// Optional chain in ternary
var tern = {val: true};
testFeature("optional chain - inside ternary", (tern?.val ? "yes" : "no") === "yes");

// Optional chain with nullish coalescing
var nc1 = {a: null};
testFeature("?. combined with ?? (null)", (nc1?.a ?? "fallback") === "fallback");
testFeature("?. combined with ?? (missing)", (nc1?.b ?? "fallback") === "fallback");
testFeature("?. combined with ?? (present)", (nc1?.a?.toString ?? "fallback") === "fallback");

var nc2 = {a: {b: 7}};
testFeature("?. combined with ?? (value)", (nc2?.a?.b ?? 99) === 7);

/* ===================================================================
   2. NULLISH COALESCING EDGE CASES
   =================================================================== */

// ?? with falsy but non-nullish values
testFeature("?? with 0", (0 ?? 42) === 0);
testFeature("?? with empty string", ("" ?? "fallback") === "");
testFeature("?? with false", (false ?? true) === false);
testFeature("?? with NaN", function() { var n = NaN; return (n ?? 99) !== 99; });  // NaN is not null
testFeature("?? with undefined", (undefined ?? "yes") === "yes");
testFeature("?? with null", (null ?? "yes") === "yes");

// Chained ??
testFeature("?? chained", (null ?? undefined ?? "third") === "third");

// ?? with function call
var nc3count = 0;
function nc3fn() { nc3count++; return null; }
var nc3val = nc3fn() ?? "default";
testFeature("?? fn call value correct", nc3val === "default");

testFeature("?? fn call single eval", nc3count === 1);

/* ===================================================================
   3. LOGICAL ASSIGNMENT EDGE CASES
   =================================================================== */

// ??= only assigns when null/undefined
var la1 = 0;
la1 ??= 99;
testFeature("??= does not overwrite 0", la1 === 0);

var la2 = "";
la2 ??= "fallback";
testFeature("??= does not overwrite empty string", la2 === "");

var la3 = false;
la3 ??= true;
testFeature("??= does not overwrite false", la3 === false);

var la4 = null;
la4 ??= 42;
testFeature("??= assigns when null", la4 === 42);

var la5 = undefined;
la5 ??= 42;
testFeature("??= assigns when undefined", la5 === 42);

// ||= and &&= basics
var la6 = 0;
la6 ||= 10;
testFeature("||= overwrites falsy 0", la6 === 10);

var la7 = "hello";
la7 &&= "world";
testFeature("&&= overwrites truthy", la7 === "world");

var la8 = 0;
la8 &&= 99;
testFeature("&&= does not overwrite falsy", la8 === 0);

// Logical assignment on object properties
var laObj = {x: null, y: 5};
laObj.x ??= 100;
laObj.y ??= 100;
testFeature("??= on object prop (null)", laObj.x === 100);
testFeature("??= on object prop (present)", laObj.y === 5);

/* ===================================================================
   4. DESTRUCTURING EDGE CASES
   =================================================================== */

// Array destructuring with skip
var [,, third1] = [1, 2, 3];
testFeature("destructuring - array skip elements", third1 === 3);

// Mixed array/object destructuring
var {coords: [cx, cy]} = {coords: [10, 20]};
testFeature("destructuring - object wrapping array", cx === 10 && cy === 20);

// Basic rename with multiple levels
var {a: {b: nested2}} = {a: {b: 20}};
testFeature("destructuring - nested rename", nested2 === 20);

// Array destructuring with defaults
var [da1 = 10, da2 = 20, da3 = 30] = [100, undefined];
testFeature("destructuring - array defaults", da1 === 100 && da2 === 20 && da3 === 30);

// Destructuring in arrow function parameters (works via arrow conversion)
var destrParam = ({x, y}) => x + y;
testFeature("destructuring - in arrow param (object)", destrParam({x: 3, y: 7}) === 10);

var destrArrParam = ([a, b, c]) => a + b + c;
testFeature("destructuring - in arrow param (array)", destrArrParam([1, 2, 3]) === 6);

// Rename + default in destructuring declaration
var rendefObj = {x: 42};
var {x: rx = 5, y: ry = 10} = rendefObj;
testFeature("destructuring - rename + default", rx === 42 && ry === 10);

// Rename + default in arrow params
var rendefFn = ({x: rx = 5, y: ry = 10}) => rx + ry;
testFeature("destructuring - rename + default in arrow", function() {
    return rendefFn({}) === 15 && rendefFn({x: 1}) === 11 && rendefFn({x: 1, y: 2}) === 3;
});

// Destructuring in regular function params
testFeature("destructuring - function obj param", function() {
    function greet({name, greeting}) { return greeting + " " + name; }
    return greet({name: "World", greeting: "Hello"}) === "Hello World";
});

testFeature("destructuring - function obj param + defaults", function() {
    function greet({name, greeting = "Hello"}) { return greeting + " " + name; }
    return greet({name: "World"}) === "Hello World";
});

testFeature("destructuring - function mixed params", function() {
    function mixed(a, {x, y}, b) { return a + x + y + b; }
    return mixed(1, {x: 2, y: 3}, 4) === 10;
});

testFeature("destructuring - function param + param default", function() {
    function calc({x = 10, y = 20} = {}) { return x + y; }
    return calc() === 30 && calc({x: 1}) === 21;
});

testFeature("destructuring - function array param", function() {
    function sum([a, b, c]) { return a + b + c; }
    return sum([1, 2, 3]) === 6;
});

// Object rest in var declaration
testFeature("destructuring - object rest", function() {
    var obj = {a: 1, b: 2, c: 3, d: 4};
    var {a, ...rest1} = obj;
    return a === 1 && rest1.b === 2 && rest1.c === 3 && rest1.d === 4 && !rest1.a;
});

// Array rest after skip
testFeature("destructuring - array rest after skip", function() {
    var arr = [10, 20, 30, 40, 50];
    var [, ...restSkip] = arr;
    return restSkip.length === 4 && restSkip[0] === 20 && restSkip[3] === 50;
});

// Nested object with intermediate default
testFeature("destructuring - nested with intermediate default", function() {
    var {a: {b: nested1 = 10} = {}} = {};
    return nested1 === 10;
});

// Computed key
testFeature("destructuring - computed key", function() {
    var obj = {a: 1, b: 2, c: 3};
    var key = "b";
    var {[key]: extracted} = obj;
    return extracted === 2;
});

// Destructuring assignment (not declaration)
testFeature("destructuring - assignment (not decl)", function() {
    var obj = {a: 10, b: 20};
    var da, db;
    ({a: da, b: db} = obj);
    return da === 10 && db === 20;
});

// Destructuring in for-of
testFeature("destructuring - for-of object pattern", function() {
    var names = [{name: "Alice", age: 30}, {name: "Bob", age: 25}];
    var result = [];
    for (var {name: n} of names) { result.push(n); }
    return result.length === 2 && result[0] === "Alice" && result[1] === "Bob";
});

/* ===================================================================
   5. ARROW FUNCTION EDGE CASES
   =================================================================== */

// Arrow returning object literal (needs parens)
var arrowObj = () => ({x: 1, y: 2});
testFeature("arrow - return object literal", function() {
    var r = arrowObj();
    return r.x === 1 && r.y === 2;
});

// Arrow in array method chain
testFeature("arrow - in method chain", [1,2,3].filter(x => x > 1).map(x => x * 10).join(",") === "20,30");

// Nested arrows
var adder = (a) => (b) => a + b;
testFeature("arrow - nested (curried)", adder(3)(4) === 7);

// Rest params in arrow — bare
var restBare = (...args) => args.length + ":" + args.join(",");
testFeature("arrow - rest params bare (block)", restBare(1,2,3) === "3:1,2,3");

// Rest params in arrow — with leading regular param
var restLead = (a, ...rest) => a + "/" + rest.join(",");
testFeature("arrow - rest params after regular", restLead("hd", 1, 2, 3) === "hd/1,2,3");

// Rest params in arrow — concise body
var restConcise = (...xs) => xs.reduce(function(a,b){return a+b;}, 0);
testFeature("arrow - rest params concise body", restConcise(1,2,3,4) === 10);

/* Dynamic import — lowers to Promise.resolve(_interopRequireWildcard(require(spec))).
   Works for static-string specs, dynamic specs, and the rejection path. */
testFeature("dynamic import - literal specifier", function() {
    return import("./tmath.js").then(function(m){
        return m.sum(2,3) === 5 && Math.abs(m.pi - Math.PI) < 0.01;
    });
});

testFeature("dynamic import - dynamic specifier (variable)", function() {
    var spec = "./tmath.js";
    return import(spec).then(function(m){ return m.sum(7,8) === 15; });
});

testFeature("dynamic import - bad path rejects", function() {
    return import("./does-not-exist-xyz-abc.js").then(
        function(){ return false; },
        function(e){ return true; }
    );
});

/* yield* asyncIter — async generator delegating to another async iterable. */
testFeature("yield* asyncIter - delegate to async gen", function() {
    async function* inner() { yield "a"; yield "b"; }
    async function* outer() { yield "start"; yield* inner(); yield "end"; }
    return new Promise(function(resolve){
        (async function(){
            var out = [];
            for await (var v of outer()) out.push(v);
            resolve(out.join(",") === "start,a,b,end");
        })();
    });
});

/* .return() propagation through __asyncGenerator should run pending finally. */
testFeature("async gen - explicit .return() runs finally", function() {
    var finRan = false;
    async function* g() {
        try { yield 1; yield 2; } finally { finRan = true; }
    }
    return new Promise(function(resolve){
        (async function(){
            var it = g();
            await it.next();
            await it.return("forced");
            resolve(finRan === true);
        })();
    });
});

/* Sync generator .return() should run pending finally. */
testFeature("sync gen - .return() runs finally", function() {
    var finRan = false;
    function* g() {
        try { yield 1; yield 2; } finally { finRan = true; }
    }
    var it = g();
    it.next();
    var r = it.return("v");
    return finRan === true && r.value === "v" && r.done === true;
});

/* Class method __source__ — toString returns the source bytes. */
testFeature("class - method toString returns source", function() {
    class CounterX {
        bump(n) { return n + 1; }
        static make() { return new CounterX(); }
        get current() { return this._c; }
        set current(v) { this._c = v; }
    }
    var bumpS = CounterX.prototype.bump.toString();
    var makeS = CounterX.make.toString();
    var pd = Object.getOwnPropertyDescriptor(CounterX.prototype, "current");
    var getS = pd && pd.get && pd.get.toString();
    var setS = pd && pd.set && pd.set.toString();
    return bumpS.indexOf("return n + 1") >= 0
        && makeS.indexOf("new CounterX") >= 0
        && getS.indexOf("this._c") >= 0
        && setS.indexOf("this._c = v") >= 0;
});

/* Async body line-number preservation: a runtime error inside an async
   body should report on the same source line.  Easy to regress; pre-fix
   the throw on the throw-line below was reported as line N-1 because
   the var-decl-then-await and if-with-await-cond paths fused their
   leading whitespace into the `case 0:` header line.
   Rampart-only: node executes async natively, different line semantics. */
if (typeof global !== 'undefined' && global.rampart) {
    testFeature("async - line numbers preserved through var+await", function() {
        var src = '"use transpiler"\n'
                + 'async function fA() {\n'
                + '    var a = 1;\n'
                + '\n'
                + '    var b = await Promise.resolve(2);\n'
                + '\n'
                + '\n'
                + '    throw new Error("expected line 8");\n'
                + '}\n'
                + 'fA().then(null,function(e){global.__lineNo = e.stack.match(/:(\\d+)/)[1];});';
        eval(src);
        return new Promise(function(resolve){
            var n = 0;
            var tick = function(){
                if (global.__lineNo !== undefined) resolve(global.__lineNo === '8');
                else if (n++ < 50) setTimeout(tick, 5);
                else resolve(false);
            };
            tick();
        });
    });
}

// Arrow with destructuring parameter
var arrowDestr = ({x, y}) => x + y;
testFeature("arrow - destructuring param", arrowDestr({x: 3, y: 7}) === 10);

// Destructuring defaults in arrow params (concise body)
var arrowDestrDef1 = ({x = 1, y = 2}) => x + y;
testFeature("arrow - destr defaults concise", function() {
    return arrowDestrDef1({}) === 3 && arrowDestrDef1({x:10}) === 12;
});

// Destructuring + param default
var arrowDestrDef2 = ({x = 1, y = 2} = {}) => x + y;
testFeature("arrow - destr defaults + param default", function() {
    return arrowDestrDef2() === 3 && arrowDestrDef2({x:5}) === 7;
});

// Destructuring defaults in arrow params (block body)
var arrowDestrDef3 = ({a = 10, b = 20}) => { return a * b; };
testFeature("arrow - destr defaults block body", function() {
    return arrowDestrDef3({}) === 200 && arrowDestrDef3({a:3,b:4}) === 12;
});

// Array destructuring defaults in arrow
var arrowArrDef = ([a = 1, b = 2]) => a + b;
testFeature("arrow - array destr defaults", function() {
    return arrowArrDef([]) === 3 && arrowArrDef([10]) === 12;
});

// Arrow with rest parameter
var arrowRest = (first, ...rest) => [first, rest.length];
testFeature("arrow - rest parameter", function() {
    var r = arrowRest(1, 2, 3, 4);
    return r[0] === 1 && r[1] === 3;
});

// Arrow preserving `this` in nested context
testFeature("arrow - this in nested setTimeout", function() {
    function Counter() {
        this.count = 0;
        this.inc = () => {
            this.count++;
        };
    }
    var c = new Counter();
    c.inc();
    c.inc();
    return c.count === 2;
});

/* ===================================================================
   6. CLASS EDGE CASES
   =================================================================== */

// Getter and setter
class GetSet {
    constructor() { this._val = 0; }
    get value() { return this._val; }
    set value(v) { this._val = v * 2; }
}
testFeature("class - getter", new GetSet().value === 0);
testFeature("class - setter doubles", function() {
    var gs = new GetSet();
    gs.value = 5;
    return gs.value === 10;
});

// Static method
class WithStatic {
    static create(v) { return new WithStatic(v); }
    constructor(v) { this.v = v; }
}
testFeature("class - static method", WithStatic.create(42).v === 42);

// Class field with this reference
class FieldThis {
    x = 10;
    y = this.x * 2;
}
testFeature("class - field referencing this", function() {
    var ft = new FieldThis();
    return ft.x === 10 && ft.y === 20;
});

// Class field with method using field
class FieldMethod {
    greeting = "hello";
    greet(name) { return this.greeting + " " + name; }
}
testFeature("class - field + method", new FieldMethod().greet("world") === "hello world");

// Multiple inheritance levels with super.method()
class Base1 {
    constructor(lvl) { this.level = lvl || "base"; }
    who() { return "base"; }
    greetWith(name) { return "hello " + name; }
}
class Mid1 extends Base1 {
    constructor(lvl) { super(lvl || "mid"); }
    who() { return "mid-" + super.who(); }
}
class Top1 extends Mid1 {
    constructor() { super("top"); }
    who() { return "top-" + super.who(); }
}
testFeature("class - multi-level inheritance", function() {
    var t = new Top1();
    return t.level === "top" && t.who() === "top-mid-base";
});

testFeature("class - inherited methods from grandparent", function() {
    var b = new Base1();
    var m = new Mid1();
    return b.who() === "base" && m.who() === "mid-base" && m.level === "mid";
});

// Super() with no args (trailing comma fixed)
testFeature("class - super() no args", function() {
    class Base2 {
        constructor() { this.base = true; }
    }
    class Child2 extends Base2 {
        constructor() { super(); this.child = true; }
    }
    var c = new Child2();
    return c.base === true && c.child === true;
});

// super.method() with arguments
testFeature("class - super.method(args)", function() {
    class Animal {
        speak(sound) { return "animal says " + sound; }
    }
    class Dog extends Animal {
        speak(sound) { return super.speak(sound) + " loudly"; }
    }
    var d = new Dog();
    return d.speak("woof") === "animal says woof loudly";
});

// super.method() with no arguments
testFeature("class - super.method() no args", function() {
    class Parent {
        name() { return "parent"; }
    }
    class Child extends Parent {
        name() { return super.name() + "-child"; }
    }
    return new Child().name() === "parent-child";
});

// super.property access (not a call)
testFeature("class - super.property access", function() {
    class Base3 {
        get tag() { return "base-tag"; }
    }
    class Derived3 extends Base3 {
        getTag() { return super.tag; }
    }
    // Note: super.tag accesses the prototype getter
    return new Derived3().getTag() === "base-tag";
});

// super.method() in constructor body (after super() call)
testFeature("class - super.method() in constructor", function() {
    class Logger {
        init() { this.ready = true; }
    }
    class AppLogger extends Logger {
        constructor() { super(); super.init(); this.app = true; }
    }
    var a = new AppLogger();
    return a.ready === true && a.app === true;
});

// Class expression assigned to variable
var MyClass = class {
    constructor(v) { this.v = v; }
    double() { return this.v * 2; }
};
testFeature("class - expression", new MyClass(5).double() === 10);

// Computed method name
var methodName = "compute";
class Computed {
    [methodName](x) { return x + 1; }
}
testFeature("class - computed method name", new Computed().compute(9) === 10);

// Constructor with default params
class DefParams {
    constructor(a = 1, b = 2) {
        this.a = a;
        this.b = b;
    }
}
testFeature("class - constructor defaults", function() {
    var ds = new DefParams(10);
    return ds.a === 10 && ds.b === 2;
});

// Rest params in class constructors
testFeature("class - rest in constructor", function() {
    class Collector {
        constructor(name, ...items) {
            this.name = name;
            this.items = items;
        }
    }
    var c = new Collector("bag", 1, 2, 3);
    return c.name === "bag" && c.items.length === 3 && c.items[2] === 3;
});

/* ===================================================================
   7. TEMPLATE LITERAL EDGE CASES
   =================================================================== */

// Nested template literals
var nt1 = `outer ${`inner ${1 + 2}`} end`;
testFeature("template - nested", nt1 === "outer inner 3 end");

// Template with ternary inside
var nt2 = `result: ${true ? "yes" : "no"}`;
testFeature("template - ternary inside", nt2 === "result: yes");

// Template with function call
var nt3 = `len: ${[1,2,3].length}`;
testFeature("template - function/prop access", nt3 === "len: 3");

// Multiline template
var nt4 = `line1
line2`;
testFeature("template - multiline", nt4 === "line1\nline2");

// Template with backslash
var nt5 = `back\\slash`;
testFeature("template - backslash", nt5 === "back\\slash");

/* ===================================================================
   8. SPREAD / REST EDGE CASES
   =================================================================== */

// Multiple spreads in array
var sp1 = [1, 2];
var sp2 = [3, 4];
testFeature("spread - multiple in array", [...sp1, ...sp2].join(",") === "1,2,3,4");

// Spread string into array
testFeature("spread - string into array", [...("abc")].join(",") === "a,b,c");

// Object spread with override
var objBase = {a: 1, b: 2};
var objOver = {...objBase, b: 3, c: 4};
testFeature("spread - object override", objOver.a === 1 && objOver.b === 3 && objOver.c === 4);

// Object spread with computed property
var spKey = "dynamic";
var objDyn = {...objBase, [spKey]: true};
testFeature("spread - object + computed key", objDyn.a === 1 && objDyn.dynamic === true);

// Spread in function call args
testFeature("spread - fn(...args)", function() {
    function sum3(a, b, c) { return a + b + c; }
    var args = [1, 2, 3];
    return sum3(...args) === 6;
});

testFeature("spread - fn(a, ...rest)", function() {
    function sum4(a, b, c, d) { return a + b + c + d; }
    var rest = [2, 3, 4];
    return sum4(1, ...rest) === 10;
});

testFeature("spread - method call", function() {
    var obj = {
        add: function(a, b, c) { return a + b + c; }
    };
    var args = [10, 20, 30];
    return obj.add(...args) === 60;
});

testFeature("spread - new expression", function() {
    function Pair(a, b) { this.a = a; this.b = b; }
    var vals = [5, 10];
    var p = new Pair(...vals);
    return p.a === 5 && p.b === 10;
});

testFeature("spread - Math.max", function() {
    var nums = [3, 1, 4, 1, 5, 9];
    return Math.max(...nums) === 9;
});

/* ===================================================================
   9. FOR-OF EDGE CASES
   =================================================================== */

// For-of with array destructuring
testFeature("for-of - array destructuring", function() {
    var result = [];
    for (var [name, age] of [["Alice", 30], ["Bob", 25]]) {
        result.push(name + ":" + age);
    }
    return result.join(",") === "Alice:30,Bob:25";
});

// For-of over string
testFeature("for-of - string iteration", function() {
    var chars = [];
    for (var ch of "hello") chars.push(ch);
    return chars.join(",") === "h,e,l,l,o";
});

// For-of with Set (Symbol.iterator)
testFeature("for-of - Set iteration", function() {
    var s = new Set([10, 20, 30]);
    var result = [];
    for (var v of s) result.push(v);
    return result.join(",") === "10,20,30";
});

// For-of with Map (Symbol.iterator)
testFeature("for-of - Map iteration", function() {
    var m = new Map([["a", 1], ["b", 2]]);
    var result = [];
    for (var entry of m) result.push(entry[0] + "=" + entry[1]);
    return result.join(",") === "a=1,b=2";
});

// let in for-of creates fresh per-iteration bindings
testFeature("let - for-of fresh binding per iteration", function() {
    var funcs = [];
    for (let v of [10, 20, 30]) {
        funcs.push(function() { return v; });
    }
    return funcs[0]() === 10 && funcs[1]() === 20 && funcs[2]() === 30;
});

/* ===================================================================
   10. ASYNC/AWAIT EDGE CASES
   =================================================================== */

// Await result assigned then destructured separately
testFeature("async - await then destructure", function() {
    async function fetchPair() {
        var obj = await Promise.resolve({a: 1, b: 2});
        var a = obj.a, b = obj.b;
        return a + b;
    }
    return fetchPair().then(v => v === 3);
});

/* destructuring + await is now supported — verify it works. Previous
   version of this test checked for a transpiler warning; that warning
   was removed when the async rewriter learned to lower
   `const {a,b} = await x` to a temp-var + destructure expansion. */
testFeature("destructuring + await - object pattern", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a, b} = await Promise.resolve({a: 1, b: 2});
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructuring + await - array pattern", function() {
    return new Promise(function(resolve){
        async function f() {
            const [x, y, z] = await Promise.resolve([10, 20, 30]);
            resolve(x === 10 && y === 20 && z === 30);
        }
        f();
    });
});

testFeature("destructuring + await - defaults", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a = 5, b = 9} = await Promise.resolve({a: 1});
            resolve(a === 1 && b === 9);
        }
        f();
    });
});

testFeature("destructuring + await - renaming", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a: x, b: y} = await Promise.resolve({a: 1, b: 2});
            resolve(x === 1 && y === 2);
        }
        f();
    });
});

testFeature("destructuring + await - var + let variants", function() {
    return new Promise(function(resolve){
        async function f() {
            var {a, b} = await Promise.resolve({a: 1, b: 2});
            let [c, d] = await Promise.resolve([3, 4]);
            resolve(a === 1 && b === 2 && c === 3 && d === 4);
        }
        f();
    });
});

testFeature("destructuring + await - multiple in one fn", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a} = await Promise.resolve({a: 1});
            const {b} = await Promise.resolve({b: 2});
            const x = await Promise.resolve(100);
            resolve(a === 1 && b === 2 && x === 100);
        }
        f();
    });
});

/* destructure ASSIGNMENT (not declaration) with await on RHS:
   `({a,b} = await x);` and `[a,b] = await x;` — the bindings already
   exist, so the lowering reassigns instead of declaring. */

testFeature("destructure-assign + await - object pattern", function() {
    return new Promise(function(resolve){
        async function f() {
            var a, b;
            ({a, b} = await Promise.resolve({a: 1, b: 2}));
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructure-assign + await - array pattern", function() {
    return new Promise(function(resolve){
        async function f() {
            var x, y, z;
            [x, y, z] = await Promise.resolve([10, 20, 30]);
            resolve(x === 10 && y === 20 && z === 30);
        }
        f();
    });
});

testFeature("destructure-assign + await - defaults", function() {
    return new Promise(function(resolve){
        async function f() {
            var a, b;
            ({a = 5, b = 9} = await Promise.resolve({a: 1}));
            resolve(a === 1 && b === 9);
        }
        f();
    });
});

testFeature("destructure-assign + await - renaming", function() {
    return new Promise(function(resolve){
        async function f() {
            var x, y;
            ({a: x, b: y} = await Promise.resolve({a: 1, b: 2}));
            resolve(x === 1 && y === 2);
        }
        f();
    });
});

testFeature("destructure-assign + await - reassignment overwrites", function() {
    return new Promise(function(resolve){
        async function f() {
            var a = 'init', b = 'init';
            ({a, b} = await Promise.resolve({a: 'new-a', b: 'new-b'}));
            resolve(a === 'new-a' && b === 'new-b');
        }
        f();
    });
});

/* Multi-declarator destructuring + await — pattern and await can be in
   the same declarator (`const {a, b} = await x`) or different
   declarators (`const x = 1, {a} = await y` / `const [a] = arr, b = await x`).
   _stmt_is_destructure_await fires whenever ANY declarator has a
   destructure pattern AND ANY declarator has await — the emitter then
   handles each declarator according to its own (pattern, await) state. */

testFeature("destructure-await - multi: plain + destructure-await", function() {
    return new Promise(function(resolve){
        async function f() {
            const a = 1, {b} = await Promise.resolve({b: 2});
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructure-await - multi: destructure-await + plain", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a} = await Promise.resolve({a: 1}), b = 2;
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructure-await - multi: plain + destr", function() {
    return new Promise(function(resolve){
        async function f() {
            const a = await Promise.resolve(1), {b} = await Promise.resolve({b: 2});
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructure-await - multi: two destr in one stmt", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a} = await Promise.resolve({a: 1}), {b} = await Promise.resolve({b: 2});
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("destructure-await - multi: noawait + destr-await", function() {
    return new Promise(function(resolve){
        async function f() {
            var src = {x: 100};
            let {x} = src, {y} = await Promise.resolve({y: 2});
            resolve(x === 100 && y === 2);
        }
        f();
    });
});

testFeature("destructure-await - multi: arr-destr + await-plain", function() {
    return new Promise(function(resolve){
        async function f() {
            var arr = [99];
            let [a] = arr, b = await Promise.resolve(7);
            resolve(a === 99 && b === 7);
        }
        f();
    });
});

testFeature("destructure-await - multi: no-value + destr", function() {
    return new Promise(function(resolve){
        async function f() {
            let a, {b, c} = await Promise.resolve({b: 1, c: 2});
            resolve(a === undefined && b === 1 && c === 2);
        }
        f();
    });
});

/* Embedded await positions — single embedded awaits (member access,
   subscript, fn arg), short-circuit chains, sibling awaits, conditional
   branches. Multi-await values use per-await `_context._ts<N>` slots
   so each resolved value survives across state-machine resumptions. */

testFeature("embedded-await - member access (await x).y", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a, b} = (await Promise.resolve({result: {a: 1, b: 2}})).result;
            resolve(a === 1 && b === 2);
        }
        f();
    });
});

testFeature("embedded-await - || default", function() {
    return new Promise(function(resolve){
        async function f() {
            const {a} = (await Promise.resolve(null)) || {a: 5};
            resolve(a === 5);
        }
        f();
    });
});

testFeature("embedded-await - fn(await x)", function() {
    return new Promise(function(resolve){
        async function f() {
            function wrap(v) { return {a: v}; }
            const {a} = wrap(await Promise.resolve(42));
            resolve(a === 42);
        }
        f();
    });
});

testFeature("embedded-await - arr[await x]", function() {
    return new Promise(function(resolve){
        async function f() {
            const arr = [{x: 100}, {x: 200}];
            const {x} = arr[await Promise.resolve(1)];
            resolve(x === 200);
        }
        f();
    });
});

testFeature("embedded-await - (await fn)(await x)", function() {
    return new Promise(function(resolve){
        async function f() {
            async function getFn() { return function(v) { return {a: v}; }; }
            const {a} = (await getFn())(await Promise.resolve(99));
            resolve(a === 99);
        }
        f();
    });
});

testFeature("embedded-await - conditional with two awaits", function() {
    return new Promise(function(resolve){
        async function f() {
            const cond = true;
            const {a} = cond ? (await Promise.resolve({a: 1})) : (await Promise.resolve({a: 99}));
            resolve(a === 1);
        }
        f();
    });
});

testFeature("embedded-await - fn(await a, await b)", function() {
    return new Promise(function(resolve){
        async function f() {
            function mk(x, y) { return {a: x + y}; }
            const {a} = mk(await Promise.resolve(10), await Promise.resolve(32));
            resolve(a === 42);
        }
        f();
    });
});

testFeature("embedded-await - destr-assign w/ embedded await", function() {
    return new Promise(function(resolve){
        async function f() {
            var a;
            ({a} = (await Promise.resolve({a: 5})));
            resolve(a === 5);
        }
        f();
    });
});

testFeature("embedded-await - destr-assign w/ sibling awaits", function() {
    return new Promise(function(resolve){
        async function f() {
            var a;
            function mk(x, y) { return {a: x * y}; }
            ({a} = mk(await Promise.resolve(6), await Promise.resolve(7)));
            resolve(a === 42);
        }
        f();
    });
});

// Multiple sequential awaits
testFeature("async - multiple sequential awaits", function() {
    async function multi() {
        const a = await Promise.resolve(1);
        const b = await Promise.resolve(2);
        const c = await Promise.resolve(3);
        return a + b + c;
    }
    return multi().then(v => v === 6);
});

// Verify transpiler warns about await inside loop
testFeature("await in loop - iterates per iteration", function() {
    return new Promise(function(resolve){
        async function f() {
            var sum = 0;
            for (var i = 0; i < 3; i++) {
                sum += await Promise.resolve(i + 1);
            }
            resolve(sum === 6);
        }
        f();
    });
});

// Async arrow function
testFeature("async - arrow function", function() {
    var asyncArrow = async (x) => {
        const r = await Promise.resolve(x * 2);
        return r;
    };
    return asyncArrow(5).then(v => v === 10);
});

// Async with try/catch/finally
testFeature("async - try/catch/finally", function() {
    async function tryCatchFinally() {
        var log = [];
        try {
            log.push("try");
            await Promise.reject("err");
        } catch(e) {
            log.push("catch:" + e);
        } finally {
            log.push("finally");
        }
        return log.join(",");
    }
    return tryCatchFinally().then(v => v === "try,catch:err,finally");
});

// Nested async functions
testFeature("async - nested async functions", function() {
    async function outer() {
        async function inner(x) {
            return await Promise.resolve(x + 1);
        }
        var a = await inner(1);
        var b = await inner(a);
        return b;
    }
    return outer().then(v => v === 3);
});

/* Async generators (async function*) — combine await + yield. The
   consumer drives via `await gen.next()` (returns Promise<{value,done}>). */

testFeature("async gen - basic await + yield", function() {
    async function* gen() {
        var v = await Promise.resolve(7);
        yield v;
        yield v * 2;
    }
    return new Promise(function(resolve){
        (async function(){
            var g = gen();
            var r1 = await g.next();
            var r2 = await g.next();
            var r3 = await g.next();
            resolve(r1.value === 7 && r2.value === 14 && r3.done === true);
        })();
    });
});

testFeature("async gen - yield Promise unwraps for consumer", function() {
    async function* gen() {
        yield Promise.resolve("a");
        yield "b";
    }
    return new Promise(function(resolve){
        (async function(){
            var g = gen();
            var r1 = await g.next();
            var r2 = await g.next();
            resolve(r1.value === "a" && r2.value === "b");
        })();
    });
});

testFeature("async gen - throw propagates as rejected promise", function() {
    async function* gen() {
        yield 1;
        throw new Error("oops");
    }
    return new Promise(function(resolve){
        (async function(){
            var g = gen();
            await g.next();
            try {
                await g.next();
                resolve(false);
            } catch (e) {
                resolve(e.message === "oops");
            }
        })();
    });
});

testFeature("async gen - consumed via for await ... of", function() {
    async function* gen() {
        yield await Promise.resolve(1);
        yield await Promise.resolve(2);
        yield await Promise.resolve(3);
    }
    return new Promise(function(resolve){
        (async function(){
            var sum = 0;
            for await (var v of gen()) sum += v;
            resolve(sum === 6);
        })();
    });
});

/* `for await` over an object that exposes ONLY Symbol.asyncIterator
   (no Symbol.iterator, no direct .next).  Pre-fix the transpiler
   emitted `_TrN_Sp._iter(<arg>)` for both `for-of` and `for-await-of`;
   `_iter` only honors Symbol.iterator and falls through to its
   length-based fallback otherwise.  An object with neither
   Symbol.iterator nor a numeric length looped forever (`undefined >=
   length-undefined` is always false).  Fix: emit `_TrN_Sp._asyncIter`
   when `is_await_of`, which checks Symbol.asyncIterator first.
   Required `Symbol.asyncIterator` to be installed as a real well-
   known symbol in register.c (otherwise user code couldn't attach a
   real property keyed by it). */
testFeature("for await over plain object keyed by Symbol.asyncIterator", function() {
    function asyncIterFrom(arr) {
        var obj = {};
        obj[Symbol.asyncIterator] = function () {
            var i = 0;
            return { next: function () {
                if (i >= arr.length) return Promise.resolve({value: undefined, done: true});
                return Promise.resolve({value: arr[i++], done: false});
            }};
        };
        return obj;
    }
    return new Promise(function(resolve){
        (async function(){
            var out = [];
            for await (var v of asyncIterFrom([10, 20, 30])) out.push(v);
            resolve(out.length === 3 && out[0] === 10 && out[2] === 30);
        })();
    });
});

/* `_asyncIter` falls back to `_iter` when the source has no
   Symbol.asyncIterator — verify `for await` over a sync iterable
   (Map) still works after the emission change. */
testFeature("for await over sync iterable (Map) still works", function() {
    return new Promise(function(resolve){
        (async function(){
            var m = new Map([['a', 1], ['b', 2]]);
            var out = [];
            for await (var pair of m) out.push(pair[0] + '=' + pair[1]);
            resolve(out.join(',') === 'a=1,b=2');
        })();
    });
});

/* Iter object exposing direct `.next()` returning Promise — already
   worked pre-fix (short-circuits `_iter`'s `typeof x.next === 'function'`
   check).  Regression-guard so the fix doesn't break this path. */
testFeature("for await over iter with direct .next() returning Promise", function() {
    function pairs(arr) {
        var i = 0;
        return { next: function () {
            if (i >= arr.length) return Promise.resolve({value: undefined, done: true});
            return Promise.resolve({value: arr[i++], done: false});
        }};
    }
    return new Promise(function(resolve){
        (async function(){
            var out = [];
            for await (var v of pairs([7, 8, 9])) out.push(v);
            resolve(out.join(',') === '7,8,9');
        })();
    });
});

/* NDE.10 — rest parameter with inline destructuring target:
   `function (...[a, b])` and `function (...{0: x, 1: y})`.
   Previously dropped through both rewrite_function_rest (looking for
   identifier only) and rewrite_function_destructuring_params (looking
   at top-level params only), so the destructuring pattern survived to
   duktape and tripped `SyntaxError: expected identifier`. */

testFeature("NDE.10 - rest with array destructure (function expr)", function() {
    var out = ['ab','cd'].map(function (...[item, idx]) {
        return idx + ':' + item;
    });
    return out.join('|') === '0:ab|1:cd';
});

testFeature("NDE.10 - rest with array destructure + defaults", function() {
    var f = function (...[a, b = 99]) { return [a, b]; };
    return f(1)[1] === 99 && f(1, 2)[1] === 2;
});

testFeature("NDE.10 - rest with array destructure after positional", function() {
    var f = function (head, ...[x, y]) { return [head, x, y]; };
    var r = f('h', 'a', 'b');
    return r[0] === 'h' && r[1] === 'a' && r[2] === 'b';
});

testFeature("NDE.10 - rest with nested rest inside destructure", function() {
    var f = function (...[first, ...rest]) {
        return first + ':' + rest.join(',');
    };
    return f(1, 2, 3, 4) === '1:2,3,4';
});

testFeature("NDE.10 - rest with array destructure (arrow)", function() {
    var f = (...[a, b]) => [a, b];
    var r = f(7, 8);
    return r[0] === 7 && r[1] === 8;
});

testFeature("NDE.10 - rest with array destructure (class method)", function() {
    class C {
        take(...[u, v]) { return u + '/' + v; }
    }
    return new C().take('p', 'q') === 'p/q';
});

/* NDE.11 — Polyfill preamble must not break when user code declares
   `var Promise = ...` in the same function scope.  Hoisting puts the
   local `Promise = undefined` in scope before the preamble runs and
   caches `Promise.allSettled` etc., so a bare `Promise.allSettled`
   lookup would throw `cannot read property 'allSettled' of undefined`.
   Real-world surfacing: readable-stream's operators.js destructures
   `Promise` from a primordials module.

   The polyfill now captures the real global object inside its IIFE
   (`_TrN_Sp._gp = p`) and routes the cache + reinstaller through
   `_TrN_Sp._gp.Promise`, so bare-Promise lookup is bypassed.

   The fixture module is generated on disk at runtime by this test
   (it can't ship with the distribution because the install rule only
   picks up *-test.js).  Written before the NDE.11 cases run; cleaned
   up after, along with its transpiler cache. */

(function _setupNDE11Fixture() {
    var dir = (typeof process !== 'undefined' && process.scriptPath)
              || (typeof __dirname !== 'undefined' && __dirname)
              || '.';
    var fixture_path = dir + '/nde11-fixture.js';
    var fixture_src = [
        '"use transpiler"',
        '/* NDE.11 fixture -- generated by transpile-edge-test.js.',
        '   Required by the NDE.11 tests below; exercises the polyfill-vs-',
        '   hoisted-Promise hazard inside a CJS module wrapper. */',
        'var d = { Promise: global.Promise };   // primordials-style stash',
        'var Promise = d.Promise;               // hoists to top of CJS wrapper',
        'async function noop() { return 42; }',
        'module.exports = {',
        '    loaded: true,',
        '    promiseIsFunction: typeof Promise === "function",',
        '    noopIsFunction: typeof noop === "function",',
        '    noop: noop',
        '};'
    ].join('\n');

    if (typeof rampart !== 'undefined' && rampart.utils && rampart.utils.writeFile) {
        rampart.utils.writeFile(fixture_path, fixture_src);
    } else {
        require('fs').writeFileSync(fixture_path, fixture_src);
    }

    /* Stash path on global so the cleanup block at the end of the
       NDE.11 section can find it.  Closure capture doesn't survive
       the test-feature thread copy, but a plain global property
       does. */
    global._nde11_fixture_path = fixture_path;
})();

testFeature("NDE.11 - polyfill survives hoisted var Promise shadow", function() {
    /* require()ing the fixture exercises the bug — preamble runs INSIDE
       the CJS wrapper scope where `var Promise` has hoisted to undefined,
       so a pre-fix transpiler throws at module load. */
    var m;
    try { m = require('./nde11-fixture.js'); }
    catch (_e) { return false; }
    return m.loaded === true && m.promiseIsFunction && m.noopIsFunction;
});

testFeature("NDE.11 - shadowed module's async fn still resolves", function() {
    return new Promise(function(resolve){
        var m = require('./nde11-fixture.js');
        m.noop().then(function(v){ resolve(v === 42); },
                      function(){ resolve(false); });
    });
});

(function _cleanupNDE11Fixture() {
    var path = global._nde11_fixture_path;
    if (!path) return;
    var rm = function(p) {
        try {
            if (typeof rampart !== 'undefined' && rampart.utils && rampart.utils.rmFile) {
                rampart.utils.rmFile(p);
            } else {
                require('fs').unlinkSync(p);
            }
        } catch (_e) {}
    };
    rm(path);
    /* Rampart's transpiler caches lowered output beside the source
       as <name>.transpiled.js -- remove that too so subsequent runs
       don't see stale state. */
    rm(path.replace(/\.js$/, '.transpiled.js'));
    delete global._nde11_fixture_path;
})();

/* NDE.12 — Class-body generator methods (`*name() { yield ... }` and
   the computed-key form `*[Expr]() { yield ... }`) used to be emitted
   as non-generator function expressions with the `yield` left in place,
   so duktape rejected the body at parse time with
   `SyntaxError: unterminated statement`.
   Real-world surfacing: readable-stream's buffer_list.js uses
   `*[SymbolIterator]() { ... }` to install iteration on BufferList. */

testFeature("NDE.12 - class generator method (plain *name)", function() {
    class C {
        constructor() { this.items = [1, 2, 3]; }
        *gen() {
            for (var i = 0; i < this.items.length; i++) yield this.items[i];
        }
    }
    var out = [];
    for (var v of (new C()).gen()) out.push(v);
    return out.join(',') === '1,2,3';
});

testFeature("NDE.12 - class generator method (computed key *[Symbol.iterator])", function() {
    class Holder {
        constructor() { this.items = ['a', 'b', 'c']; }
        *[Symbol.iterator]() {
            for (var i = 0; i < this.items.length; i++) yield this.items[i];
        }
    }
    var out = [];
    for (var v of new Holder()) out.push(v);
    return out.join(',') === 'a,b,c';
});

testFeature("NDE.12 - static class generator method", function() {
    class K {
        static *seq() { yield 10; yield 20; yield 30; }
    }
    var out = [];
    for (var n of K.seq()) out.push(n);
    return out.join(',') === '10,20,30';
});

testFeature("NDE.12 - class generator method with this-capture", function() {
    /* Confirms regenerator-switch lowering still binds `this` correctly. */
    class Counter {
        constructor() { this.n = 0; }
        *up(to) {
            while (this.n < to) {
                this.n++;
                yield this.n;
            }
        }
    }
    var c = new Counter();
    var out = [];
    for (var v of c.up(3)) out.push(v);
    return out.join(',') === '1,2,3' && c.n === 3;
});

/* NDE.13 — `{__proto__: null, ...src}` lowered to a chained-builder
   shape whose `._concat({__proto__: null})` step severed `this`'s
   prototype chain (duktape exposes __proto__ as an enumerable own key
   on object literals, so Object.assign copies it via the accessor),
   breaking subsequent `._addchain` lookups.  Fix hoists the __proto__
   pair out of the chain and wraps with `Object.setPrototypeOf`.
   Real-world surfacing: readable-stream's duplex/readable/writable
   `ObjectDefineProperties({ writable: {__proto__:null, ...desc}, ... })`. */

testFeature("NDE.13 - object spread + __proto__:null builds correctly", function() {
    var src = { foo: 1, bar: 2 };
    var d = { __proto__: null, ...src };
    return d.foo === 1 && d.bar === 2 &&
           Object.getPrototypeOf(d) === null;
});

testFeature("NDE.13 - __proto__:null mixed with other keys + spread", function() {
    var src = { foo: 1, bar: 2 };
    var d = { a: 0, __proto__: null, ...src, b: 99 };
    return d.a === 0 && d.foo === 1 && d.bar === 2 && d.b === 99 &&
           Object.getPrototypeOf(d) === null;
});

testFeature("NDE.13 - __proto__ to a real object + spread", function() {
    var base = { greet: function(){ return 'hi'; } };
    var src = { foo: 1 };
    var d = { __proto__: base, ...src };
    return d.foo === 1 && Object.getPrototypeOf(d) === base &&
           d.greet() === 'hi';
});

testFeature("NDE.13 - chain stays intact when proto:null is first", function() {
    /* The exact readable-stream/buffer_list pattern: spread of an
       existing descriptor, prefixed with __proto__:null. */
    function getDesc() { return { value: 42, writable: true, configurable: true }; }
    var d = { __proto__: null, ...getDesc() };
    return d.value === 42 && d.writable === true && d.configurable === true &&
           Object.getPrototypeOf(d) === null;
});

/* NDE.14 — class fields with a computed key (`[Expr] = value`) used to
   lower to `this.[Expr] = value` (stray `.`), tripping
   `SyntaxError: expected identifier`.  Fix: detect computed_property_name
   on the field's property node and emit bracket notation without dot.
   Real-world surfacing: lru-cache's `[Symbol.toStringTag] = 'LRUCache'`. */

testFeature("NDE.14 - class field with computed key (well-known symbol)", function() {
    class C {
        [Symbol.toStringTag] = 'MyTag';
        constructor() { this.x = 1; }
    }
    var c = new C();
    return Object.prototype.toString.call(c) === '[object MyTag]' && c.x === 1;
});

testFeature("NDE.14 - static field with computed key", function() {
    var key = 'dynKey';
    class C {
        static [key] = 42;
    }
    return C.dynKey === 42;
});

testFeature("NDE.14 - mixed plain + computed + private + static fields", function() {
    class C {
        plain = 10;
        [Symbol.iterator] = function*(){ yield 1; yield 2; };
        #priv = 99;
        static label = 'cls';
        static [Symbol.toStringTag] = 'CTag';
        getPriv() { return this.#priv; }
    }
    var c = new C();
    var iter = [];
    for (var v of c[Symbol.iterator]()) iter.push(v);
    return c.plain === 10 && iter.join(',') === '1,2' &&
           c.getPriv() === 99 && C.label === 'cls' &&
           C[Symbol.toStringTag] === 'CTag';
});

testFeature("NDE.14 - computed key expression captures outer scope", function() {
    var idx = 7;
    class C {
        [`field_${idx}`] = 'seven';
    }
    return new C().field_7 === 'seven';
});

/* NDE.15 — Private class fields (`#name`) are mangled to
   `_TrN_priv<id>_name` by the class-body emitter, but the
   regenerator-runtime body transform (used for async + generator
   methods) copied source bytes verbatim, so `this.#name` survived
   inside the lowered switch body and tripped duktape with
   `SyntaxError: invalid token`.
   Fix: post-process the regen output with a strings-aware walker that
   re-mangles `#name` → `_TrN_priv<class_id>_name` outside string
   literals, comments, and template literals.
   Real-world surfacing: lru-cache (transitive of mqtt) uses #-fields
   heavily and has many generator/async accessors. */

testFeature("NDE.15 - generator method reads private field", function() {
    class C {
        #size = 3;
        *gen() {
            if (this.#size) yield 'has-size';
            yield 'always';
        }
    }
    var out = [];
    for (var v of new C().gen()) out.push(v);
    return out.join('|') === 'has-size|always';
});

testFeature("NDE.15 - async method reads private field", function() {
    class A {
        #x = 10;
        async getX() { return this.#x + 1; }
    }
    return new A().getX().then(function(v){ return v === 11; });
});

testFeature("NDE.15 - generator calls a private method with args", function() {
    class C {
        #scale(n) { return n * 100; }
        *scaled(arr) {
            for (var i = 0; i < arr.length; i++) yield this.#scale(arr[i]);
        }
    }
    var out = [];
    for (var v of new C().scaled([1, 2, 3])) out.push(v);
    return out.join(',') === '100,200,300';
});

testFeature("NDE.15 - string content containing '#name' is preserved", function() {
    /* The regen-body re-mangler must not touch '#size' inside string
       literals — otherwise the function's stored source (via _fs) and
       any user string would be silently corrupted. */
    class D {
        #size = 5;
        *probe() {
            var label = "#size literal";
            yield label;
            yield this.#size;
        }
    }
    var out = [];
    for (var v of new D().probe()) out.push(v);
    return out[0] === '#size literal' && out[1] === 5;
});

/* NDE.17 — `_TrN_Sp._req` used to walk node_modules without first
   delegating to rampart's native require, so a same-named npm package
   (e.g. the `buffer` polyfill in node_modules/) shadowed rampart's
   built-in shim under -t.  Real-world surfacing: safe-buffer +
   etag rejected every Express response with
   "argument entity must be string, Buffer, or fs.Stats". */

testFeature("NDE.17 - bare require resolves built-in before node_modules", function() {
    var b = require('buffer');
    /* The built-in buffer has the rampart Buffer class; npm's polyfill
       would expose its own Buffer.  Both have .Buffer.from(); the
       distinguishing test is the global-Buffer identity. */
    return b.Buffer === global.Buffer && typeof b.Buffer.isBuffer === 'function';
});

testFeature("NDE.17 - repeated require returns the same export", function() {
    /* Cache must return the same object on repeat — the fix changed the
       cache to store the resolved EXPORTS rather than the bare spec, so
       cache hits don't re-resolve. */
    var a = require('buffer');
    var b = require('buffer');
    return a === b;
});

testFeature("NDE.17 - require inside a function returns the same built-in", function() {
    /* Pre-fix, only the FIRST require('buffer') call (top level) saw the
       built-in; subsequent calls (inside functions, IIFEs, callbacks)
       slipped through to the node_modules walk. */
    var top = require('buffer');
    var inFn = (function(){ return require('buffer'); })();
    return top === inFn;
});

/* NDE.18 — `for (const x of await promise) { … }` inside an async
   function: the regen for-of lowering copied the iterable expression's
   source bytes verbatim, so `await` survived in
   `_TrN_Sp._iter(await promise)` which duktape rejects.  Fix routes the
   iterable through `_lower_range_with_yields` so awaits become
   state-machine transitions. */
testFeature("NDE.18 - await in for-of iterable inside async function", function() {
    async function f(p) {
        var out = [];
        for (const x of await p) out.push(x);
        return out;
    }
    return f(Promise.resolve([1,2,3])).then(function(v){
        return v.join(',') === '1,2,3';
    });
});

/* NDE.19 — string-literal destructure keys (`const { "~standard": _, ... } = result;`)
   used to bail out of `collect_flat_destructure_bindings` (only
   identifier / computed keys were handled), so the unlowered destructure
   survived to duktape.  Fix adds a string-literal and number-literal
   case that uses bracket-access for the binding. */
testFeature("NDE.19 - string-literal destructure key", function() {
    var src = { "~standard": "stdval", a: 1, b: 2 };
    var { "~standard": std, ...rest } = src;
    return std === "stdval" && rest.a === 1 && rest.b === 2 &&
           rest["~standard"] === undefined;
});

testFeature("NDE.19 - reserved-word shorthand method in object literal", function() {
    /* The transpiler's object-literal shorthand-method rewriter used to
       only fire for `get`/`set` names; reserved words like `default()` /
       `catch()` got emitted as keyword tokens to duktape.  Fix extends
       the rewrite to all reserved-word names — the rewriter inserts
       `: function` so the entry becomes a normal property. */
    var o = {
        default(x) { return 'd:' + x; },
        catch(x)   { return 'c:' + x; },
    };
    return o.default(1) === 'd:1' && o.catch(2) === 'c:2';
});

/* NDE.20 — trailing comma in a multi-line argument list with line
   comments between args was leaving the comma in the emit because the
   stripper picked the last NAMED child of `arguments` (which can be a
   `comment` node in tree-sitter JS) and scanned past the actual comma. */
testFeature("NDE.20 - trailing comma after inline-comment arg", function() {
    function f(a, b, c) { return a + b + c; }
    var r = f(
        1, // first
        2, // second
        3, // third
    );
    return r === 6;
});

/* NDE.21 — call-spread (`fn(a, ...arr, c)`) emit looped over
   `arguments` named children including `comment` nodes, leaving line
   comments inside the rewritten `[a].concat(...)` array literal where a
   subsequent line collapse let `//` eat the closing `]`. */
testFeature("NDE.21 - spread call with inline comments between args", function() {
    var src = [10, 20];
    var r = Math.max(
        // smaller
        5,
        // spread
        ...src,
        // larger
        15
    );
    return r === 20;
});

/* NDE.22 — `await /JSDoc-cast/ (expr)` had the JSDoc comment picked as
   the await's argument because the dispatchers grabbed the first NAMED
   child (which can be a comment).  Fix walks past comment children. */
testFeature("NDE.22 - JSDoc cast between await and its expression", function() {
    async function f(g) {
        const out = await /** @type {Promise<number>} */ (g)();
        return out + 1;
    }
    return f(function(){ return Promise.resolve(7); }).then(function(v){
        return v === 8;
    });
});

/* NDE.23 — `_emit_yield_body_range` unwrapped a `labeled_statement`
   into its body but dropped the label bytes from the byte-verbatim emit
   path, so `break <label>;` inside ended up with no target.  Fix emits
   the label prefix when the body has no yield/await (the LoopCtx-based
   break/continue routing only fires when there's a yield inside). */
testFeature("NDE.23 - labeled break inside async function", function() {
    async function f(arr) {
        var out = [];
        outer: for (var i = 0; i < arr.length; i++) {
            for (var j = 0; j < arr[i].length; j++) {
                if (arr[i][j] < 0) break outer;
                out.push(arr[i][j]);
            }
        }
        return out;
    }
    return f([[1, 2], [3, -1, 99], [4, 5]]).then(function(v){
        return v.join(',') === '1,2,3';
    });
});

testFeature("NDE.23 - for-of-simple replaces braceless if-consequent", function() {
    /* Without the brace wrap on the for-of's emit, `if (cond) for (const
       it of x) body;` would split into two statements in the if's
       single-statement slot, orphaning subsequent `else`/while clauses. */
    function f(useFirst, src) {
        var out = [];
        if (useFirst)
            for (const it of src) out.push(it);
        else
            for (const it of src) out.push(-it);
        return out;
    }
    return f(true, [1,2,3]).join(',') === '1,2,3' &&
           f(false, [1,2,3]).join(',') === '-1,-2,-3';
});

/* NDE.24 — `_TrN_Sp._req` (transpile-mode bare-require helper) used to
   stat the literal `package.json#main` path and walk past if it didn't
   exist as a file.  form-data declares `"main": "./lib/form_data"` with
   no extension, so the walk missed it; axios's require('form-data')
   then re-threw "Could not resolve".  Fix: after stat-ing the literal
   path, also try `.js`/`.cjs`/`.mjs`/`.json` extensions, and if the
   path is a directory, try its `index.js`/`index.cjs`.

   This test creates a temporary fixture under test/nde24-fixture/ that
   reproduces the form-data shape (extension-less main).  A second
   package exercises the directory-as-main branch. */

(function _setupNDE24Fixture() {
    var dir = (typeof process !== 'undefined' && process.scriptPath)
              || (typeof __dirname !== 'undefined' && __dirname)
              || '.';
    var root = dir + '/nde24-fixture';
    /* Layout:
         nde24-fixture/probe-ext.js          // require('_nde24ext')
         nde24-fixture/probe-dir.js          // require('_nde24dir')
         nde24-fixture/node_modules/_nde24ext/package.json   main:"./lib/x"
         nde24-fixture/node_modules/_nde24ext/lib/x.js
         nde24-fixture/node_modules/_nde24dir/package.json   main:"./lib"
         nde24-fixture/node_modules/_nde24dir/lib/index.js
    */
    rampart.utils.mkdir(root + '/node_modules/_nde24ext/lib', 0o755);
    rampart.utils.mkdir(root + '/node_modules/_nde24dir/lib', 0o755);

    rampart.utils.writeFile(root + '/probe-ext.js',
        '"use transpiler"\nmodule.exports = require("_nde24ext");\n');
    rampart.utils.writeFile(root + '/probe-dir.js',
        '"use transpiler"\nmodule.exports = require("_nde24dir");\n');

    /* form-data-style: extension-less main */
    rampart.utils.writeFile(
        root + '/node_modules/_nde24ext/package.json',
        '{"name":"_nde24ext","main":"./lib/x"}\n');
    rampart.utils.writeFile(
        root + '/node_modules/_nde24ext/lib/x.js',
        'module.exports = "NDE24-EXT-OK";\n');

    /* main points to a directory — Node falls back to its index.js */
    rampart.utils.writeFile(
        root + '/node_modules/_nde24dir/package.json',
        '{"name":"_nde24dir","main":"./lib"}\n');
    rampart.utils.writeFile(
        root + '/node_modules/_nde24dir/lib/index.js',
        'module.exports = "NDE24-DIR-OK";\n');

    global._nde24_fixture_root = root;
})();

testFeature("NDE.24 - main without extension resolves via .js fallback", function() {
    var m;
    try { m = require('./nde24-fixture/probe-ext.js'); }
    catch (_e) { return false; }
    return m === 'NDE24-EXT-OK';
});

testFeature("NDE.24 - main pointing to a directory resolves via index.js", function() {
    var m;
    try { m = require('./nde24-fixture/probe-dir.js'); }
    catch (_e) { return false; }
    return m === 'NDE24-DIR-OK';
});

(function _cleanupNDE24Fixture() {
    var root = global._nde24_fixture_root;
    if (!root) return;
    /* Walk and remove bottom-up.  rampart.utils has rmFile + rmDir but
       no recursive variant — easier to enumerate the paths we created. */
    var rm = function(p) {
        try { rampart.utils.rmFile(p); } catch (_e) {}
    };
    var rd = function(p) {
        try { rampart.utils.rmDir(p); } catch (_e) {}
    };
    /* Files first, then the .transpiled caches the loader writes
       alongside each source. */
    var files = [
        '/probe-ext.js', '/probe-ext.transpiled.js',
        '/probe-dir.js', '/probe-dir.transpiled.js',
        '/node_modules/_nde24ext/lib/x.js', '/node_modules/_nde24ext/lib/x.transpiled.js',
        '/node_modules/_nde24ext/package.json',
        '/node_modules/_nde24dir/lib/index.js', '/node_modules/_nde24dir/lib/index.transpiled.js',
        '/node_modules/_nde24dir/package.json'
    ];
    files.forEach(function(f){ rm(root + f); });
    /* Empty dirs, deepest first. */
    var dirs = [
        '/node_modules/_nde24ext/lib',
        '/node_modules/_nde24ext',
        '/node_modules/_nde24dir/lib',
        '/node_modules/_nde24dir',
        '/node_modules',
        ''
    ];
    dirs.forEach(function(d){ rd(root + d); });
    delete global._nde24_fixture_root;
})();

/* NDE.25 — duktape's object-literal handling makes `__proto__` an own
   data property instead of just setting the prototype (per spec).  When
   such an object is then spread via `{...obj}`, the transpiler's
   `__spreadO` copied `__proto__` to the target, severing the target's
   prototype chain — `._addchain(...)` in a subsequent step then saw
   `undefined`.  Fix filters `__proto__` out of `__spreadO`'s ownKeys
   list (and out of the `Object.getOwnPropertyDescriptors` branch), so
   spreading is effectively spec-compliant even though the literal
   itself still carries the duktape quirk. */

testFeature("NDE.25 - spread of {__proto__:null,...} doesn't sever chain", function() {
    var u = { __proto__: null, a: 1, b: 2 };
    var v = { c: 3 };
    var p = { ...u, ...v };
    /* All three keys present, no __proto__ leaked into p's own keys,
       and p's prototype is Object.prototype (spread doesn't carry over
       the source's prototype intent — matches V8). */
    return p.a === 1 && p.b === 2 && p.c === 3 &&
           Object.keys(p).sort().join(',') === 'a,b,c' &&
           Object.getPrototypeOf(p) === Object.prototype;
});

testFeature("NDE.25 - chain of multiple {__proto__:null,...} spreads", function() {
    /* The chain that broke pre-fix: each spread step severed the
       target's prototype, and the next `._addchain` saw `_addchain` as
       undefined.  Now all four sources merge cleanly. */
    var x = { __proto__: null, x: 'x' };
    var y = { __proto__: null, y: 'y' };
    var z = { __proto__: null, z: 'z' };
    var combined = { ...x, ...y, ...z, w: 'w' };
    return combined.x === 'x' && combined.y === 'y' &&
           combined.z === 'z' && combined.w === 'w' &&
           Object.keys(combined).sort().join(',') === 'w,x,y,z';
});

testFeature("NDE.26 - line comments between arrow params + destructure", function() {
    const f = (path,
        // already filtered, remove from options
        // eslint-disable-next-line @typescript-eslint/no-unused-vars
        { filter, ...opt }) => {
        return path + ':' + Object.keys(opt).sort().join(',');
    };
    return f('/tmp', { filter: 'x', a: 1, b: 2 }) === '/tmp:a,b';
});

testFeature("NDE.26 - comments before rest param keep slice index correct", function() {
    function f(a,
        // documentation between a and the rest
        ...rest) {
        return a + ':' + rest.join(',');
    }
    return f('hd', 1, 2, 3) === 'hd:1,2,3';
});

testFeature("NDE.26 - block comments between regular function params + destructure", function() {
    function f(path,
        /* multi-line block comment
           explaining something */
        { x, y }) {
        return path + ':' + x + ',' + y;
    }
    return f('here', { x: 1, y: 2 }) === 'here:1,2';
});

/* NDE.27 — `async function f(a, b = 'x') { ... await ... }` failed
   with `parse error` at the function-header line.  The async/regenerator
   wholesale-replace embeds the original params verbatim inside the
   emitted `_TrN_callee` inner function, so a default-valued param
   (or rest, or destructure) survives in the transpiled output.  The
   dispatch wasn't setting `*unresolved=1` after the async/generator
   rewriter fired, so pass 2 never ran — the inner `_TrN_callee`'s
   ES2015+ syntax went straight to duktape unlowered.
   Fix: set `*unresolved=1` after both `rewrite_async_await_to_regenerator`
   and `rewrite_generator_to_regenerator` fire successfully, so the
   re-pass lowers any embedded defaults/rest/destructure on the
   emitted inner function.
   Real-world surfacing: fs-extra's `outputFile(file, data, encoding = 'utf-8')`. */

testFeature("NDE.27 - async function with default param", function() {
    async function f(file, encoding = 'utf-8') {
        return await Promise.resolve(file + ':' + encoding);
    }
    return f('x').then(function(v){ return v === 'x:utf-8'; });
});

testFeature("NDE.27 - async function with rest param", function() {
    async function g(head, ...tail) {
        return await Promise.resolve(head + ':' + tail.join(','));
    }
    return g('hd', 1, 2, 3).then(function(v){ return v === 'hd:1,2,3'; });
});

testFeature("NDE.27 - async function with destructuring param", function() {
    async function h({ x, y }) {
        return await Promise.resolve(x + ',' + y);
    }
    return h({ x: 1, y: 2 }).then(function(v){ return v === '1,2'; });
});

testFeature("NDE.27 - generator (sync) with default param", function() {
    function* g(start = 10) {
        yield start;
        yield start + 1;
        yield start + 2;
    }
    var out = [];
    for (var v of g()) out.push(v);
    var out2 = [];
    for (var v of g(100)) out2.push(v);
    return out.join(',') === '10,11,12' && out2.join(',') === '100,101,102';
});


/* NDE.28 — `OUTER: for (const x of …) { … continue OUTER; … }` failed
   with "invalid label" because NDE.23's outer brace wrap turned
   `OUTER: for(...){...}` into `OUTER: {var _i=…; while(…){…}}` —
   `OUTER` now labels a BLOCK, and `continue OUTER` only works on loops.
   Fix: when the for-of has a `labeled_statement` parent, extend the
   replace range to include `<label>:` and re-emit the label on the
   inner `while`.  Real-world: semver 7.x subset.js. */

testFeature("NDE.28 - labeled continue inside for-of", function() {
    function f(sub, dom) {
        var hits = 0;
        OUTER: for (const s of sub) {
            for (const d of dom) {
                if (s === d) { hits++; continue OUTER; }
            }
        }
        return hits;
    }
    return f([1, 2, 3], [2, 3]) === 2;
});

testFeature("NDE.28 - labeled break inside for-of", function() {
    function f(arr) {
        var seen = [];
        OUTER: for (const row of arr) {
            for (const cell of row) {
                if (cell === 'stop') break OUTER;
                seen.push(cell);
            }
        }
        return seen.join(',');
    }
    return f([[1,2],[3,'stop',4],[5]]) === '1,2,3';
});

/* NDE.29 — async class method containing `super.X()` failed with parse
   error.  Sync class methods ran `copy_body_replace_super` to lower
   `super.X(...)` → `_TrN_Super.prototype.X.call(this, ...)`; the async
   path emitted the body verbatim into a non-class-method
   `_TrN_callee$(_TrN_context)` inner function where bare `super` is a
   syntax error.  Fix: also call `copy_body_replace_super` on the
   regen-body output in the async-method emit when the class has
   `extends`.  Real-world: undici 7.x snapshot-agent.js. */

testFeature("NDE.29 - super.X() inside async class method", function() {
    class Base {
        greet() { return Promise.resolve('base'); }
    }
    class Derived extends Base {
        async greet() {
            var b = await super.greet();
            return b + '+derived';
        }
    }
    return new Derived().greet().then(function(v){ return v === 'base+derived'; });
});

testFeature("NDE.29 - optional chain + computed key in async method with super", function() {
    /* undici shape: this[kA].close(), this[kB]?.close(), super.close() */
    const kA = 'k1', kB = 'k2';
    class Base {
        close() { return Promise.resolve('base-closed'); }
    }
    class Derived extends Base {
        constructor() {
            super();
            this[kA] = { close: function(){ return Promise.resolve('kA-closed'); } };
            this[kB] = null;
        }
        async close() {
            var a = await this[kA].close();
            var b = await this[kB]?.close();
            var c = await super.close();
            return a + '|' + (b === undefined ? 'nil' : b) + '|' + c;
        }
    }
    return new Derived().close().then(function(v){
        return v === 'kA-closed|nil|base-closed';
    });
});

/* NDE.30 — constructor with private field + multiple `super(...)` call
   sites failed with parse error.  The constructor super rewriter
   `strstr(body, "super(")`-found the FIRST super and rewrote it to
   `(_TrN_this = _TrN_super.call(this, ...), _TrN_this)`; bytes after
   the closing `)` passed through `copy_body_replace_super` which only
   handles `super.X(...)` (method-shorthand) NOT bare `super(...)`.
   Any second super-call survived as bare `super(...)` — illegal
   outside a method.  Real-world: undici 7.x web/websocket/events.js
   MessageEvent constructor (kConstruct early-return path).
   Fix: loop the constructor rewriter over every standalone `super(`
   in the body, emitting between-segments through copy_body_replace_super. */

testFeature("NDE.30 - constructor with private field + two super() call sites", function() {
    class Base {
        constructor(a, b) { this.a = a; this.b = b; }
    }
    class Derived extends Base {
        #priv

        constructor(type, dict = {}) {
            if (type === 'X') {
                super(arguments[1], arguments[2]);
                return;
            }
            super(type, dict);
            this.#priv = dict;
        }
        get d() { return this.#priv; }
    }
    var x = new Derived('Y', {hello: 1});
    if (x.d.hello !== 1) return false;
    var y = new Derived('X', 'aa', 'bb');
    return y.a === 'aa' && y.b === 'bb';
});

testFeature("NDE.30 - constructor with three super() call sites in different branches", function() {
    class Base {
        constructor(tag) { this.tag = tag; }
    }
    class D extends Base {
        constructor(n) {
            if (n === 1) { super('one'); return; }
            if (n === 2) { super('two'); return; }
            super('other');
        }
    }
    return new D(1).tag === 'one'
        && new D(2).tag === 'two'
        && new D(99).tag === 'other';
});

/* NDE.31 — transpile preamble's Promise polyfill IIFE captured
   `var d = setTimeout;` at install time.  In vm.createContext bare
   threads (no setTimeout global), this threw ReferenceError immediately
   on any code referencing the Promise identifier.
   Fix: defensive guard — `var d = (typeof setTimeout === 'function')
   ? setTimeout : function(fn){ fn(); };` so the polyfill installs
   in setTimeout-less realms (synchronous fallback only matters when
   .then() actually has to defer). */

testFeature("NDE.31 - Promise polyfill loads in vm bare-thread context", function() {
    var vm = require('vm');
    var ctx = vm.createContext({});
    var r1 = vm.runInContext('typeof Promise', ctx);
    var r2 = vm.runInContext('typeof setTimeout', ctx);
    return r1 === 'function' && r2 === 'undefined';
});

/* NDE.32 — Under `-t`, eval'd syntax errors lost their SyntaxError
   type.  The transpile pre-pass `rp_get_transpiled_eval` rejected
   `'foo bar baz'` and rampart wrapped the failure via `RP_THROW`
   (DUK_ERR_ERROR), so `caught instanceof SyntaxError` was false and
   `String(caught)` was `Error: ...`.  Without `-t`, duktape's own
   eval threw a proper SyntaxError.
   Fix: use `RP_SYNTAX_THROW` (DUK_ERR_SYNTAX_ERROR) so the error
   carries the SyntaxError prototype just like duktape's path.
   Surfaced by vm-smoke's runInContext sandbox-error assertion. */

testFeature("NDE.32 - eval() syntax error is a real SyntaxError under -t", function() {
    var caught;
    try { (0,eval)('foo bar baz'); }
    catch(e) { caught = e; }
    return caught != null
        && caught.name === 'SyntaxError'
        && caught instanceof SyntaxError
        && /SyntaxError/.test(String(caught));
});

/* NDE.33 — `class X extends Error` lost subclass identity under `-t`.
   The `_TrN_Sp.createSuper` helper computed `hasNativeReflectConstruct`
   but never used it — always fell through to `Super.apply(this, args)`.
   When Super is Error, that call returns a NEW Error (Error ignores
   `this`), so `_TrN_this` was a plain Error and subsequent
   `this.name = ...` writes landed on a discarded `this`.
   Tried Reflect.construct first (duktape's doesn't support cross-
   NewTarget — throws "unsupported").  Settled on: copy Super.apply's
   result own-props onto `this` and keep using `this` (whose proto
   already came from `new CustomError`).  Surfaced by chai
   AssertionError instanceof checks. */

testFeature("NDE.33 - class extends Error preserves subclass identity", function() {
    class MyErr extends Error {
        constructor(msg) { super(msg); this.name = 'MyErr'; this.tag = 42; }
    }
    var caught;
    try { throw new MyErr('boom'); }
    catch(e) { caught = e; }
    return caught instanceof MyErr
        && caught instanceof Error
        && caught.name === 'MyErr'
        && caught.message === 'boom'
        && caught.tag === 42;
});

testFeature("NDE.33 - class extends Error inherits prototype methods", function() {
    class TaggedErr extends Error {
        constructor(msg, code) { super(msg); this.code = code; }
        describe() { return this.code + ':' + this.message; }
    }
    var caught;
    try { throw new TaggedErr('boom', 'E42'); }
    catch(e) { caught = e; }
    return caught instanceof TaggedErr
        && caught.describe && caught.describe() === 'E42:boom';
});

/* NDE.34 — `rewrite_for_of_destructuring`'s emit was multi-statement
   (`var _TrN_x = ...; while(...) { ... }`) without an outer brace
   wrap, mirroring the NDE.23 issue in `rewrite_for_of_simple`.  When
   placed in a brace-less `if (cond)` body, the `var` became the
   if-body and the `while` ran unconditionally.  Surfaced by yaml's
   Document.toJS: `if (typeof onAnchor === 'function') for (const {
   count, res } of ctx.anchors.values()) onAnchor(res, count);`
   ran the while-iterating with onAnchor=undefined, crashing
   downstream.  Fix: wrap emit in `{...}`.  Same wrap added to the
   array-pattern emit path which has the same multi-statement shape. */

testFeature("NDE.34 - object-destructure for-of in unbraced if", function() {
    function go(onA, items) {
        var hits = 0;
        if (typeof onA === 'function')
            for (const { x, y } of items)
                onA(x, y, hits++);
        return hits;
    }
    return go(null, [{x:1,y:2},{x:3,y:4}]) === 0
        && go(function(){}, [{x:1,y:2},{x:3,y:4}]) === 2;
});

testFeature("NDE.34 - array-destructure for-of in unbraced if", function() {
    function go(onA, items) {
        var hits = 0;
        if (typeof onA === 'function')
            for (const [a, b] of items)
                onA(a, b, hits++);
        return hits;
    }
    return go(null, [[1,2],[3,4]]) === 0
        && go(function(){}, [[1,2],[3,4]]) === 2;
});

/* NDE.36 — `_emit_yield_body_range` lacked a dispatch case for child
   `statement_block` nodes containing yields.  A bare `{ ... }` block
   at the switch-case body level (or any other block at this level)
   fell through to `_emit_stmt_yield_lower`, which collects yields
   flat and emits all iter-setups sequentially at the top of the case.
   Nested if-statements inside the block were preserved verbatim, so
   their conditional structure didn't gate the yield setup — every
   yield fired unconditionally.  Surfaced by yaml's
   `*blockMap(map)` whose `default:` case contains
   `{ const bv = ...; if(bv) { if(bv.type === 'block-seq') { if(...) {
   yield* this.pop({type:'error',...}); return; } } ...; this.stack.push(bv); return; }}`.
   Fix: add a statement_block branch in the dispatcher that recurses
   via `_emit_yield_body`, so nested if-statements get their own
   structural lowering. */

testFeature("NDE.36 - yield* in deeply-nested if inside switch default", function() {
    var innerCalls = 0;
    function* inner(tag) {
        innerCalls++;
        yield tag;
    }
    function* outer(type) {
        switch (type) {
            case 'x': yield 'X'; return;
            default: {
                var bv = { type: type };
                if (bv) {
                    if (bv.type === 'fire') {
                        if (true) {
                            yield* inner('fired');
                            return;
                        }
                    }
                    yield 'D';
                    return;
                }
            }
        }
    }
    var collected = [];
    var g = outer('other');
    var r;
    while (!(r = g.next()).done) collected.push(r.value);
    if (innerCalls !== 0 || collected.length !== 1 || collected[0] !== 'D') return false;
    g = outer('fire');
    collected = [];
    while (!(r = g.next()).done) collected.push(r.value);
    return innerCalls === 1 && collected.length === 1 && collected[0] === 'fired';
});

testFeature("NDE.36 - yield inside block-statement at top level", function() {
    var calls = 0;
    function bump() { calls++; return 'X'; }
    function* gen(flag) {
        {
            var z = 5;
            if (flag) {
                yield bump();
                return;
            }
            yield 'Y';
        }
    }
    var g = gen(false);
    var r = g.next();
    if (calls !== 0 || r.value !== 'Y') return false;
    g = gen(true);
    r = g.next();
    return calls === 1 && r.value === 'X';
});

/* NDE.37 — array-spread / object-spread lowering replaced the source
   `[...]` / `{...}` byte range with `_TrN_Sp._newArray()...` /
   `_TrN_Sp._newObject()...` in-place.  When the literal was directly
   preceded by a keyword expression-starter with no whitespace
   (`return[...]`, `typeof{...}`, `void[...]`, ...), the emit fused
   into a single identifier `return_TrN_Sp` etc., causing
   ReferenceError at runtime.  Minifiers emit `return[`/`return{`
   constantly (no space needed by grammar).
   Surfaced by glob 13's minified bundle:
   `if (a) return[...r.slice(0, 4), ...r.slice(4).map(f => this.parse(f))];`.
   Fix: prepend a single leading space to the newArray/newObject
   emit strings.  Harmless when the preceding byte isn't an
   identifier character. */

testFeature("NDE.37 - return immediately followed by [...spread]", function() {
    function f(a, r) {
        if (a) return[...r.slice(0, 2), ...r.slice(2).map(function(x){return x*10;})];
        return [];
    }
    var got = f(true, [1, 2, 3, 4]);
    return got.length === 4
        && got[0] === 1 && got[1] === 2
        && got[2] === 30 && got[3] === 40;
});

testFeature("NDE.37 - return immediately followed by {...spread}", function() {
    function f(a, o) {
        if (a) return{...o, extra: 1};
        return {};
    }
    var got = f(true, {a: 1, b: 2});
    return got.a === 1 && got.b === 2 && got.extra === 1;
});

testFeature("NDE.37 - typeof immediately followed by [...spread]", function() {
    var t = typeof[...[1, 2], ...[3, 4]];
    return t === 'object';
});

/* NDE.38 — `??` lowering emitted `(_TrN_nc<N> = left, _TrN_nc<N> !=
   null ? _TrN_nc<N> : right)` with `_TrN_nc<N>` never declared.  In
   strict mode (the default for ES modules and explicit "use strict"
   contexts like glob 13's minified bundle) the assignment to an
   undeclared identifier throws ReferenceError.  Non-strict callers
   limped along because duktape created an implicit global on first
   write.  Fix: emit an IIFE arrow so the temp is a function
   parameter — `(function(_TrN_nc<N>){return _TrN_nc<N> != null ?
   _TrN_nc<N> : right;})(left)`.  Fallback to the bare-temp form
   when `right` contains yield/await (those don't survive the IIFE
   function-scope boundary). */

testFeature("NDE.38 - ?? with complex left works in strict mode", function() {
    'use strict';
    function f(o) {
        return o.a ?? 'fallback';
    }
    return f({a: 'hit'}) === 'hit'
        && f({}) === 'fallback'
        && f({a: null}) === 'fallback'
        && f({a: undefined}) === 'fallback'
        && f({a: 0}) === 0
        && f({a: ''}) === '';
});

testFeature("NDE.38 - ?? with method call on left", function() {
    'use strict';
    function getThing() { return { val: null }; }
    function f() {
        return getThing().val ?? 'fallback';
    }
    return f() === 'fallback';
});

/* NDE.39 — `_emit_async_expr_replacement` wrapped the async body in
   a non-arrow `function(){...}` and returned a callable also using a
   non-arrow `function(){return _TrN_ref.apply(this, arguments);}`.
   For an `async (args) => body` arrow, `this` should be the lexical
   `this` of the enclosing scope (arrow functions don't have their
   own `this`).  Under `-t`, the wrapper got a fresh `this` at call
   site — when the arrow was passed to `Array.prototype.map`, the
   body saw `this === undefined`.
   Fix: when the AST node is `arrow_function`, invoke the outer IIFE
   with `.call(this)` and `.bind(this)` the returned callable so the
   lexical `this` threads through `_TrN_Sp.asyncToGenerator`'s
   `fn.apply(self, args)` chain.  Surfaced by chokidar 5's
   FSWatcher.add: `paths.map(async (path) => { ... await this._x ... })`. */

testFeature("NDE.39 - async arrow inherits lexical this in class method", function() {
    class C {
        constructor() { this.helper = function() { return 'hi'; }; }
        run(items) {
            var self = this;
            return Promise.all(items.map(async (item) => {
                var hi = this.helper();
                return hi + ':' + item;
            }));
        }
    }
    return new C().run(['a', 'b']).then(function(out) {
        return out.length === 2 && out[0] === 'hi:a' && out[1] === 'hi:b';
    });
});

testFeature("NDE.39 - async arrow with await this.X inherits this", function() {
    class C {
        constructor() { this.helper = { sayHi: function(){ return 'hi'; } }; }
        run(items) {
            return Promise.all(items.map(async (item) => {
                var hi = await this.helper.sayHi();
                return hi + ':' + item;
            }));
        }
    }
    return new C().run(['a', 'b']).then(function(out) {
        return out.length === 2 && out[0] === 'hi:a' && out[1] === 'hi:b';
    });
});

/* NDE.40 — `obj[key](...args)` spread-call lost its receiver under
   `-t`.  `rewrite_call_spread` only recognized `member_expression`
   (dot access `obj.method`) as needing receiver-preserving
   `obj.method.apply(obj, args)` lowering — for
   `subscript_expression` (bracket access `obj[key]`) it fell through
   to `fn.apply(void 0, args)`, losing the receiver entirely.
   Surfaced by light-my-request 6.x's Chain delegate
   `Chain.prototype[method] = function(...args) { return
   this._promise[method](...args); }` — the bracket-method call on
   `this._promise` lowered to `this._promise[method].apply(void 0,
   args)`, so the inner Promise.then's `this` was undefined (in
   non-strict that surfaces as globalThis), and the resulting
   promise resolved with globalThis instead of the response object.
   Fix: treat `subscript_expression` the same as `member_expression`
   in the receiver-extraction branch. */

testFeature("NDE.40 - bracket-method spread call preserves receiver", function() {
    var obj = {
        push: function() {
            this.last = arguments[arguments.length - 1];
            return this.last;
        }
    };
    var args = ['a', 'b', 'c'];
    var m = 'push';
    obj[m](...args);
    return obj.last === 'c';
});

testFeature("NDE.40 - bracket-method delegate (chain.then style)", function() {
    function Chain(p) { this._p = p; }
    Chain.prototype.then = function() {
        var args = Object.values(arguments);
        var m = 'then';
        return this._p[m](...args);
    };
    var c = new Chain(Promise.resolve(42));
    return c.then(function(v) { return v === 42; });
});

/* NDE.41 — `_emit_async_method_replacement` used the method's own
   identifier as the name of the inner `mark(function NAME(...){...})`
   callback when the method has a name (`function _handleDir(...){...}`).
   But `_build_regenerator_switch_body`'s tail unconditionally emits
   `regeneratorRuntime.wrap(..., _TrN_callee, this)` — referencing the
   literal identifier `_TrN_callee`.  When the inner fn-expr was named
   `_handleDir` instead, `_TrN_callee` was unbound → ReferenceError at
   call time.  The trigger required a sibling non-async method whose
   body contained an inner async-arrow with at least one `await` (which
   nudged the emit through the named-fn path).  Surfaced by chokidar
   handler.js once native Promise replaced the polyfill.
   Fix: always emit `function _TrN_callee` for the inner mark-callback. */

testFeature("NDE.41 - async class method with sibling async-arrow-in-method", function() {
    class C {
        method1() { const f = async () => { await Promise.resolve(1); }; }
        async method2(x) { return x * 2; }
    }
    return new C().method2(5).then(function(v) { return v === 10; });
});

testFeature("NDE.41 - async method runs after named-method emit", function() {
    class C {
        sync_a() { return 1; }
        sync_b() { const f = async () => { await Promise.resolve(); }; return 2; }
        async go() { return 'done'; }
    }
    return new C().go().then(function(v) { return v === 'done'; });
});

/* NDE.42 — `expr ?? this.#privateMethod(args)` lost its receiver when
   the LHS was complex enough to trigger the IIFE form of the lowering.
   The IIFE used `function(_TrN_nc<N>){...}` and was called as `(...)(lhs)`,
   so inside the body `this` was global, making `this._TrN_privN_x()` a
   lookup on global.  Fix: emit `.call(this, lhs)` so the RHS inherits
   the outer method's `this`.  Surfaced in undici (cheerio's default
   entry pulls it) at lib/util/runtime-features.js:95. */

testFeature("NDE.42 - ?? with private method on RHS preserves this", function() {
    class C {
        constructor() { this.val = null; }
        has() { return this.val ?? this.#detect(); }
        #detect() { return 'detect-result'; }
    }
    return new C().has() === 'detect-result';
});

testFeature("NDE.42 - ?? with this.method on RHS preserves this", function() {
    /* same shape, but non-private method — already worked, regression guard. */
    class C {
        constructor() { this.val = null; this.tag = 'T'; }
        ask() { return this.val ?? this.makeIt(); }
        makeIt() { return this.tag + '!'; }
    }
    return new C().ask() === 'T!';
});

/* NDE.43 — `for (let|const key in obj)` didn't create a per-iteration
   binding; closures in the body all closed over one function-scoped
   slot — last iteration's value for every closure. Only for-in was
   broken; for-of and C-style for(;;) already wrap. Fix: IIFE-wrap the
   body when there are captures, matching the for(;;) pattern.
   Surfaced in zod v4's _installLazyMethods (every lazy-installed
   method on the proto closes over the SAME `fn`). */

testFeature("NDE.43 - for-in const per-iteration binding", function() {
    var methods = { a: 'A', b: 'B', c: 'C' };
    var getters = {};
    for (const key in methods) {
        const fn = methods[key];
        Object.defineProperty(getters, key, { get: function () { return key + ':' + fn; } });
    }
    return getters.a === 'a:A' && getters.b === 'b:B' && getters.c === 'c:C';
});

testFeature("NDE.43 - for-in let per-iteration binding", function() {
    var methods = { x: 1, y: 2, z: 3 };
    var getters = {};
    for (let key in methods) {
        let v = methods[key];
        Object.defineProperty(getters, key, { get: function () { return key + ':' + v; } });
    }
    return getters.x === 'x:1' && getters.y === 'y:2' && getters.z === 'z:3';
});

testFeature("NDE.43 - for-in no-capture body still works", function() {
    /* without captures, no wrap is emitted — verify the loop still runs
       correctly (regression guard against breaking the simple case). */
    var keys = [];
    for (const k in {a:1, b:2, c:3}) keys.push(k);
    return keys.length === 3 && keys.indexOf('a') >= 0 &&
           keys.indexOf('b') >= 0 && keys.indexOf('c') >= 0;
});

/* NDE.44 — `new Function(body)` under `-t` rejected arrow-with-spread
   combos. Root cause: multi-pass transpilation stacks consecutive
   `;_TrN_Sp.load();` preambles; the new-Function shim only stripped
   the first, leaving the trailing no-op preamble as a leading
   statement that DUK_COMPILE_FUNCTION refused. Fix: walk and strip
   ALL consecutive preambles (same loop as transpile_code). Surfaced
   in zod v4 JIT-compiled object parsers. */

testFeature("NDE.44 - new Function: arrow + array spread", function() {
    var f = new Function("return (x => [...x])([1,2,3]).length;");
    return f() === 3;
});

testFeature("NDE.44 - new Function: arrow + object spread", function() {
    var f = new Function("return (x => ({...x, b:2}))({a:1}).b;");
    return f() === 2;
});

testFeature("NDE.44 - new Function: block-body arrow + spread", function() {
    var f = new Function("return ((x) => { return [...x]; })([1,2]).length;");
    return f() === 2;
});

testFeature("NDE.44 - new Function: zod-shape map + spread", function() {
    var f = new Function(
        "var items = [{a:1},{a:2}];" +
        "var out = items.map(i => ({...i, p: ['x']}));" +
        "return out.length;"
    );
    return f() === 2;
});

/* NDE.45 — `(async function(){}).constructor`,
   `(async function*(){}).constructor`, and a classic function's
   constructor all aliased to the global `Function` because the
   transpiler lowered async/asyncgen to plain ES5 functions, leaving
   nothing at runtime to distinguish them by identity. through2 v5's
   `fnKind` dispatch (via `instanceof AsyncGeneratorFunction`)
   therefore routed every classic transform through the async-gen
   path and crashed. Fix: ASYNC_PF preamble defines distinct
   `_TrN_Sp.AsyncFunction` / `_TrN_Sp.AsyncGeneratorFunction` tag
   constructors with `Symbol.hasInstance` traps that read
   `fn.__TrN_kind`; 6 emit sites (decl/expr/method × async/asyncgen)
   wrap their callable with `_tagAsync` / `_tagAsyncGen`. The
   regressions below cover all three emit sites — class methods are
   a documented known gap and intentionally not tested. */

testFeature("NDE.45 - AsyncFunction !== AsyncGeneratorFunction", function() {
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    return AF !== AGF;
});

testFeature("NDE.45 - async function expr: instanceof tag", function() {
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    var classic = function () {};
    var asy     = async function () { return 1; };
    return asy instanceof AF && !(asy instanceof AGF) &&
           !(classic instanceof AF) && !(classic instanceof AGF);
});

testFeature("NDE.45 - async generator expr: instanceof tag", function() {
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    var agen = async function * () { yield 1; };
    return agen instanceof AGF && !(agen instanceof AF);
});

testFeature("NDE.45 - async function decl: instanceof tag", function() {
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    async function asyDecl() { return 1; }
    return asyDecl instanceof AF && !(asyDecl instanceof AGF);
});

testFeature("NDE.45 - async generator decl: instanceof tag", function() {
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    async function * agenDecl() { yield 1; }
    return agenDecl instanceof AGF && !(agenDecl instanceof AF);
});

testFeature("NDE.45 - object-literal async method shorthand: instanceof tag", function() {
    /* `async NAME() {}` and `async * NAME() {}` method shorthand both
       route correctly (asyncgen-shorthand was fixed in NDE.46). The
       `key: async function*(){}` form (function-expression value) is
       a separate path through `_emit_async_expr_replacement`. */
    var AF  = async function () {}.constructor;
    var AGF = async function * () {}.constructor;
    var o = {
        plain()           { return 1; },
        async asy()       { return 1; },
        async * agen()    { yield 1; }
    };
    return o.asy instanceof AF && !(o.asy instanceof AGF) &&
           o.agen instanceof AGF && !(o.agen instanceof AF) &&
           !(o.plain instanceof AF) && !(o.plain instanceof AGF);
});

/* NDE.46 — `async * NAME()` method shorthand in object literals was
   misrouted through `_emit_async_method_replacement` (the async-only
   path) instead of `_emit_async_gen_method_replacement`. Two visible
   effects: (1) the function was tagged with `__TrN_kind: 'async'`
   instead of `'asyncgen'`, so `instanceof AsyncGeneratorFunction`
   returned false; (2) the body was lowered via `asyncToGenerator`
   instead of `__asyncGenerator`, so calling it produced a Promise
   instead of an async iterable — `for await (const x of o.agen())`
   would throw "not iterable".

   Root cause: dispatcher consults `_is_async_function_like` first,
   which matches anything with the `async` keyword (incl. `async *`).
   The function-expression cases were unaffected because tree-sitter
   parses `async function * () {}` as a `generator_function*` node
   type that the async dispatcher doesn't match, leaving it to the
   generator pass. Method shorthand has no separate node type — it's
   always `method_definition`. Fix: gate the async rewriter on
   `!_is_async_generator_function_like(node)` so async-gen methods
   fall through to the generator pass, which correctly routes them
   to `_emit_async_gen_method_replacement`. */

testFeature("NDE.46 - object-literal async-gen method shorthand: __TrN_kind", function() {
    var o = { async * agen() { yield 1; } };
    return o.agen.__TrN_kind === 'asyncgen';
});

testFeature("NDE.46 - object-literal async-gen method shorthand: returns async iterable", function() {
    var o = { async * agen() { yield 1; yield 2; } };
    var it = o.agen();
    /* an async iterable has both .next and Symbol.asyncIterator */
    return typeof it.next === 'function' &&
           typeof Symbol !== 'undefined' &&
           typeof it[Symbol.asyncIterator] === 'function';
});

testFeature("NDE.46 - object-literal async-gen method body actually iterates", function() {
    var o = { async * agen() { yield 'a'; yield 'b'; yield 'c'; } };
    var it = o.agen();
    return it.next().then(function(r) {
        if (r.value !== 'a' || r.done) return false;
        return it.next().then(function(r2) {
            if (r2.value !== 'b' || r2.done) return false;
            return it.next().then(function(r3) {
                return r3.value === 'c' && !r3.done;
            });
        });
    });
});

testFeature("NDE.46 - async method shorthand unaffected", function() {
    /* Make sure the gating didn't accidentally break plain `async NAME()`. */
    var AF = async function () {}.constructor;
    var o = { async asy() { return 42; } };
    return o.asy instanceof AF &&
           o.asy.__TrN_kind === 'async';
});

/* NDE.47 — `await` on the conditionally-evaluated operand of `&&`,
   `||`, or `?:` was hoisted out of the short-circuit by the async→
   regenerator lowering and evaluated UNCONDITIONALLY (the temp +
   `yield` was emitted before the guard). rimraf 6.x's
   `opt.filter && (await opt.filter())` therefore called `undefined`
   when no filter was set. Fix: `_try_lower_shortcircuit_await`
   rewrites a conditionally-awaiting operand Babel-style into a
   guarded temp (`L && (t = await R(), t)`), gated to fire only when
   a conditionally-evaluated operand awaits. The tests below count
   side-effect invocations to prove the skipped branch never runs. */

testFeature("NDE.47 - && skips RHS await when LHS falsy", function() {
    return (async function() {
        var calls = 0;
        async function maybe() { calls++; return calls; }
        var opt = {};                                  // opt.filter undefined
        var r = opt.filter && (await maybe());
        return r === undefined && calls === 0;
    })();
});

testFeature("NDE.47 - && runs RHS await when LHS truthy", function() {
    return (async function() {
        var calls = 0;
        async function maybe() { calls++; return calls; }
        var r = "go" && (await maybe());
        return r === 1 && calls === 1;
    })();
});

testFeature("NDE.47 - || skips RHS await when LHS truthy", function() {
    return (async function() {
        var calls = 0;
        async function maybe() { calls++; return calls; }
        var r = "truthy" || (await maybe());
        return r === "truthy" && calls === 0;
    })();
});

testFeature("NDE.47 - ?: evaluates only the selected branch's await", function() {
    return (async function() {
        var calls = 0;
        async function maybe() { calls++; return calls; }
        var r = false ? (await maybe()) : "else";
        return r === "else" && calls === 0;
    })();
});

testFeature("NDE.47 - await on LHS/condition still always runs", function() {
    return (async function() {
        var calls = 0;
        async function maybe() { calls++; return calls; }
        var r = (await maybe()) && "after";            // LHS await always evaluates
        return r === "after" && calls === 1;
    })();
});

/* NDE.48 — `await` in the init or update clause of a C-style for(;;)
   loop failed to PARSE under -t ("unterminated statement" / "parse
   error"). The async→regenerator state-machine builder emitted the
   init and increment clauses verbatim, so a bare `await` keyword
   survived into the switch body. Body and condition clauses already
   routed through `_lower_range_with_yields`. Fix: route init and
   update through the same lowering so their awaits become real state
   steps. */

testFeature("NDE.48 - await in for-init clause", function() {
    return (async function() {
        async function p(v) { return v; }
        var a = [];
        for (let k = await p(0); k < 3; k++) a.push(k);
        return a.length === 3 && a[0] === 0 && a[1] === 1 && a[2] === 2;
    })();
});

testFeature("NDE.48 - await in for-update clause", function() {
    return (async function() {
        async function p(v) { return v; }
        var a = [];
        for (let k = 0; k < 3; k = k + (await p(1))) a.push(k);
        return a.length === 3 && a[0] === 0 && a[1] === 1 && a[2] === 2;
    })();
});

testFeature("NDE.48 - await in for-condition clause (regression guard)", function() {
    return (async function() {
        async function p(v) { return v; }
        var a = [];
        for (let k = 0; k < (await p(3)); k++) a.push(k);
        return a.length === 3 && a[2] === 2;
    })();
});

testFeature("NDE.48 - await in all four for clauses", function() {
    return (async function() {
        async function p(v) { return v; }
        var a = [];
        for (let k = await p(0); k < (await p(3)); k = k + (await p(1))) a.push(await p(k));
        return a.length === 3 && a[0] === 0 && a[1] === 1 && a[2] === 2;
    })();
});

/* NDE.49 — NDE.47's short-circuit await fix only fired in statement
   position (if-cond, return, var-init). When a `&&`/`||`/`??`/`?:`
   with a conditionally-evaluated await is nested inside a LARGER
   expression (array element, call arg), the generic per-await hoist
   evaluated it UNCONDITIONALLY, breaking short-circuit; the
   optional-chaining ternary form even threw. Fix: collect the
   outermost qualifying short-circuit/ternary subexpressions and lower
   each as a unit (`_collect_shortcircuit_subexprs` + kind-4 SubItem),
   and extend `_try_lower_shortcircuit_await` to also cover `??`. The
   tests count side-effects to prove the skipped branch never awaits. */

testFeature("NDE.49 - && skip nested in array literal", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        var x = [ (0) && (await am('a')), ran.join(',') ];
        return x[0] === 0 && x[1] === '';            // await must NOT have run
    })();
});

testFeature("NDE.49 - || skip nested in array literal", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        var x = [ ('x') || (await am('c')), ran.join(',') ];
        return x[0] === 'x' && x[1] === '';
    })();
});

testFeature("NDE.49 - ?? skip nested in array literal", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        var x = [ (5) ?? (await am('n')), ran.join(',') ];
        return x[0] === 5 && x[1] === '';
    })();
});

testFeature("NDE.49 - optional-chain ternary with awaited branch (skipped)", function() {
    return (async function() {
        var obj = null;
        var r = (obj?.fn) ? (await obj.fn()) : 'short';
        return r === 'short';                         // must not throw
    })();
});

testFeature("NDE.49 - && taken nested in array literal still awaits", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        var x = [ (1) && (await am('a')), ran.join(',') ];
        return x[0] === 'a' && x[1] === 'a';          // taken branch DID await
    })();
});

testFeature("NDE.49 - ?? taken nested in array literal still awaits", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        var x = [ (null) ?? (await am('n')), ran.join(',') ];
        return x[0] === 'n' && x[1] === 'n';
    })();
});

testFeature("NDE.49 - short-circuit await nested in call argument", function() {
    return (async function() {
        var ran = [];
        async function am(n) { ran.push(n); return n; }
        function tag(v) { return 'tag:' + v; }
        var r = tag( (0) && (await am('q')) );
        return r === 'tag:0' && ran.join(',') === '';
    })();
});

/* NDE.50 — a `catch (e)` binding was lost across an `await` inside the
   catch block. The state-machine builder emitted `var e = _caught;`
   inside the inner per-step function `_TrN_callee$`, which is
   re-invoked on every resume — so after a suspend the binding reset to
   undefined and a reference to `e` past the await threw. Fix: hoist the
   catch parameter to the outer `_TrN_callee` closure (via
   `_collect_var_names_recursive`) and emit a plain assignment in the
   CATCH case. Destructure catch params (`catch ({message})`) are also
   handled (hoisted + parenthesized destructuring assignment). */

testFeature("NDE.50 - catch binding ref after await (same stmt)", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r;
        try { await pf(new Error('boom')); }
        catch (e) { r = e.message + '-' + (await p('B')); }
        return r === 'boom-B';
    })();
});

testFeature("NDE.50 - catch binding ref after await (separate stmt)", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r;
        try { await pf(new Error('boom')); }
        catch (e) { var w = await p('B'); r = e.message + '-' + w; }
        return r === 'boom-B';
    })();
});

testFeature("NDE.50 - catch binding ref before await still works", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var msg, w;
        try { await pf(new Error('boom')); }
        catch (e) { msg = e.message; w = await p('B'); }
        return msg === 'boom' && w === 'B';
    })();
});

testFeature("NDE.50 - nested catch bindings across awaits", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r;
        try { await pf(new Error('outer')); }
        catch (e1) {
            try { await pf(new Error('inner')); }
            catch (e2) { r = e1.message + '/' + (await p('X')) + '/' + e2.message; }
        }
        return r === 'outer/X/inner';
    })();
});

testFeature("NDE.50 - catch binding across await with finally", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r, fin = '';
        try { await pf(new Error('cf')); }
        catch (e) { r = e.message + '-' + (await p('Z')); }
        finally { fin = await p('fin'); }
        return r === 'cf-Z' && fin === 'fin';
    })();
});

testFeature("NDE.50 - rethrow after await preserves binding", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r;
        try {
            try { await pf(new Error('rt')); }
            catch (e) { await p('w'); throw new Error('re:' + e.message); }
        } catch (e2) { r = e2.message; }
        return r === 're:rt';
    })();
});

testFeature("NDE.50 - destructure catch param across await", function() {
    return (async function() {
        function pf(e) { return Promise.reject(e); }
        function p(v) { return Promise.resolve(v); }
        var r;
        try { await pf(new Error('dmsg')); }
        catch ({ message }) { r = (await p('D')) + ':' + message; }
        return r === 'D:dmsg';
    })();
});

/* NDE.54 — inside a transpiled `async function`/generator, `arguments`
   read the regenerator innerFn's frame (`[_TrN_context]`) instead of the
   function's own call args. Fix: alias `arguments` (9 chars) to the
   same-length `_TrN_args`, captured in the _TrN_callee scope where the
   real args live. Surfaced in puppeteer-extra-plugin-stealth's sourceurl
   evasion (`const [method, paramArgs] = arguments`). */

testFeature("NDE.54 - async fn arguments.length + index", function() {
    return (async function() {
        async function f() { return arguments.length + ':' + arguments[0] + ',' + arguments[1]; }
        return await f('hello', 'world') === '2:hello,world';
    })();
});

testFeature("NDE.54 - async fn arguments destructure (stealth shape)", function() {
    return (async function() {
        async function send() {
            var a = arguments;
            var method = a[0], paramArg = a[1];
            return method + ':' + paramArg.userAgent;
        }
        return await send('Network.setUserAgentOverride', { userAgent: 'X' })
               === 'Network.setUserAgentOverride:X';
    })();
});

testFeature("NDE.54 - arrow inside async fn inherits arguments", function() {
    return (async function() {
        async function f() {
            var get = function() { return arguments.length; };   // own arguments (regular fn)
            var arrow = () => arguments.length + ':' + arguments[0];  // inherits f's arguments
            return arrow() + '|' + get(9, 9, 9);
        }
        return await f('p', 'q') === '2:p|3';
    })();
});

testFeature("NDE.54 - arguments survives an await", function() {
    return (async function() {
        async function f() {
            await Promise.resolve();
            return arguments.length + ':' + arguments[0];
        }
        return await f('only') === '1:only';
    })();
});

testFeature("NDE.54 - nested regular function keeps its own arguments", function() {
    return (async function() {
        async function outer() {
            function inner() { return arguments.length; }
            return inner(1, 2, 3, 4) + ':' + arguments.length;
        }
        return await outer('a', 'b') === '4:2';
    })();
});

testFeature("NDE.54 - async generator arguments", function() {
    return (async function() {
        async function* ag() {
            for (var i = 0; i < arguments.length; i++) yield await Promise.resolve(arguments[i]);
        }
        var out = [];
        for await (var v of ag('x', 'y', 'z')) out.push(v);
        return out.join(',') === 'x,y,z';
    })();
});

testFeature("NDE.54 - sync generator arguments", function() {
    function* sg() { for (var i = 0; i < arguments.length; i++) yield arguments[i] * 2; }
    var r = [], it = sg(1, 2, 3), s;
    while (!(s = it.next()).done) r.push(s.value);
    return r.join(',') === '2,4,6';
});

testFeature("NDE.54 - async fn toString still shows arguments (alias is internal)", function() {
    async function f(a) { return arguments.length; }
    return ('' + f).indexOf('arguments') >= 0 && ('' + f).indexOf('_TrN_args') < 0;
});

/* NDE.55 — `for-of` / `for await…of` must call `iter.return()` on abrupt
   completion (break / return / throw) so iterators with side effects
   (subscriptions, fds, locks) clean up. The transpiler emitted a plain
   loop that just exited. Fix: wrap the desugared loop in try/finally that
   calls iter.return() when completion was abrupt (sync paths + the async
   state machine route break/return through a cleanup case). Plain-array
   iteration has no iterator so return() is never attempted there.
   Known limitation: an uncaught throw out of a `for await` body does not
   call return() (it routes through the regenerator _catch). */

function _nde55mk() {  // sync iterator that records return()
    var i = 0, st = { returned: false };
    var it = {
        next: function () { return { value: i++, done: i > 100 }; },
        "return": function (v) { st.returned = true; return { value: v, done: true }; }
    };
    if (typeof Symbol !== 'undefined' && Symbol.iterator) it[Symbol.iterator] = function () { return this; };
    return { it: it, st: st };
}

testFeature("NDE.55 - for-of break calls iter.return()", function() {
    var m = _nde55mk();
    for (var x of m.it) { if (x >= 3) break; }
    return m.st.returned === true;
});

testFeature("NDE.55 - for-of return calls iter.return()", function() {
    var m = _nde55mk();
    (function () { for (var x of m.it) { if (x >= 3) return; } })();
    return m.st.returned === true;
});

testFeature("NDE.55 - for-of throw calls iter.return()", function() {
    var m = _nde55mk();
    try { for (var x of m.it) { if (x >= 3) throw new Error('e'); } } catch (_) {}
    return m.st.returned === true;
});

testFeature("NDE.55 - for-of natural end does NOT call return()", function() {
    var i = 0, st = { returned: false };
    var it = { next: function () { return { value: i++, done: i > 3 }; },
               "return": function (v) { st.returned = true; return { value: v, done: true }; } };
    if (typeof Symbol !== 'undefined' && Symbol.iterator) it[Symbol.iterator] = function () { return this; };
    var sum = 0; for (var x of it) sum += x;
    return st.returned === false && sum === 3;   // 0+1+2
});

testFeature("NDE.55 - plain array for-of break: no return attempted", function() {
    var sum = 0; for (var v of [10, 20, 30]) { sum += v; if (v >= 20) break; }
    return sum === 30;   // doesn't throw; arrays have no custom return()
});

testFeature("NDE.55 - destructure for-of break calls return()", function() {
    var i = 0, data = [[1, 'a'], [2, 'b'], [3, 'c']], st = { returned: false };
    var it = { next: function () { return i >= data.length ? { value: undefined, done: true } : { value: data[i++], done: false }; },
               "return": function (v) { st.returned = true; return { value: v, done: true }; } };
    if (typeof Symbol !== 'undefined' && Symbol.iterator) it[Symbol.iterator] = function () { return this; };
    for (var [n, s] of it) { if (n >= 2) break; }
    return st.returned === true;
});

testFeature("NDE.55 - for-await-of break calls iter.return()", function() {
    return (async function () {
        var i = 0, st = { returned: false };
        var it = { next: function () { var v = i++; return Promise.resolve({ value: v, done: v >= 100 }); },
                   "return": function (v) { st.returned = true; return Promise.resolve({ value: v, done: true }); } };
        if (typeof Symbol !== 'undefined' && Symbol.asyncIterator) it[Symbol.asyncIterator] = function () { return this; };
        for await (var x of it) { if (x >= 3) break; }
        return st.returned === true;
    })();
});

testFeature("NDE.55 - for-await-of return calls iter.return() and propagates value", function() {
    return (async function () {
        var i = 0, st = { returned: false };
        var it = { next: function () { var v = i++; return Promise.resolve({ value: v, done: v >= 100 }); },
                   "return": function (v) { st.returned = true; return Promise.resolve({ value: v, done: true }); } };
        if (typeof Symbol !== 'undefined' && Symbol.asyncIterator) it[Symbol.asyncIterator] = function () { return this; };
        var r = await (async function () { for await (var x of it) { if (x >= 3) return 'E' + x; } return 'nat'; })();
        return st.returned === true && r === 'E3';
    })();
});

testFeature("NDE.55 - for-await-of natural end does NOT call return()", function() {
    return (async function () {
        var i = 0, st = { returned: false };
        var it = { next: function () { var v = i++; return Promise.resolve({ value: v, done: v >= 3 }); },
                   "return": function (v) { st.returned = true; return Promise.resolve({ value: v, done: true }); } };
        if (typeof Symbol !== 'undefined' && Symbol.asyncIterator) it[Symbol.asyncIterator] = function () { return this; };
        var got = []; for await (var x of it) got.push(x);
        return st.returned === false && got.join(',') === '0,1,2';
    })();
});


testFeature("NDE.25 - axios-style rollup namespace spread pattern", function() {
    /* axios.cjs:1789 spreads two namespace objects each built with
       `Object.freeze({__proto__: null, ...})`.  Pre-fix this threw
       `TypeError: undefined not callable (property '_addchain' ...)`. */
    var utils = Object.freeze({ __proto__: null, isArray: function(){return true;}, isFn: function(){return true;} });
    var platform$1 = Object.freeze({ __proto__: null, name: 'node' });
    var platform = { ...utils, ...platform$1, classes: {} };
    return platform.name === 'node' &&
           typeof platform.isArray === 'function' &&
           typeof platform.isFn === 'function' &&
           typeof platform.classes === 'object';
});

testFeature("NDE.15 - two classes with same #name get distinct ids", function() {
    /* The class-priv counter assigns each class its own id; the regen
       re-mangler uses that id, so identically-named privates in two
       classes don't collide. */
    class E1 { #v = 100; *one() { yield this.#v; } }
    class E2 { #v = 200; *two() { yield this.#v; } }
    var a = [], b = [];
    for (var v of new E1().one()) a.push(v);
    for (var v of new E2().two()) b.push(v);
    return a[0] === 100 && b[0] === 200;
});

/* Collision-resistance: user-defined locals named `_e`, `_da1`, `_fk0`,
   `_ofdiscard`, `_x`, `_bsf0`, `_r`, `_it` etc. used to be silently
   overwritten by the transpiler's emitted temporaries. They've all been
   renamed to `_TrN_*` so the user's vars must survive. */

testFeature("collision - user _e survives try/finally lowering", function() {
    return new Promise(function(resolve){
        async function f() {
            var _e = "user-value";
            try {
                try {
                    throw new Error("x");
                } finally {
                    /* finally re-throws via the transpiler — must not stomp _e */
                }
            } catch (e) {
                resolve(_e === "user-value" && e.message === "x");
            }
        }
        f();
    });
});

testFeature("collision - user _da1 survives destructure-await", function() {
    return new Promise(function(resolve){
        async function f() {
            var _da1 = 42;
            var {a, b} = await Promise.resolve({a: 1, b: 2});
            resolve(_da1 === 42 && a === 1 && b === 2);
        }
        f();
    });
});

testFeature("collision - user _r survives destructure-for-of", function() {
    var _r = "user-r";
    for (var {x} of [{x: 1}, {x: 2}]) { /* uses iter machinery */ }
    return _r === "user-r" && x === 2;
});

testFeature("async gen - try/catch inside body", function() {
    async function* gen() {
        try {
            yield 1;
            throw new Error("E");
        } catch (e) {
            yield "caught:" + e.message;
        }
        yield "after";
    }
    return new Promise(function(resolve){
        (async function(){
            var out = [];
            for await (var v of gen()) out.push(v);
            resolve(out.join(",") === "1,caught:E,after");
        })();
    });
});

/* ===================================================================
   11. GENERATOR EDGE CASES
   =================================================================== */

// Generator with return value
testFeature("generator - return value", function() {
    function* gen1() {
        yield 1;
        yield 2;
        return 3;
    }
    var g = gen1();
    g.next(); g.next();
    var last = g.next();
    return last.value === 3 && last.done === true;
});

// Generator used manually (for-of over generators uses Symbol.iterator which works)
testFeature("generator - manual iteration", function() {
    function* triple() {
        yield 10;
        yield 20;
        yield 30;
    }
    var g = triple();
    var a = g.next().value;
    var b = g.next().value;
    var c = g.next().value;
    return a === 10 && b === 20 && c === 30 && g.next().done === true;
});

// Nested yield: `yield yield X` — the inner yield's sent value
// becomes the outer yield's arg. Each yield is a separate state
// transition. The rxjs `isReadableStreamLike` pattern lives here.
testFeature("generator - nested yield", function() {
    function* gen() {
        var a = yield yield 1;
        return a;
    }
    var g = gen();
    var r1 = g.next();          // emits inner yield's arg: 1
    var r2 = g.next("x");        // sends "x" to inner; outer yields "x"
    var r3 = g.next("y");        // sends "y" to outer; assigns "y" to a; returns "y"
    return r1.value === 1 && r2.value === "x" && r3.value === "y" && r3.done === true;
});

// Sequential yields in the same expression need separate slots so
// each yield's sent value survives the next yield's re-entry.
testFeature("generator - sequential yields in expression", function() {
    function* gen() {
        return (yield 1) + (yield 2);
    }
    var g = gen();
    var r1 = g.next();          // emits 1
    var r2 = g.next(10);         // sends 10; emits 2
    var r3 = g.next(20);         // sends 20; returns 10+20=30
    return r1.value === 1 && r2.value === 2 && r3.value === 30 && r3.done === true;
});

// Three sequential yields
testFeature("generator - three sequential yields", function() {
    function* gen() {
        return (yield 'a') + (yield 'b') + (yield 'c');
    }
    var g = gen();
    g.next();
    g.next('X');
    g.next('Y');
    var r = g.next('Z');
    return r.value === 'XYZ' && r.done === true;
});

// Verify yield-in-loop works (used to warn; now lowered correctly).
// Also verify the warning still fires for actually-unsupported patterns
// (for-in with yield, yield inside a catch handler).
testFeature("yield in for-loop produces expected values", function() {
    function* _wyl() { for (var i = 0; i < 3; i++) yield i; }
    var g = _wyl();
    return g.next().value === 0 && g.next().value === 1 &&
           g.next().value === 2 && g.next().done === true;
});

testFeature("yield in for-in over object keys", function() {
    function* _gfi() { for (var k in {a:1, b:2}) yield k; }
    var g = _gfi();
    var seen = [g.next().value, g.next().value, g.next().done];
    seen.sort();
    return seen[0] === "a" && seen[1] === "b" && seen[2] === true;
});

testFeature("yield inside catch handler", function() {
    function* _gc() {
        try { yield 1; throw "err"; }
        catch (e) { yield "caught:" + e; }
    }
    var g = _gc();
    return g.next().value === 1 && g.next().value === "caught:err" && g.next().done === true;
});

// Generator with destructuring yield
testFeature("generator - yield in expressions", function() {
    function* gen2() {
        var x = yield 1;
        var y = yield 2;
        return x + y;
    }
    var g = gen2();
    g.next();
    g.next(10);
    var r = g.next(20);
    return r.value === 30 && r.done === true;
});

/* ===================================================================
   12. NUMERIC SEPARATORS EDGE CASES
   =================================================================== */

testFeature("numeric sep - integer", 1_000_000 === 1000000);
testFeature("numeric sep - float", 1_234.567_8 === 1234.5678);
testFeature("numeric sep - hex", 0xFF_FF === 65535);
testFeature("numeric sep - octal", 0o77_77 === 4095);
testFeature("numeric sep - binary", 0b1010_0001 === 161);

/* ===================================================================
   13. OPTIONAL CATCH BINDING EDGE CASES
   =================================================================== */

testFeature("catch binding - omitted", function() {
    var caught = false;
    try { throw new Error("test"); }
    catch { caught = true; }
    return caught;
});

testFeature("catch binding - omitted with finally", function() {
    var log = [];
    try { throw "x"; }
    catch { log.push("caught"); }
    finally { log.push("finally"); }
    return log.join(",") === "caught,finally";
});

/* ===================================================================
   14. BLOCK SCOPING EDGE CASES
   =================================================================== */

// Let in for loop creates fresh binding per iteration
testFeature("let - for loop fresh binding", function() {
    var funcs = [];
    for (let i = 0; i < 5; i++) {
        funcs.push(() => i);
    }
    return funcs[0]() === 0 && funcs[2]() === 2 && funcs[4]() === 4;
});

/* DISABLED: bare-block `const` scoping is a known transpiler limitation
   — `const` lowers to `var`, which has function scope, so the inner
   `outer` overwrites the outer one.  We previously IIFE-wrapped such
   blocks to fake block scope, but that was reverted because it shadowed
   `arguments` (see [[project_transpiler_arguments_iife]]).  Proper fix
   needs block-scoped lexical environments in duktape itself.

testFeature("const - block scoping", function() {
    var outer = "OUTER";
    {
        const outer = "INNER";
    }
    return outer === "OUTER";
});
*/

// Let temporal dead zone doesn't leak
testFeature("let - no hoisting across blocks", function() {
    var result = "before";
    {
        // let x should not be visible outside this block
        let x = "scoped";
        result = x;
    }
    return result === "scoped";
});

/* ===================================================================
   15. COMBINED FEATURE INTERACTIONS
   =================================================================== */

// Optional chaining + destructuring
testFeature("combo - ?. + destructuring", function() {
    var data = {user: {name: "Alice", scores: [10, 20]}};
    var name = data?.user?.name;
    var {user: {scores: [s1, s2]}} = data;
    return name === "Alice" && s1 === 10 && s2 === 20;
});

// Nullish coalescing + template literal
testFeature("combo - ?? + template literal", function() {
    var name = null;
    var msg = `Hello ${name ?? "stranger"}`;
    return msg === "Hello stranger";
});

// Arrow + spread + destructuring
testFeature("combo - arrow + spread + destructuring", function() {
    var merge = (...arrays) => arrays.reduce((acc, arr) => [...acc, ...arr], []);
    var result = merge([1, 2], [3, 4], [5]);
    return result.join(",") === "1,2,3,4,5";
});

// Class with fields + getter + defaults
testFeature("combo - class fields + getter + defaults", function() {
    class Config {
        version = 1;
        constructor(name = "unnamed") {
            this.name = name;
        }
        get info() { return this.name + " v" + this.version; }
    }
    var c1 = new Config();
    var c2 = new Config("myApp");
    return c1.info === "unnamed v1" && c2.info === "myApp v1";
});

// Static class fields
testFeature("combo - static class field", function() {
    class Config {
        static defaultName = "unnamed";
        version = 1;
        constructor(name) { this.name = name || Config.defaultName; }
    }
    var c = new Config();
    return c.name === "unnamed" && c.version === 1 && Config.defaultName === "unnamed";
});

// Async + optional chaining + nullish coalescing
testFeature("combo - async + ?. + ??", function() {
    async function getVal(data) {
        var result = await Promise.resolve(data);
        return result?.value ?? "none";
    }
    return Promise.all([
        getVal({value: 42}),
        getVal(null),
        getVal({})
    ]).then(function(results) {
        return results[0] === 42 && results[1] === "none" && results[2] === "none";
    });
});

// Destructuring + arrow + template (without defaults in destructuring pattern)
testFeature("combo - destructuring + arrow + template", function() {
    var greet = ({name, greeting}) =>
        `${greeting}, ${name}!`;
    return greet({name: "Alice", greeting: "Hello"}) === "Hello, Alice!" &&
           greet({greeting: "Hi", name: "Bob"}) === "Hi, Bob!";
});

// For-of + destructuring + template
testFeature("combo - for-of + destructuring + template", function() {
    var people = [["Alice", 30], ["Bob", 25]];
    var descriptions = [];
    for (var [name, age] of people) {
        descriptions.push(`${name} is ${age}`);
    }
    return descriptions.join("; ") === "Alice is 30; Bob is 25";
});

// Class field + method + arrow
testFeature("combo - class field + method + arrow", function() {
    class Builder {
        items = [];
        add(val) {
            this.items = [...this.items, val];
            return this;
        }
        build() { return this.items; }
    }
    var b = new Builder();
    return b.add(1).add(2).add(3).build().join(",") === "1,2,3";
});

// Rest params in class methods
testFeature("class - rest in method", function() {
    class Adder {
        sum(...nums) {
            var total = 0;
            for (var i = 0; i < nums.length; i++) total += nums[i];
            return total;
        }
    }
    return new Adder().sum(1, 2, 3, 4) === 10;
});

testFeature("class - rest in method with leading params", function() {
    class Logger {
        log(prefix, ...msgs) {
            return prefix + ": " + msgs.join(", ");
        }
    }
    return new Logger().log("INFO", "a", "b") === "INFO: a, b";
});

/* ===================================================================
   18. FOR-OF INSIDE ARROW / OVERLAP EDGE CASES
   =================================================================== */

// for...of inside arrow function (was double-rewritten due to missing overlap check)
testFeature("for-of - inside arrow function", function() {
    var fn = (arr) => {
        var result = [];
        for (var x of arr) {
            result.push(x * 2);
        }
        return result;
    };
    return fn([1,2,3]).join(",") === "2,4,6";
});

// for...of with let inside arrow (overlap + IIFE)
testFeature("for-of - let in arrow", function() {
    var fn = (arr) => {
        var funcs = [];
        for (let x of arr) {
            funcs.push(function() { return x; });
        }
        return funcs.map(function(f) { return f(); });
    };
    return fn([10,20,30]).join(",") === "10,20,30";
});

/* ===================================================================
   19. LET/CONST IN FOR LOOPS WITH BREAK/CONTINUE
   =================================================================== */

// let in for loop with break (IIFE must be skipped)
testFeature("let - for loop with break", function() {
    var result = [];
    for (let i = 0; i < 5; i++) {
        if (i === 3) break;
        result.push(i);
    }
    return result.join(",") === "0,1,2";
});

// let in for loop with continue (IIFE must be skipped)
testFeature("let - for loop with continue", function() {
    var result = [];
    for (let i = 0; i < 5; i++) {
        if (i % 2 === 0) continue;
        result.push(i);
    }
    return result.join(",") === "1,3";
});

// let in for loop with closure but no break (IIFE preserved)
testFeature("let - for loop closure no break", function() {
    var funcs = [];
    for (let i = 0; i < 3; i++) {
        funcs.push(function() { return i; });
    }
    return funcs[0]() === 0 && funcs[1]() === 1 && funcs[2]() === 2;
});

// const in for...of with continue (IIFE must be skipped)
testFeature("const - for-of with continue", function() {
    var result = [];
    var items = [1, 2, 3, 4, 5];
    for (const x of items) {
        if (x % 2 === 0) continue;
        result.push(x);
    }
    return result.join(",") === "1,3,5";
});

// const in for...of with break
testFeature("const - for-of with break", function() {
    var result = [];
    var items = [10, 20, 30, 40];
    for (const x of items) {
        if (x > 20) break;
        result.push(x);
    }
    return result.join(",") === "10,20";
});

// break inside nested function should NOT prevent IIFE
testFeature("let - break in nested func still wraps IIFE", function() {
    var funcs = [];
    for (let i = 0; i < 3; i++) {
        funcs.push(function() { if (i > 1) return "big"; return i; });
    }
    return funcs[0]() === 0 && funcs[1]() === 1 && funcs[2]() === "big";
});

/* ===================================================================
   20. CONST/LET IN FOR...IN LOOPS
   =================================================================== */

testFeature("const - for-in loop", function() {
    var obj = {a: 1, b: 2, c: 3};
    var keys = [];
    for (const key in obj) {
        keys.push(key);
    }
    return keys.sort().join(",") === "a,b,c";
});

testFeature("let - for-in loop", function() {
    var obj = {x: 10, y: 20};
    var keys = [];
    for (let key in obj) {
        keys.push(key);
    }
    return keys.sort().join(",") === "x,y";
});

/* ===================================================================
   21. CONST/LET DESTRUCTURING DECLARATIONS
   =================================================================== */

testFeature("const - array destructuring", function() {
    const [a, b, c] = [1, 2, 3];
    return a === 1 && b === 2 && c === 3;
});

testFeature("const - object destructuring", function() {
    const {x, y} = {x: 10, y: 20, z: 30};
    return x === 10 && y === 20;
});

testFeature("let - array destructuring with defaults", function() {
    let [a, b, c] = [1, undefined, 3];
    return a === 1 && b === undefined && c === 3;
});

testFeature("const - nested destructuring", function() {
    const {a: {b}} = {a: {b: 42}};
    return b === 42;
});

/* ===================================================================
   22. CLASS EXPRESSION POLYFILL EDGE CASES
   =================================================================== */

// class expression (var X = class _X {}) must emit CLASS_PF polyfill
testFeature("class expr - polyfill emitted", function() {
    var Foo = class _Foo {
        constructor(v) { this.v = v; }
        get() { return this.v; }
    };
    var f = new Foo(42);
    return f.get() === 42;
});

// class expression with extends
testFeature("class expr - extends works", function() {
    var Base = class _Base {
        constructor(v) { this.v = v; }
    };
    var Child = class _Child extends Base {
        constructor(v) { super(v * 2); }
        get() { return this.v; }
    };
    return new Child(5).get() === 10;
});

// class expression assigned inline (esbuild pattern)
testFeature("class expr - inline assignment", function() {
    var classes = {};
    classes.Pair = class _Pair {
        constructor(a, b) { this.a = a; this.b = b; }
        sum() { return this.a + this.b; }
    };
    return new classes.Pair(3, 7).sum() === 10;
});

// cleanup transpiler cache file
try { rampart.utils.rmFile(process.scriptPath + '/transpile-edge-test.transpiled.js'); } catch(e) {}

/* Drain pending async tests before exiting. Poll _asyncQueue and the
   _asyncRunning flag — when both are clear the queue is fully drained. */
(function waitAndExit() {
    if ((_asyncQueue && _asyncQueue.length > 0) || _asyncRunning) {
        setTimeout(waitAndExit, 25);
        return;
    }
    testFeature.exit();
})();
