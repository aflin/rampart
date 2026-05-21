#!/usr/bin/env rampart
"use transpilerGlobally"

/* Top-level await tests. Each test runs at program top level using `await`
   directly so the transpiler must wrap the program in an async IIFE.
   Dual-mode: rampart + node (node supports TLA natively).

   Tests use `globalScope.<name>` (a per-runtime alias for the appropriate
   global-attaching object) to verify that top-level var/function/class
   remain externally reachable after the IIFE wrap. Under rampart this is
   `global`; under node ESM, modules have their own scope and top-level
   bindings don't attach to globalThis — node test uses module-local
   reads instead. */

if (typeof global !== 'undefined' && global.rampart) {
    rampart.globalize(rampart.utils);
}

var _nfailed = 0;
function testFeature(name, test) {
    var label = (typeof global !== 'undefined' && global.rampart)
        ? "testing tla - "
        : "testing node tla - ";
    var ok = false;
    try { ok = !!test; } catch (e) { /* ok stays false */ }
    var line = label + name;
    while (line.length < 56) line += " ";
    line += " - " + (ok ? "passed" : ">>>>> FAILED <<<<<");
    if (typeof process !== 'undefined' && process.stdout && process.stdout.write) {
        process.stdout.write(line + "\n");
    } else {
        console.log(line);
    }
    if (!ok) {
        _nfailed++;
        if (typeof process !== 'undefined' && process.exit) process.exit(1);
    }
}

/* For node ESM, top-level var/function don't attach to globalThis.
   For rampart, our TLA rewrite explicitly attaches them via `global.X = X`.
   Use a per-runtime check that matches each runtime's actual semantics. */
var globalScope = (typeof global !== 'undefined' && global.rampart) ? global : null;

/* ---------------- Basics ---------------- */

var basic = await Promise.resolve(42);
testFeature("basic - var = await literal", basic === 42);

var chained = await Promise.resolve(await Promise.resolve("chained"));
testFeature("basic - chained await", chained === "chained");

/* ---------------- Global visibility ---------------- */

var globalA = await Promise.resolve("A-value");
testFeature("global - var attaches to global (or module scope)",
    globalScope ? globalScope.globalA === "A-value" : globalA === "A-value");

function tlaHelper(x) { return x * 2; }
testFeature("global - function decl externally reachable",
    globalScope ? (typeof globalScope.tlaHelper === 'function' && globalScope.tlaHelper(5) === 10)
                : (typeof tlaHelper === 'function' && tlaHelper(5) === 10));

class TlaPair { constructor(a, b) { this.a = a; this.b = b; } }
testFeature("global - class decl externally reachable",
    globalScope ? (typeof globalScope.TlaPair === 'function' && new globalScope.TlaPair(1, 2).b === 2)
                : (typeof TlaPair === 'function' && new TlaPair(1, 2).b === 2));

/* ---------------- Control flow with TLA ---------------- */

var ifResult;
if (true) {
    ifResult = await Promise.resolve("if-branch");
}
testFeature("control - await inside if", ifResult === "if-branch");

var loopSum = 0;
for (var ti = 1; ti <= 3; ti++) {
    loopSum += await Promise.resolve(ti);
}
testFeature("control - await inside for loop", loopSum === 6);

var caught = null;
try {
    var bad = await Promise.reject(new Error("expected-err"));
} catch (err) {
    caught = err.message;
}
testFeature("control - await inside try/catch", caught === "expected-err");

/* ---------------- Multiple sequential awaits ---------------- */

var seq1 = await Promise.resolve(10);
var seq2 = await Promise.resolve(seq1 + 5);
var seq3 = await Promise.resolve(seq2 * 2);
testFeature("multi - three sequential awaits", seq1 === 10 && seq2 === 15 && seq3 === 30);

/* ---------------- Function uses TLA-bound variable via closure ---------------- */

var captureValue = await Promise.resolve("captured");
function readCapture() { return captureValue; }
testFeature("closure - function reads TLA-bound var",
    readCapture() === "captured");

var beforeCaptured = await Promise.resolve(captureValue);
testFeature("closure - awaited via free function call", beforeCaptured === "captured");

/* ---------------- Promise.all + TLA ---------------- */

var results = await Promise.all([Promise.resolve("p1"), Promise.resolve("p2"), Promise.resolve("p3")]);
testFeature("multi - Promise.all results", results.length === 3 && results[0] === "p1" && results[2] === "p3");

/* ---------------- Object literal with await in property value ---------------- */

var objWithAwait = { x: await Promise.resolve(7), y: 9, z: await Promise.resolve(11) };
testFeature("expr - await in object literal", objWithAwait.x === 7 && objWithAwait.z === 11);

/* ---------------- Conditional expression with await ---------------- */

var trueBranch = true ? await Promise.resolve("yes") : await Promise.resolve("no");
testFeature("expr - await in conditional", trueBranch === "yes");

if (_nfailed) process.exit(1);
