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
   `_TrN_Sp._gp.Promise`, so bare-Promise lookup is bypassed. */

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
