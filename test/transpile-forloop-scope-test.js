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

var testFeature = new (require('./test-feature.js'))({prefix: "for-loop scope", allowNode: true});

if (testFeature.isRampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-forloop-scope-test.transpiled.js'); } catch(e) {}
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

/* NDE.8 (2026-05-23): body-scoped `let`/`const` declarations inside a
   `for (let i = ...; …)` loop, when captured by a closure, were not
   per-iteration — every closure saw the final iteration's value
   because duktape gives `const`/`let` a single binding for the whole
   function scope rather than per-iteration semantics.  Phase 5
   already wraps each iteration in an IIFE when an init-let is
   captured; the bug was that Phase 5 only checked init-let names for
   captures.  Fix: extend `_bs_for_has_capture`'s name set with
   body-scoped lexical decls collected via `_bs_collect_lexical_decls`
   — the IIFE wrap then triggers and the wrap's fresh function scope
   gives the body-scoped declarations per-iteration semantics
   naturally.  Surfaced in WPT fetch/data-urls (~50 fails). */
testFeature("NDE.8 - body-scoped const captured by closure", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) {
        const v = i * 10;
        fns.push(function () { return v; });
    }
    return fns[0]() === 0 && fns[1]() === 10 && fns[2]() === 20;
});

testFeature("NDE.8 - body-scoped let captured by closure", function () {
    var fns = [];
    for (let i = 0; i < 3; i++) {
        let v = i * 100;
        fns.push(function () { return v; });
    }
    return fns[0]() === 0 && fns[1]() === 100 && fns[2]() === 200;
});

testFeature("NDE.8 - for-of with body-scoped const captured", function () {
    var fns = [];
    for (let x of [1, 2, 3]) {
        const w = x * 1000;
        fns.push(function () { return w; });
    }
    return fns[0]() === 1000 && fns[1]() === 2000 && fns[2]() === 3000;
});

testFeature("NDE.8 - multiple body-scoped const captured (WPT data-urls shape)", function () {
    /* Exact shape from the WPT fetch/data-urls fails: const-extract
       from indexed tests, then closure over the extracted names. */
    var tests = [["a", 1], ["b", 2], ["c", 3]];
    var fns = [];
    for (let i = 0; i < tests.length; i++) {
        const input = tests[i][0], output = tests[i][1];
        fns.push(function () { return input + ":" + output; });
    }
    return fns.map(function (f) { return f(); }).join(",") === "a:1,b:2,c:3";
});

testFeature("NDE.8 - loop-let still captures correctly (regression-guard)", function () {
    /* Variant A from the probe — was already working pre-fix.
       Make sure the fix didn't break Phase 5's existing path. */
    var fns = [];
    for (let i = 0; i < 3; i++) {
        fns.push(function () { return i; });
    }
    return fns[0]() === 0 && fns[1]() === 1 && fns[2]() === 2;
});

/* NDE.9 (2026-05-23): `for (var/let/const [a, b] of iter)` iterated
   zero times when `iter` lacked a `.length` property (Map, Set,
   custom iterable).  The array-pattern for-of lowering in
   `rewrite_for_of_destructuring` used a plain
   `for (_i = 0, _pairs = <right>; _i < _pairs.length; _i++)` header
   that silently terminated immediately when `.length` was undefined.

   Fix: drive the iteration via `Symbol.iterator` when the RHS has
   one (eagerly materializing all values into a temp array before the
   body loop), falling back to array-like indexing for plain Arrays.
   Mirrors the object-pattern branch that was already doing this.
   The eager-materialize design keeps the existing `_loopN` function
   shape + sentinel-based flow-control propagation intact.

   Surfaced in WPT fetch/api/headers iteration (~5 fails). */
testFeature("NDE.9 - array destructure for-of Map", function () {
    var m = new Map([['a',1], ['b',2]]);
    var got = [];
    for (var [k, v] of m) got.push(k + '=' + v);
    return got.length === 2 && got[0] === 'a=1' && got[1] === 'b=2';
});

testFeature("NDE.9 - array destructure for-of Set", function () {
    var s = new Set([['x','1'], ['y','2']]);
    var got = [];
    for (let [k, v] of s) got.push(k + ':' + v);
    return got.join(',') === 'x:1,y:2';
});

