#!/usr/bin/env rampart
"use transpilerGlobally"

/* §8 Phase 5: for-loop let block-scoping with capture-aware wrapping.
   Covers:
   - No captures: optimization (no wrap emitted, behavior unchanged)
   - Captures, no flow-control: simple IIFE wrap (existing)
   - Captures + break/continue/return: sentinel wrap so flow-control crosses
     the synthesized function boundary
   - `this` preservation via .call(this, ...)
   - Labeled break / `arguments` fall through to no-wrap (limitations) */

if (global && global.rampart) {
    rampart.globalize(rampart.utils);
    var _nfailed = 0;
    function testFeature(name, test) {
        var error = false;
        if (typeof test == 'function') {
            try { test = test(); }
            catch (e) { error = e; test = false; }
        }
        printf("testing for-loop scope - %-46s - ", name);
        if (test) printf("passed\n");
        else { printf(">>>>> FAILED <<<<<\n"); _nfailed++; }
        if (error) console.log(error);
    }
} else {
    var testFeature = function (name, test) {
        var error = false;
        if (typeof test == 'function') {
            try { test = test(); }
            catch (e) { error = e; test = false; }
        }
        process.stdout.write("testing node ES2015+ - " + name + " - ");
        if (test) process.stdout.write("passed\n");
        else { process.stdout.write(">>>>> FAILED <<<<<\n"); if (error) console.log(error); process.exit(1); }
    };
    global.printf = function() {};
}

/* ---------------- No captures: simple let bodies ---------------- */

testFeature("for-let sum no closure", function () {
    var sum = 0;
    for (let i = 0; i < 5; i++) sum += i;
    return sum === 10;
});

testFeature("for-let with break, no closure", function () {
    var sum = 0;
    for (let i = 0; i < 10; i++) {
        if (i === 4) break;
        sum += i;
    }
    return sum === 0 + 1 + 2 + 3;
});

testFeature("for-let with continue, no closure", function () {
    var sum = 0;
    for (let i = 0; i < 5; i++) {
        if (i % 2 === 0) continue;
        sum += i;
    }
    return sum === 1 + 3;
});

testFeature("for-let with early return, no closure", function () {
    function fn() {
        for (let i = 0; i < 10; i++) {
            if (i === 3) return i;
        }
        return -1;
    }
    return fn() === 3;
});

/* ---------------- Captures, no flow-control: simple IIFE ---------------- */

testFeature("for-let closure captures (no flow)", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) fns.push(function () { return i; });
    return fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

testFeature("for-let arrow captures (no flow)", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) fns.push(() => i);
    return fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

/* ---------------- Captures + flow-control: sentinel wrap ---------------- */

testFeature("for-let capture + break", function () {
    var fns = [];
    for (let i = 0; i < 5; i++) {
        fns.push(function () { return i; });
        if (i === 2) break;
    }
    return fns.length === 3 && fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

testFeature("for-let capture + continue", function () {
    var fns = [];
    for (let i = 0; i < 5; i++) {
        if (i % 2 === 0) continue;
        fns.push(function () { return i; });
    }
    return fns.length === 2 && fns[0]() === 1 && fns[1]() === 3;
});

testFeature("for-let capture + return", function () {
    var saved = null;
    function fn() {
        for (let i = 0; i < 5; i++) {
            saved = function () { return i; };
            if (i === 2) return "done";
        }
    }
    fn();
    return saved() === 2;
});

testFeature("for-let capture + return value", function () {
    function fn() {
        var captured = null;
        for (let i = 0; i < 5; i++) {
            captured = function () { return i; };
            if (i === 1) return captured;
        }
    }
    var f = fn();
    return f() === 1;
});

testFeature("for-let capture + mix break/continue", function () {
    var fns = [];
    for (let i = 0; i < 10; i++) {
        if (i === 0) continue;
        if (i === 4) break;
        fns.push(function () { return i; });
    }
    return fns.length === 3 && fns[0]() === 1 && fns[1]() === 2 && fns[2]() === 3;
});

/* ---------------- `this` preservation ---------------- */

testFeature("for-let capture + this in method", function () {
    var obj = {
        val: 42,
        run: function () {
            var fns = [];
            for (let i = 0; i < 2; i++) {
                fns.push(function () { return this.val + i; });
                if (i === 1) break;
            }
            return fns.map(function (f) { return f.call(obj); });
        }
    };
    var r = obj.run();
    return r[0] === 42 && r[1] === 43;
});

testFeature("for-let inner this.x access + flow-control", function () {
    function Box(v) { this.v = v; }
    Box.prototype.run = function () {
        var arr = [];
        for (let i = 0; i < 3; i++) {
            arr.push(this.v + i);
            if (i === 1) break;
        }
        return arr;
    };
    var b = new Box(100);
    var r = b.run();
    return r.length === 2 && r[0] === 100 && r[1] === 101;
});

/* ---------------- Multiple loop vars ---------------- */

testFeature("for-let multi-decl capture + break", function () {
    var fns = [];
    for (let i = 0, j = 10; i < 5; i++, j--) {
        fns.push(function () { return i * 100 + j; });
        if (i === 2) break;
    }
    return fns.length === 3 && fns[0]() === 0 * 100 + 10 && fns[2]() === 2 * 100 + 8;
});

/* ---------------- Nested loops ---------------- */

testFeature("inner break doesn't escape outer", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) {
        for (let j = 0; j < 3; j++) {
            if (j === 1) break;   // breaks inner only
            fns.push(function () { return i * 10 + j; });
        }
        if (i === 1) break;        // breaks outer
    }
    /* Each outer iter pushes one fn (inner breaks at j=1, only j=0 pushed)
       Outer breaks at i=1: i=0 push j=0; i=1 push j=0. */
    return fns.length === 2 && fns[0]() === 0 && fns[1]() === 10;
});

