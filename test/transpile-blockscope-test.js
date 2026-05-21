#!/usr/bin/env rampart
"use transpilerGlobally"

/* Block-scope tests for §8 (babel-style let/const rename in nested blocks).
   See transpiler-todo.md §8. */

var testFeature = new (require('./test-feature.js'))({prefix: "blockscope", allowNode: true});

/* Drop stale .transpiled.js so we always test fresh transpiler output. */
if (testFeature.isRampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-blockscope-test.transpiled.js'); } catch(e) {}
}

/* ---------------- Basic shadowing in function scopes ---------------- */

testFeature("let shadows param in block", function () {
    function fn(x) {
        if (x > 0) {
            let x = 99;
            return x;
        }
        return x;
    }
    return fn(5) === 99 && fn(-1) === -1;
});

testFeature("let shadows function decl", function () {
    function fn() {
        function helper() { return "outer-helper"; }
        if (true) {
            let helper = "inner-helper";
            return helper;
        }
    }
    return fn() === "inner-helper";
});

testFeature("collision with existing _name", function () {
    function fn(x) {
        var _x = 100;
        if (x > 0) {
            let x = 50;
            return _x + x;
        }
    }
    return fn(1) === 150;
});

testFeature("triple-nested same-name shadow", function () {
    function fn(v) {
        var r = "";
        r += v + ",";
        if (1) {
            let v = "B";
            r += v + ",";
            if (1) {
                let v = "C";
                r += v + ",";
            }
            r += v + ",";
        }
        r += v;
        return r;
    }
    return fn("A") === "A,B,C,B,A";
});

testFeature("shorthand property with shadow", function () {
    function fn(suffix) {
        if (true) {
            let name = "x-" + suffix;
            return { name };
        }
    }
    return fn("a").name === "x-a";
});

testFeature("shorthand: multiple shadow keys", function () {
    function fn(a, b) {
        if (true) {
            let a = "inner-a";
            let b = "inner-b";
            return { a, b };
        }
    }
    var r = fn("OA", "OB");
    return r.a === "inner-a" && r.b === "inner-b";
});

testFeature("no rename when no conflict", function () {
    function fn() {
        if (true) {
            let q = 42;
            return q;
        }
    }
    return fn() === 42;
});

/* ---------------- Free-reference cases (M3) ---------------- */

testFeature("arrow free-ref to closure-captured var", function () {
    function outer() {
        var x = "outer";
        var arrow = (y) => {
            if (y) {
                let x = "arrow-shadow";
                return x;
            }
            return x;
        };
        return arrow(1) + "/" + arrow(0);
    }
    return outer() === "arrow-shadow/outer";
});

testFeature("function expr free-ref to outer var", function () {
    function outer() {
        var x = "outer";
        var fn = function (y) {
            if (y) {
                let x = "inner";
                return x;
            }
            return x;
        };
        return fn(1) + "/" + fn(0);
    }
    return outer() === "inner/outer";
});

/* ---------------- Arrow function block-scope ---------------- */

testFeature("arrow body shadow", function () {
    var fn = (x) => {
        if (x > 0) {
            let x = 99;
            return x;
        }
        return x;
    };
    return fn(5) === 99 && fn(-1) === -1;
});

testFeature("nested arrow with own scope", function () {
    var fn = (n) => {
        let n2 = n * 2;
        var inner = (n) => {
            let n2 = n * 3;
            return n2;
        };
        return n2 + inner(n);
    };
    return fn(4) === 8 + 12;
});

testFeature("destructured arrow param + body shadow", function () {
    var fn = ({a, b}) => {
        if (a > 0) {
            let a = 99;
            return a + b;
        }
        return a + b;
    };
    return fn({a: 1, b: 2}) === 101;
});

testFeature("arrow array-destructured first param + other params", function () {
    /* The luxon-blocking pattern (from .reduce). */
    var fn = ([sofar, current], item) => {
        if (!current) return [sofar, item];
        return [sofar.concat([current]), item];
    };
    var r = fn([[], "prev"], "next");
    return r[0].length === 1 && r[0][0] === "prev" && r[1] === "next";
});

testFeature("arrow object-destructured first param + others", function () {
    var fn = ({a, b}, c) => a + b + c;
    return fn({a: 1, b: 2}, 100) === 103;
});

testFeature("arrow destructure-with-default second param", function () {
    var fn = (length = "long", {locale = null, count = 0} = {}) =>
        length + ":" + locale + ":" + count;
    return fn() === "long:null:0" &&
           fn("short", {count: 5}) === "short:null:5";
});

testFeature("arrow multi-destructure concise body", function () {
    var fn = ({x}, [a, b], y) => x + a + b + y;
    return fn({x: 1}, [10, 20], 100) === 131;
});