testFeature("NDE.9 - array destructure for-of plain array (regression-guard)", function () {
    /* Was already working pre-fix; ensure the iterator-protocol
       rewrite still handles the .length fallback case. */
    var arr = [['a',1], ['b',2], ['c',3]];
    var got = [];
    for (const [k, v] of arr) got.push(k + ':' + v);
    return got.join(',') === 'a:1,b:2,c:3';
});

testFeature("NDE.9 - array destructure for-of custom Symbol.iterator", function () {
    var src = {
        _entries: [['foo','a'], ['bar','b']],
        [Symbol.iterator]: function () {
            var arr = this._entries, i = 0;
            return { next: function () {
                return i < arr.length
                    ? { value: arr[i++], done: false }
                    : { value: undefined, done: true };
            }};
        }
    };
    var got = [];
    for (var [k, v] of src) got.push(k + '=' + v);
    return got.length === 2 && got[0] === 'foo=a' && got[1] === 'bar=b';
});

testFeature("NDE.9 - array destructure for-of inside function preserves `this`", function () {
    /* The `_loopN.call(this)` wrap had to be preserved.  Verify by
       calling a method that uses `this` inside a destructured for-of. */
    var obj = {
        scale: 10,
        run: function () {
            var out = [];
            for (var [k, v] of new Map([['a',1], ['b',2]])) {
                out.push(k + ':' + (v * this.scale));
            }
            return out;
        }
    };
    var r = obj.run();
    return r.length === 2 && r[0] === 'a:10' && r[1] === 'b:20';
});

testFeature("NDE.9 - array destructure for-of with break", function () {
    /* Sentinel-based break/continue flow control should still
       propagate correctly. */
    var m = new Map([['a',1], ['b',2], ['c',3]]);
    var got = [];
    for (var [k, v] of m) {
        if (k === 'c') break;
        got.push(k + v);
    }
    return got.length === 2 && got[0] === 'a1' && got[1] === 'b2';
});

/* NDE.9b (2026-05-23): the NDE.9 fix's first attempt pre-buffered
   the iterator into a temp array before any body iteration ran.
   That violates spec lazy-iteration semantics — mutations to a live
   iterable during the body were invisible because the iterator had
   already been fully drained.  WPT headers-basic
   Iteration-skips/Removing/Appending/Prepending tests rely on
   per-step lazy iteration.  Fix: advance the iterator inside the
   loop header, one step per body run.  See NDE.9b in
   transpiler-todo.md. */
testFeature("NDE.9b - iterator advances lazily (per body step, not upfront)", function () {
    var calls = 0;
    var iter = {
        [Symbol.iterator]: function () {
            return {
                next: function () {
                    calls++;
                    if (calls === 1) return {value: ['a', 1], done: false};
                    if (calls === 2) return {value: ['b', 2], done: false};
                    return {value: undefined, done: true};
                }
            };
        }
    };
    var sequence = [];
    for (var [k, v] of iter) {
        /* Record (k, v) AND how many next() calls have happened so
           far.  Lazy iteration: calls===1 on first body run, ===2 on
           second.  Pre-buffered (NDE.9b bug): both body runs see
           calls===3 because the iterator was fully drained. */
        sequence.push(k + v + '@' + calls);
    }
    return sequence.length === 2
        && sequence[0] === 'a1@1'
        && sequence[1] === 'b2@2';
});

testFeature("NDE.9b - mutation during loop body affects later iterations", function () {
    /* The headers-basic style test: an iterable whose contents
       change while we iterate.  After NDE.9b fix, the next .next()
       call sees the mutated state. */
    var data = [['a', 1], ['b', 2], ['c', 3]];
    var consumed = 0;
    var live = {
        [Symbol.iterator]: function () {
            return {
                next: function () {
                    if (consumed >= data.length)
                        return {value: undefined, done: true};
                    return {value: data[consumed++], done: false};
                }
            };
        }
    };
    var got = [];
    for (var [k, v] of live) {
        got.push(k + v);
        /* Remove the next entry mid-iteration.  After 'a1' the
           next pending item is 'b'.  We splice it out — so the
           next .next() call should now return 'c'. */
        if (k === 'a') data.splice(consumed, 1);   /* remove 'b' */
    }
    return got.length === 2 && got[0] === 'a1' && got[1] === 'c3';
});

if (testFeature.isRampart) {
    try { rampart.utils.rmFile(process.scriptPath + '/transpile-forloop-scope-test.transpiled.js'); } catch (e) {}
}
testFeature.exit();