/* ---------------- for-of with let + capture ---------------- */

testFeature("for-of let capture (no flow)", function () {
    var fns = [];
    for (let v of [10, 20, 30]) fns.push(function () { return v; });
    return fns[0]() === 10 && fns[1]() === 20 && fns[2]() === 30;
});

/* ---------------- Edge cases ---------------- */

testFeature("for-let return undefined (no expr)", function () {
    function fn(stop) {
        var saved = null;
        for (let i = 0; i < 5; i++) {
            saved = function () { return i; };
            if (i === stop) return;
        }
        return "fallthrough";
    }
    return fn(1) === undefined;
});

testFeature("for-let no capture + return", function () {
    function fn() {
        for (let i = 0; i < 5; i++) {
            if (i === 2) return "early";
        }
        return "done";
    }
    return fn() === "early";
});

testFeature("for-let capture + return inside switch", function () {
    function fn() {
        var fns = [];
        for (let i = 0; i < 5; i++) {
            fns.push(function () { return i; });
            switch (i) {
                case 2: return fns;
            }
        }
        return fns;
    }
    var r = fn();
    return r.length === 3 && r[0]() === 0 && r[1]() === 1 && r[2]() === 2;
});

testFeature("for-let capture + continue inside switch", function () {
    var fns = [];
    for (let i = 0; i < 5; i++) {
        fns.push(function () { return i; });
        switch (i) {
            case 1: continue;   // continue targets the loop, not the switch
            case 3: continue;
        }
    }
    return fns.length === 5 && fns[1]() === 1 && fns[3]() === 3;
});

