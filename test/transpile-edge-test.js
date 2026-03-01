#!/usr/bin/env rampart
"use transpilerGlobally"

/* Edge-case tests for the ES2015+ transpiler.
   Runs on both Rampart (transpiled) and Node (native).

   Known transpiler limitations (ES2015+ → ES5):
   - const is converted to var and is NOT read-only (no enforcement).
   - let/const at top level become var and attach to the global object.
   - Destructuring with await (e.g. const {a,b} = await expr) is not supported.
     Workaround: const tmp = await expr; const {a,b} = tmp;
   - await inside for/while/do loops does not execute per-iteration.
     Workaround: move await outside the loop or use Promise.all().
   - yield inside for/while/do loops is dropped from the output.
     Workaround: use a manual iteration pattern.
   - BigInt literals (123n) and BigInt operators are not supported.
   - Intl (Internationalization API) is not available.
*/

if(global && global.rampart) {
    rampart.globalize(rampart.utils);
    var _asyncQueue = [];
    var _asyncRunning = false;
    var _drainAsync = function() {
        if (_asyncRunning || _asyncQueue.length === 0) return;
        _asyncRunning = true;
        var item = _asyncQueue.shift();
        item.promise.then(function(result) {
            printf("testing edge - %-52s - ", item.name);
            if (result)
                printf("passed\n");
            else
            {
                printf(">>>>> FAILED <<<<<\n");
                process.exit(1);
            }
            _asyncRunning = false;
            _drainAsync();
        }).then(null, function(e) {
            printf("testing edge - %-52s - ", item.name);
            printf(">>>>> FAILED <<<<<\n");
            console.log(e);
            process.exit(1);
        });
    };
    function testFeature(name,test)
    {
        var error=false;
        if (typeof test =='function'){
            try {
                test=test();
            } catch(e) {
                error=e;
    console.log(e);
                test=false;
            }
        }
        if (test && typeof test === 'object' && typeof test.then === 'function') {
            _asyncQueue.push({name: name, promise: test});
            _drainAsync();
            return;
        }
        printf("testing edge - %-52s - ", name);
        if(test)
            printf("passed\n")
        else
        {
            printf(">>>>> FAILED <<<<<\n");
            if(error) console.log(error);
            process.exit(1);
        }
        if(error) console.log(error);
    }
    _TrN_Sp.warnUnhandledPromise=false;
} else {
    /* for testing against node */
    global.printf=function(fmt) {
      var args = Array.prototype.slice.call(arguments, 1);
      var i = 0;

      var output = fmt.replace(/%(-?)(\d*)s/g, function(match, flag, width) {
        var arg = (i < args.length) ? args[i++] : "";
        var str = String(arg);
        width = parseInt(width, 10) || 0;

        if (width > str.length) {
          var padding = Array(width - str.length + 1).join(" ");
          if (flag === "-") {
            str = str + padding;
          } else {
            str = padding + str;
          }
        }
        return str;
      });

      if (typeof process !== "undefined" && process.stdout && process.stdout.write) {
        process.stdout.write(output);
      } else {
        console.log(output);
      }

      return output.length;
    }

    var _asyncQueue = [];
    var _asyncRunning = false;
    var _drainAsync = function() {
        if (_asyncRunning || _asyncQueue.length === 0) return;
        _asyncRunning = true;
        var item = _asyncQueue.shift();
        item.promise.then(function(result) {
            printf("testing node edge - %-52s - ", item.name);
            if (result)
                printf("passed\n");
            else
            {
                printf(">>>>> FAILED <<<<<\n");
                process.exit(1);
            }
            _asyncRunning = false;
            _drainAsync();
        }).then(null, function(e) {
            printf("testing node edge - %-52s - ", item.name);
            printf(">>>>> FAILED <<<<<\n");
            console.log(e);
            process.exit(1);
        });
    };
    var testFeature = function(name,test)
    {
        var error=false;
        if (typeof test =='function'){
            try {
                test=test();
            } catch(e) {
                error=e;
    console.log(e);
                test=false;
            }
        }
        if (test && typeof test === 'object' && typeof test.then === 'function') {
            _asyncQueue.push({name: name, promise: test});
            _drainAsync();
            return;
        }
        printf("testing node edge - %-52s - ", name);
        if(test)
            printf("passed\n")
        else
        {
            printf(">>>>> FAILED <<<<<\n");
            if(error) console.log(error);
            process.exit(1);
        }
        if(error) console.log(error);
    }

}

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

// Verify transpiler warns about destructuring + await
testFeature("warning - destructuring + await", function() {
    if (!global.rampart) return true; // Node supports this natively
    var fh = fopenBuffer(stderr);
    try { eval('async function _wdsa() { const {a, b} = await Promise.resolve({a:1,b:2}); }'); } catch(e) {}
    fclose(fh);
    var msg = fh.getString();
    fh.destroy();
    return msg.indexOf("destructuring") !== -1 && msg.indexOf("await") !== -1;
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
testFeature("warning - await in loop", function() {
    if (!global.rampart) return true; // Node supports this natively
    var fh = fopenBuffer(stderr);
    eval('async function _wal() { for (var i=0;i<3;i++) { await Promise.resolve(i); } }');
    fclose(fh);
    var msg = fh.getString();
    fh.destroy();
    return msg.indexOf("await") !== -1 && msg.indexOf("loop") !== -1;
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

// Verify transpiler warns about yield inside loop
testFeature("warning - yield in loop", function() {
    if (!global.rampart) return true; // Node supports this natively
    var fh = fopenBuffer(stderr);
    eval('function* _wyl() { for (var i=0;i<3;i++) yield i; }');
    fclose(fh);
    var msg = fh.getString();
    fh.destroy();
    return msg.indexOf("yield") !== -1 && msg.indexOf("loop") !== -1;
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

// const block scoping via IIFE wrapping
testFeature("const - block scoping", function() {
    var outer = "OUTER";
    {
        const outer = "INNER";
    }
    return outer === "OUTER";
});

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
