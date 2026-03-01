Rampart Transpiler Fixes — February 2026
=========================================

All fixes are in `/extern/tree-sitter/transpiler.c`.

Bug 1: Local variables lost across await points (var hoisting)
--------------------------------------------------------------
From `transpiler-bugs.txt`. Variables declared with `var`/`let`/`const`
inside an async function body became `undefined` after any `await`
because the generator state machine placed each `case` label inside
its own block, and `var` declarations were not hoisted to the
function scope.

**Fix:** Three new functions hoist all variable names to the top of the
generated `_callee$` wrapper:

- `_collect_var_names_recursive()` — walks the AST collecting
  identifier names from `variable_declaration` and
  `lexical_declaration` nodes, stopping at function/class boundaries.
- `_collect_body_var_names()` — drives the recursive walk and returns
  a comma-separated string of names (or NULL).
- `_emit_var_decl_as_assignments()` — emits `var`/`let`/`const`
  declarations inside the state machine as plain assignments
  (`name = value;`), since the `var` has already been hoisted.

The generated output now starts with `var x, obj, ...;` before the
`switch` statement, and the original declarations become bare
assignments. This keeps the variables alive across `yield` points.


Bug 2 & Bug 3: Not reproducible
--------------------------------
Bug 2 (`obj.prop = await fn()` misrouting) and Bug 3 (`.catch()`
reserved-word error) from `transpiler-bugs.txt` could not be
reproduced with the current codebase.


Fix: `this` binding lost across await/yield
--------------------------------------------
Async methods (e.g. `obj.addLater()`) lost their `this` reference
after an `await` because the regenerator `wrap()` function called
`innerFn(context)` without binding `this`.

**Fix:** `wrap()` now takes an `outerThis` parameter and calls
`innerFn.call(outerThis, context)`. The transpiler emits
`, _callee, this)` (async) or `, null, this)` (generators) so the
original `this` value is captured at call time and forwarded into
every state-machine step.


Fix: `_context.sent` error routing in try/catch
-------------------------------------------------
When an `await` inside a `try` block rejected, the error was
delivered via `_context.sent` on the next `.next()` call but was
silently swallowed — the `catch` block never ran.

**Fix:** `_context.sent` is now defined with `Object.defineProperty`
using a getter/setter pair. A `throw` flag (`_t`) and stored error
(`_te`) are set by the new `throw(err)` method on the iterator.
The getter checks the flag and re-throws the error, which the
generated `try/catch` in the state machine catches naturally.

```javascript
Object.defineProperty(context, 'sent', {
  get: function() {
    if (_t) { _t = false; var e = _te; _te = void 0; throw e; }
    return _s;
  },
  set: function(v) { _s = v; },
  configurable: true
});
```


Fix: Concise arrow body with await
------------------------------------
`async (n) => await expr` (arrow with no braces) was transpiled
without a `return` before the final `_context.sent` value, so the
async function resolved to `undefined` instead of the awaited value.

**Fix:** After the await is lowered into the state machine, the code
scans for the last `case N:` label in the emitted output. Everything
after that colon gets a `return ` prefix inserted, so the implicit
arrow return is preserved:

```
case 2: return _context.sent;
         ^^^^^^ inserted
```


Fix: `//` comments inside async/generator bodies
--------------------------------------------------
Line comments (`//`) inside an async or generator function body
corrupted the state machine output. Tree-sitter treats `comment`
nodes as named children of `statement_block`. The transpiler's
statement loop fell through to the `else` branch which copies
source verbatim. Because the transpiler compresses whitespace to
preserve line numbers, a `//` comment and the next `case N:` code
ended up on the same line — the comment consumed the code.

**Fix:** A `comment` handler was added before the other statement-type
checks in both `_build_regenerator_switch_body` (async) and
`_build_regenerator_switch_body_for_yield` (generators). When a `//`
comment is found, it is converted to a `/* */` block comment so it
cannot consume following code:

```
// Do something       →   /* Do something*/
```

Leading whitespace/newlines are preserved so line numbering is
maintained.


Fix: Promise.any([]) polyfill
------------------------------
`Promise.any([])` should reject with an `AggregateError` per spec,
but the polyfill was not handling the empty-array case.

**Fix:** Added an early check: `if (0 === i.length) return o(new n([], 'All promises were rejected'));` where `n` is `AggregateError`.


Fix: ASYNC_PF | PROMISE_PF flag
---------------------------------
When an async function was detected and rewritten, only the
`ASYNC_PF` polyfill flag was set. The Promise polyfill (`PROMISE_PF`)
was not included, so on engines without native `Promise` the
generated code would fail at runtime.

**Fix:** Changed to `*polysneeded |= ASYNC_PF | PROMISE_PF;` so both
polyfills are emitted.


Test coverage
-------------
10 async/await tests were added to `test/transpile-test.js`:

 1. await returns resolved value
 2. sequential awaits preserve order
 3. await works in expression context
 4. rejected await is caught by try/catch
 5. finally runs after successful await
 6. finally runs after rejected await
 7. arrow concise body returns awaited value
 8. this/arguments preserved across await
 9. multiple awaits with reassignment
10. start two promises then await them later

An async test queue (`_asyncQueue` / `_drainAsync`) was added to
`testFeature()` so Promise-returning tests are drained sequentially
and failures are reported correctly. All 153 tests pass with both
Rampart and Node.js.


Fix: Promise polyfill `_immediateFn` timer ordering (Linux)
------------------------------------------------------------
The Promise polyfill used a separate `setTimeout(fn, 0)` call for each
`.then()` callback via `_immediateFn`. On Linux, libevent's timer
min-heap does not guarantee FIFO ordering for equal-timeout events, so
two `setTimeout(fn, 0)` calls could fire in reverse order. This caused
tests like "onFulfilled runs exactly once" to fail because the check
callback ran before the increment callback. On macOS (kqueue backend),
the ordering happened to be FIFO.

**Fix:** Replaced the per-callback `setTimeout` with a microtask queue
that batches all pending callbacks into an array and flushes them in
FIFO order in a single `setTimeout`. This matches the Promise/A+ spec's
microtask semantics:

```javascript
f._immediateFn = (function(){
  var q=[], s=false;
  function fl(){
    var c=q; q=[]; s=false;
    for(var j=0; j<c.length; j++) c[j]();
  }
  return function(e){
    q.push(e);
    if(!s){ s=true; d(fl,0); }
  };
})()
```