/* ---------------- ES2022 private class fields/methods ---------------- */

testFeature("private class field", function () {
    /* Lowered to _priv_x property; no privacy enforcement but the
       value lives where the class methods can reach it. */
    function build() {
        var src = 'class C { #x = 42; get value() { return this.#x; } }';
        eval(src + 'var c = new C(); global.__t_priv_v = c.value;');
        return global.__t_priv_v === 42;
    }
    if (typeof eval === 'undefined') return true;
    return build();
});

testFeature("private class method", function () {
    function build() {
        var src = 'class C { #greet() { return "hi"; } shout() { return this.#greet().toUpperCase(); } }';
        eval(src + 'var c = new C(); global.__t_priv_m = c.shout();');
        return global.__t_priv_m === 'HI';
    }
    if (typeof eval === 'undefined') return true;
    return build();
});

testFeature("private field referenced in another field initializer", function () {
    /* The marked-blocking pattern. */
    function build() {
        var src = 'class C { #base = 10; total = this.#base + 5; }';
        eval(src + 'var c = new C(); global.__t_priv_f = c.total;');
        return global.__t_priv_f === 15;
    }
    if (typeof eval === 'undefined') return true;
    return build();
});

/* ---------------- Method definitions ---------------- */

testFeature("method with shadow in inner arrow", function () {
    class C {
        method(x) {
            var arrow = (y) => {
                if (y > 0) {
                    let x = 99;
                    let y = 88;
                    return x + y;
                }
                return x + y;
            };
            return arrow(5);
        }
    }
    return new C().method(1) === 187;
});

testFeature("method shadow in switch case", function () {
    class C {
        m(x) {
            switch (x) {
                case 1: {
                    let r = "one";
                    return r;
                }
                case 2: {
                    let r = "two";
                    return r;
                }
                default:
                    return "other";
            }
        }
    }
    var c = new C();
    return c.m(1) === "one" && c.m(2) === "two" && c.m(3) === "other";
});

/* ---------------- Property safety ---------------- */

testFeature("does not rename obj.x property", function () {
    function fn(x) {
        var obj = { x: 1, y: 2 };
        if (x > 0) {
            let x = 99;
            return obj.x + x;
        }
    }
    return fn(1) === 100;
});

testFeature("does not rename pair key", function () {
    function fn(x) {
        if (x > 0) {
            let x = 99;
            return { x: x }.x;
        }
    }
    return fn(1) === 99;
});

/* ---------------- For-loop block-scope (existing path) ---------------- */

testFeature("for-let with closure capture", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) {
        fns.push(function () { return i; });
    }
    return fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

testFeature("for-of with let", function () {
    var sum = 0;
    for (let v of [1, 2, 3]) {
        sum += v;
    }
    return sum === 6;
});

testFeature("for-in with let", function () {
    var keys = [];
    for (let k in {a: 1, b: 2}) {
        keys.push(k);
    }
    return keys.length === 2;
});

/* ---------------- const cases (treated as let for shadow purposes) ---------------- */

testFeature("const shadow in nested block", function () {
    function fn(x) {
        if (x > 0) {
            const x = 99;
            return x;
        }
        return x;
    }
    return fn(5) === 99;
});

testFeature("mixed let/const shadow", function () {
    function fn(name) {
        const result = (function () {
            if (true) {
                let name = "inner";
                return name;
            }
        })();
        return result + "/" + name;
    }
    return fn("outer") === "inner/outer";
});

/* ---------------- Edge cases ---------------- */

testFeature("let in deep nested if-chain", function () {
    function fn(x) {
        if (x > 0) {
            if (x > 1) {
                if (x > 2) {
                    let x = "deep";
                    return x;
                }
            }
        }
        return "shallow";
    }
    return fn(3) === "deep" && fn(1) === "shallow";
});

testFeature("multiple lets same block, different names", function () {
    function fn(x) {
        if (x > 0) {
            let a = x * 2;
            let b = x * 3;
            return a + b;
        }
    }
    return fn(4) === 20;
});

testFeature("let with no shadow stays let", function () {
    function fn() {
        if (true) {
            let unique = 42;
            return unique;
        }
    }
    return fn() === 42;
});

testFeature("multiple sibling block lets", function () {
    function fn() {
        var results = [];
        if (true) {
            let v = "A";
            results.push(v);
        }
        if (true) {
            let v = "B";
            results.push(v);
        }
        return results.join(",");
    }
    return fn() === "A,B";
});

/* ---------------- Exit ---------------- */

if (testFeature.isRampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-blockscope-test.transpiled.js'); } catch (e) {}
}
testFeature.exit();