testFeature("for-let capture + break inside switch only", function () {
    /* break inside switch targets the switch (NOT the outer loop).
       The loop should run to completion. */
    var fns = [];
    for (let i = 0; i < 3; i++) {
        fns.push(function () { return i; });
        switch (i) {
            case 1: break;   // breaks switch, NOT loop
        }
    }
    return fns.length === 3 && fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

/* ---------------- Babel-parity additions ---------------- */

/* a) arguments capture */

testFeature("for-let capture + arguments in body", function () {
    function fn() {
        var fns = [];
        for (let i = 0; i < arguments.length; i++) {
            fns.push(function () { return i; });
            if (arguments[i] === "stop") break;
        }
        return fns;
    }
    var fns = fn("a", "b", "stop", "d");
    return fns.length === 3 &&
           fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

testFeature("for-let capture + return arguments[i]", function () {
    function fn() {
        var captured = null;
        for (let i = 0; i < arguments.length; i++) {
            captured = function () { return i; };
            if (arguments[i] === "target") return captured;
        }
        return null;
    }
    var f = fn("a", "b", "target", "d");
    return f() === 2;
});

/* b) Labeled break/continue */

testFeature("for-let capture + break LABEL (outer)", function () {
    function fn() {
        var fns = [];
        outer: for (var k = 0; k < 5; k++) {
            for (let i = 0; i < 3; i++) {
                fns.push(function () { return i; });
                if (k === 1 && i === 1) break outer;
            }
        }
        return fns;
    }
    var fns = fn();
    /* k=0: 3 closures (i=0,1,2). k=1: 2 closures (i=0,1), then break outer. */
    return fns.length === 5 &&
           fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2 &&
           fns[3]() === 0 && fns[4]() === 1;
});

testFeature("for-let capture + continue LABEL (outer)", function () {
    function fn() {
        var fns = [];
        outer: for (var k = 0; k < 3; k++) {
            for (let i = 0; i < 3; i++) {
                if (i === 1) continue outer;
                fns.push(function () { return k * 10 + i; });
            }
            fns.push(function () { return -1; });  // shouldn't reach here
        }
        return fns;
    }
    var fns = fn();
    /* Outer uses `var k` → all closures see final k=3. For each k iter,
       i=0 pushes, i=1 continues outer. So 3 closures, all with k=3, i=0
       captured (i is let → per-iteration). All return 30 + 0 = 30. */
    return fns.length === 3 &&
           fns[0]() === 30 && fns[1]() === 30 && fns[2]() === 30;
});

testFeature("for-let capture + break to outer-outer label", function () {
    function fn() {
        var fns = [];
        foo: for (var k = 0; k < 3; k++) {
            for (let i = 0; i < 3; i++) {
                fns.push(function () { return i; });
                for (var j = 0; j < 3; j++) {
                    if (i === 1 && j === 2) break foo;
                }
            }
        }
        return fns;
    }
    var fns = fn();
    /* k=0, i=0 push; i=1 push, then inner runs to j=2 → break foo */
    return fns.length === 2 && fns[0]() === 0 && fns[1]() === 1;
});

testFeature("labeled break to inner loop is left alone", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) {
        fns.push(function () { return i; });
        inner: for (var j = 0; j < 5; j++) {
            if (j === 2) break inner;    // targets inner; must NOT propagate
        }
    }
    /* All 3 outer iterations complete. */
    return fns.length === 3 && fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

/* c) return inside nested loop */

testFeature("for-let capture + return inside nested loop", function () {
    function fn() {
        var fns = [];
        for (let i = 0; i < 5; i++) {
            fns.push(function () { return i; });
            for (var j = 0; j < 3; j++) {
                if (i === 2 && j === 1) return fns;
            }
        }
        return fns;
    }
    var fns = fn();
    return fns.length === 3 &&
           fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

testFeature("for-let capture + return undefined inside nested while", function () {
    function fn() {
        var captured = null;
        for (let i = 0; i < 5; i++) {
            captured = function () { return i; };
            var k = 0;
            while (k < 3) {
                k++;
                if (i === 2 && k === 2) return captured;
            }
        }
    }
    var f = fn();
    return f() === 2;
});

/* ---------------- Nested for-let with all three babel-parity features ---------------- */

testFeature("nested for-let, continue outer label crosses wraps", function () {
    function fn() {
        var fns = [];
        outer: for (let k = 0; k < 3; k++) {
            for (let i = 0; i < 3; i++) {
                if (i === 1) continue outer;     // skip pushing at i=1, jump to next k
                fns.push(function () { return k * 10 + i; });
            }
            fns.push(function () { return -1; }); // shouldn't reach (always continued first)
        }
        return fns;
    }
    var fns = fn();
    /* For each k: i=0 push (k*10+0), i=1 → continue outer. So 3 pushes total. */
    return fns.length === 3 &&
           fns[0]() === 0 && fns[1]() === 10 && fns[2]() === 20;
});

testFeature("nested for-let, break outer crosses wraps", function () {
    function fn() {
        var fns = [];
        outer: for (let k = 0; k < 4; k++) {
            for (let i = 0; i < 4; i++) {
                fns.push(function () { return k * 10 + i; });
                if (k === 1 && i === 2) break outer;
            }
        }
        return fns;
    }
    var fns = fn();
    /* k=0: 4 pushes (0,1,2,3). k=1: 3 pushes (10,11,12), then break outer. */
    return fns.length === 7 &&
           fns[0]() === 0 && fns[3]() === 3 &&
           fns[4]() === 10 && fns[6]() === 12;
});

testFeature("nested for-let, return crosses two wraps", function () {
    function fn() {
        var captured = null;
        for (let k = 0; k < 5; k++) {
            for (let i = 0; i < 5; i++) {
                captured = function () { return k * 100 + i; };
                if (k === 1 && i === 2) return captured;
            }
        }
    }
    var f = fn();
    return f() === 102;
});

testFeature("nested for-let, return primitive crosses two wraps", function () {
    function fn() {
        for (let k = 0; k < 5; k++) {
            for (let i = 0; i < 5; i++) {
                if (k === 1 && i === 2) return 42;
            }
        }
        return -1;
    }
    return fn() === 42;
});

testFeature("nested for-let, return undefined crosses two wraps", function () {
    function fn() {
        var saved = "before";
        for (let k = 0; k < 5; k++) {
            for (let i = 0; i < 5; i++) {
                saved = function () { return k * 10 + i; };
                if (k === 1 && i === 2) return;
            }
        }
        return "after";
    }
    return fn() === undefined;
});

if (global && global.rampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-forloop-scope-test.transpiled.js'); } catch (e) {}
    process.exit(_nfailed ? 1 : 0);
}
