# Transpiler TODO / Known Gaps

Snapshot of known gaps in `extern/tree-sitter/transpiler.c` and its
interaction with duktape. Captured after the `fn-source` work
(May 2026 branch). Items are roughly in order of "how often you'd
trip over it."

## Tier A library testing — round 2 (continued debugging)

After the initial pass that fixed three bugs, continued probing of each
lib's failure surfaced more issues. Most were fixed; some are open.

### Fixed in this round

- **Object-spread with comments collapsing the line** *(fixed)*. The
  spread rewriter (`rewrite_array_spread`) iterated all named children
  of an object literal and emitted each as a property — including
  `comment` nodes. The output collapses to a single line, so a `//
  line comment` ends up consuming subsequent properties.
  *Surfaced by*: marked (`{ ...other, html: edit(…), def: …, fences:
  noopTest, // fences not supported, lheading: …`).
  *Fix*: skip `comment` children in both the size-pass and emit-pass
  loops of `rewrite_array_spread`.

- **`export default class/function` not detected** *(fixed)*. The
  detection `is_default_export = !ts_node_is_null(
  ts_node_child_by_field_name(snode, "default", 7))` always returned
  null because `default` is an unnamed keyword token in
  tree-sitter-javascript, not a field. Result: classes/functions
  exported with `export default` were emitted as named exports
  (`exports.ClassName = ClassName`) instead of `module.exports =
  ClassName`, breaking `_interopDefault` import paths.
  *Surfaced by*: luxon, which has many `export default class Foo {}`.
  *Fix*: walk unnamed children and match the byte range "default".

- **Async class methods not lowered** *(fixed)*. `es5_emit_class_core`
  detected `static`/`get`/`set` modifiers but not `async`. Result:
  `async parseAsync() { await x; }` emitted as a regular `function`
  with `await` in the body — parse error.
  *Surfaced by*: commander.
  *Fix*: detect `async` in the same modifier-scan loop, and when
  present, emit the method value as
  `function name() { return _TrN_Sp.asyncToGenerator(
  _TrN_Sp.regeneratorRuntime.mark(function _callee(params) {
  <switch> })).apply(this, arguments); }`.

- **Method shorthand with ES2015+ params** *(fixed conditionally)*. The
  comment in `rewrite_plain_method_shorthand` said "duktape supports
  method shorthand" — true for plain identifiers, false for rest /
  default / destructure params. Now: detect those param features and
  force `name: function(params)` form so function-param rewriters can
  fire. Plain shorthand still passes through unchanged (avoids
  regressing the minify-test getter/setter suite).
  *Surfaced by*: rxjs (`setTimeout(handler, timeout, ...args) {}`).

- **`method_definition` not in function-param-rewriter dispatch**
  *(fixed)*. The dispatch only listed function/generator types.
  Combined with the method-shorthand fix above, this lets default/
  rest/destructure params be lowered for object-literal methods.

- **Comments between function params** *(fixed)*.
  `build_param_default_inits` iterates named children and bails on
  anything that isn't `identifier` or `assignment_pattern`. Comments
  are named children too, so they hit the bail-out — and the entire
  default-param rewrite was canceled when comments were sprinkled
  between params.
  *Surfaced by*: ajv (`addSchema(schema, // …\n key, // …\n
  _validateSchema = this.opts.validateSchema // …)`).
  *Fix*: skip `comment` children in the iteration (and don't
  increment the arg index counter for them).

### Still open

- **Marked uses ES2022 private class methods** (`this.#parseMarkdown(…)`).
  Documented in §5 already. Will need a real rewriter pass — probably
  rewrite `#name` to a per-instance WeakMap or a name-mangled
  `_$name` property.

- **Luxon: arrow with multi-param destructure** —
  `(['a', 'b'], item) => …`. The arrow rewriter handles single-param
  destructure inline but bails for multi-param. Tried reordering the
  function-param dispatch ahead of the arrow rewriter; the destructure
  rewriter's injection-at-body-start logic doesn't work for concise-
  body arrows (no `{` brace), corrupting other tests. Real fix: extend
  arrow rewriter to handle each destructured param with a generated
  temp + body decl, like the single-param case.

- **Ajv: parse-error after polyfill prefix on full core.js**. The
  errmsg points at `"use strict";` after the polyfill load, but that
  position is in the middle of a giant single-line polyfill prefix —
  the `walk-back-to-prev-newline` errmsg builder scans back through
  the whole polyfill, so the reported context isn't where the actual
  error is. Bisecting line ranges of ajv core.js worked for portions
  but not the full file. Needs a way to localize the actual error
  position; can't be done from the errmsg as printed.

- **RxJS: parse error in lowered generator (line 4 of
  `internal/util/isReadableStreamLike.js`)**. The async-generator
  combination (`async function* foo() { yield __await(stream.read()) }`)
  produces a regenerator state machine that doesn't quite work. Likely
  the same multi-state-temp issue we already fixed for top-level
  awaits, but in the async-generator emit path which isn't using the
  shared helper.

- **Commander: blocked on `require('events')`**. Not a transpiler bug.
  Rampart's `require` doesn't shim Node's built-in `events` module.
  Would need either a `node_modules`-style resolver or a shim file
  in `process.modulesPath`.

- **Chalk: blocked on `require('ansi-styles')`**. Same family as
  commander — bare-specifier dep resolution. Could be fixed by
  shimming `ansi-styles.js` etc. in `modules/`.

### Status summary

| Lib       | Status               |
|-----------|----------------------|
| chalk     | needs module resolution (bare-spec deps) |
| picomatch | **OK** |
| commander | needs Node `events` shim |
| marked    | ES2022 private class methods (open) |
| luxon     | arrow multi-param destructure (open) |
| immer     | **OK** |
| rxjs      | async-generator lowering bug (open) |
| ajv       | parse error, unlocalized (open) |
| date-fns  | **OK** |

3 of 9 load cleanly; the rest each have a specific identified issue
or non-transpiler blocker.

---

## Findings from Tier A real-world library testing

Loaded each Tier A lib (`run-tier-a.js` in
`build/src/transpiler-libs/`) under `"use transpilerGlobally"`. Below
is a catalog of issues surfaced. Three were patched as we found them
(marked **fixed**); the rest are open.

### Heap corruption: arrow with object-literal default param  *(fixed)*
**Repro**: `(a, b = {}) => a`
**Symptom**: `double free or corruption` / `munmap_chunk(): invalid pointer`.
**Root cause**: in `rewrite_arrow_function_node`, the post-rewrite
default-init injection used `strchr(rep, '{')` to find the body's
opening brace, but for any arrow whose param has an object-literal
default (`b = {}`) the first `{` in the rewritten `rep` is the
*default value's* brace, not the body's. The pointer arithmetic then
underflows and `memcpy` writes far out-of-bounds.
**Fix**: `strchr(pc, '{')` where `pc` is the closing `)` of the
parameter list.
**Surfaced by**: picomatch, chalk (any lib using
`(input, state = {}) => …` idioms).

### Destructuring pattern with embedded comment doesn't lower  *(fixed)*
**Repro**:
```js
const { A, B /* note */, C } = obj;
```
**Symptom**: emitted output is `var { A, B /* note */, C } = obj;` —
which duktape rejects ("invalid variable declaration").
**Root cause**: `collect_flat_destructure_bindings` iterates the
pattern's named children and returns 0 on any unknown type. A
`comment` is a named child, was hitting the bail-out, which canceled
the destructure rewrite (only the `const` → `var` part survived).
**Fix**: skip `comment` nodes in both array_pattern and object_pattern
loops.
**Surfaced by**: picomatch (`lib/scan.js` has a constants-import
destructure with `/* * */` comments on every line).

### Class method named with a reserved word produces invalid output  *(fixed)*
**Repro**:
```js
class Foo {
    default(v) { return v; }
}
```
**Symptom**: emitted output is
`{key:'default', value: function default(v) { … }}` — invalid as a
function-expression name.
**Root cause**: `es5_emit_class_core` always emits
`function <name>(…)`, which fails when `<name>` is a JS keyword.
Method names DO permit keywords syntactically; function-expression
names DO NOT.
**Fix**: new helper `_name_is_reserved()` checks against the keyword
list; when true, emit the function expression anonymously (the `key:`
field still carries the name).
**Surfaced by**: commander (`Argument.default()`), immer
(`Patches.default` somewhere — error was at line 991).

### Async class method's `await` not lowered  *(open)*
**Repro**:
```js
class C {
    async foo() { await x(); }
}
```
**Symptom**: emitted output is
`{key:'foo', value: function foo() { var u = this._x(); await … }}`
— `await` appears in a non-async function. Parse error.
**Root cause**: `es5_emit_class_core` doesn't see method-level
`async` modifier. The standalone `rewrite_async_await_to_regenerator`
runs on `method_definition` nodes but the class rewriter claims the
whole class range first, so it doesn't get to fire on inner methods.
**Fix path**: in `es5_emit_class_core`'s method loop, detect the
`async` modifier the same way `static`/`get`/`set` are detected;
when present, build the regenerator wrap inline (similar to how
generator methods are handled), or invoke the async lowering helpers
to produce the wrapped body.
**Surfaced by**: commander (`Command.parseAsync`).

### Object-literal method shorthand with ES2015+ params  *(open)*
**Repro**:
```js
var obj = {
    setTimeout(handler, timeout, ...args) { … }
};
```
**Symptom**: shorthand passes through duktape's parser only when
params are plain identifiers. With rest (`...args`), default (`a=1`),
or destructure (`{a,b}`) — duktape rejects: "expected identifier".
**Root cause**: `rewrite_plain_method_shorthand` deliberately skips
all method shorthand except `get`/`set` ("works in duktape"); the
function-param rewriters (`rewrite_function_rest`,
`rewrite_function_like_default_params`,
`rewrite_function_destructuring_params`) only dispatch on
`function_declaration`/`function_expression`/etc., not
`method_definition`.
**Fix path**: either always convert method shorthand to
`name: function(params)` so the function-expression rewriters catch
the params on a later pass, OR add `method_definition` to the
function-param rewriters' dispatch types.
**Surfaced by**: rxjs (`timeoutProvider.setTimeout(...args) {}`).

### `_interopRequireWildcard` missing at runtime  *(open)*
**Repro**: any lib that uses ESM `import * as ns from "mod"`.
**Symptom**: `TypeError: undefined not callable (property
'_interopRequireWildcard' of [object Object])`.
**Root cause**: the import rewriter emits
`_TrN_Sp._interopRequireWildcard(require(...))` but the helper is
only defined inside the `IMPORT_PF` polyfill string. For some lib's
namespace-import path the IMPORT_PF flag isn't being set, or the
polyfill isn't being prepended in the pass that emits the call.
**Surfaced by**: luxon (`src/luxon.js` has `import * as datetime`
patterns) — but the lib itself uses CJS-style `require` at the top
level too; the trigger needs more investigation.

### Marked emits invalid `_TrN_Sp.__spreadO` chain  *(open)*
**Symptom**: error fragment shows `var blockPedantic =
_TrN_Sp._newObject()._addchain(_TrN_Sp.__spreadO({},blockNormal))._concat({html: …})`
— looks like spread/object-rest interaction with the runtime
helpers.
**Root cause**: not investigated; probably an interaction between
object spread and method-chaining via `_addchain`/`_concat`.
**Surfaced by**: marked (`lib/marked.esm.js`).

### Ajv: long error fragment that starts with the polyfill preamble  *(open)*
**Symptom**: error message includes the polyfill prefix text,
suggesting the parser failed inside or right after the polyfill
emission.
**Root cause**: not investigated. Could be the double-preamble issue
noted in §3, could be something specific to ajv's runtime-code-gen
(`new Function(...)` for generated validators).
**Surfaced by**: ajv (`dist/core.js`).

### Module resolution: bare specifiers don't work in test setup
Not a transpiler bug per se, but worth noting: rampart's `require`
resolves bare specifiers (`require("ansi-styles")`) against
`process.modulesPath`, which is `build/src` in our test setup. It
doesn't walk up looking for a `node_modules/`-style hierarchy, so
chalk's `require('ansi-styles')` failed with `Could not resolve
module id ansi-styles`. To make these libs loadable for testing we'd
need to either flatten dep `.js` files into `modulesPath`, set up
symlinks, or add `node_modules` walking to rampart's resolver.
**Surfaced by**: chalk, plus would affect rxjs (`tslib`), ajv
(several).

### Summary

After fixes-as-we-went, the Tier A status is:

| Lib       | Status               |
|-----------|----------------------|
| chalk     | needs module resolution (bare-spec deps) |
| picomatch | **OK** |
| commander | async class method (open) |
| marked    | spread/chain helper interaction (open) |
| luxon     | _interopRequireWildcard missing (open) |
| immer     | **OK** |
| rxjs      | method shorthand + rest params (open) |
| ajv       | post-polyfill parse error (open) |
| date-fns  | **OK** |

3 of 9 load cleanly. The two transpiler-output-correctness bugs
(reserved-word method name, comment-in-pattern) and the heap
corruption (arrow object-default param) were all real bugs caught
by these libraries that wouldn't have surfaced in our hand-written
tests.

---

## Recently fixed

- **Destructuring with `await`** (`const {a, b} = await x`) — added a
  dedicated emitter in the async rewriter
  (`_emit_destructure_await_lower`) that lowers to a temp var via the
  regenerator state machine then expands the destructure pattern as
  plain assignments. Covers object/array patterns, defaults, renaming,
  and `var`/`let`/`const` variants.

- **Multi-declarator forms** — `const a = 1, {b} = await x;`,
  `const {a} = await x, b = 1;`, `let [a] = arr, b = await x;`, etc.
  Broadened `_stmt_is_destructure_await` to trigger whenever any
  declarator has a destructure pattern AND any declarator has await
  (the two can be the same declarator or different declarators). The
  per-declarator loop in the emitter handles each by its own
  (pattern, await) state — including sync destructure (temp + expand
  with no state machine), plain-await (state machine, identifier
  binding), and plain (assignment without keyword).

- **Embedded await positions** — extracted `_emit_value_awaits_lower`
  as a shared helper. Each await in the value expression gets its own
  `_context._ts<N>` slot (stored on the persistent context object, not
  a local var — locals get re-hoisted to `undefined` on every entry to
  `_callee$` and lose values from prior cases). This handles:
  - single embedded awaits — `(await x).y`, `arr[await x]`, `fn(await x)`
  - short-circuit chains — `(await x) || default`
  - sibling awaits — `fn(await a, await b)`
  - conditional with awaits — `cond ? (await a) : (await b)`
  - chained calls — `(await fn)(await x)`
  - destructure-assignment variants of all of the above

  Same helper is now used by both `_emit_destructure_await_lower` and
  `_emit_destructure_assignment_await_lower`.

  **Still TODO**: nested awaits like `await fn(await g())` — same
  limitation as the existing `_emit_stmt_async_lower`, because
  `_collect_awaits_shallow` doesn't recurse into an outer await's
  argument. A multi-pass lowering or a recursive collector would be
  needed.

- **Destructure-assignment with `await`** (`({a, b} = await x);`) — added
  a parallel emitter (`_emit_destructure_assignment_await_lower`) for
  the no-declaration form. Same coverage matrix as the declaration
  variant; the only difference is no `var` on the binding names since
  they already exist.

- **Test framework: async tests now drain** — `transpile-edge-test.js`
  previously called `process.exit()` immediately after the synchronous
  body, silently dropping any Promise-returning test result. Replaced
  the bare `process.exit` with a `setTimeout` poll on `_asyncQueue`
  before exiting. Around a dozen pre-existing async tests are now
  actually validated.


---

## 1. Source-level semantics that aren't enforced

### `const` is not read-only
`const x = 1; x = 2;` succeeds silently — `const` is converted to `var`
and re-assignment is not detected.

### Top-level `let`/`const` leak to global
`let x = 1` at program scope becomes `var x = 1`, which under duktape
attaches to `globalThis`. ES2015 says block scope should keep these out
of the global object.

### `await` inside `for`/`while`/`do` does not iterate
The async-to-regenerator lowering does not produce a state-machine
case-label per loop iteration for `await` inside a loop body, so the
await runs at most once and the loop semantics break.

Workaround: hoist the await outside the loop, or fan out with
`Promise.all`.

### BigInt is absent
`123n` literals, BigInt operators, and the `BigInt` global do not exist.

### `Intl` is absent
No polyfill loaded; `Intl.Collator`, `Intl.DateTimeFormat`, etc. throw
`ReferenceError`.

---

## 2. fn-source feature gaps (v1 scope cuts)

### Class methods don't get `__source__`
`rewrite_attach_fn_source` skips `method_definition`. Inside a
`class C { foo() {} }`, `C.prototype.foo.toString()` still returns
duktape's `function foo() { [ecmascript code] }`.

Fix path: have `_TrN_Sp.createClass` (CLASS_PF polyfill) pick up an
extra `_src` field on each method descriptor and attach via
`Object.defineProperty(target[key], '__source__', ...)`. Requires
emitting `_src` from the C side in the method-descriptor emitter at
`rewrite_class_to_es5`.

### Object-literal method shorthand doesn't get `__source__`
`{ foo() {} }` is a `method_definition` inside an `object` node. Same
v1-cut as class methods. The transpiler's
`rewrite_plain_method_shorthand` converts these to `foo: function(){}`
on a later pass, at which point the inner `function` would normally
get wrapped — but it lives inside the object literal, not as a
standalone expression, so the current dispatch path doesn't fire.

### Anonymous `export default function() {}` not wrapped
fn-source's decl path skips when there's no `name` field. The
declaration runs but `__source__` isn't attached.

---

## 3. Issues surfaced by the fn-source work

### Inner arrows inside async bodies aren't rewritten to ES5 functions
On pass 0 the async rewriter and the arrow rewriter both try to edit
the same overlapping region. Arrow's edit is sorted descending by
start and applied first, but the async rewriter's replacement text
covers the arrow range — built from raw src bytes, so it contains the
original `() => ...`. After apply_edits the arrow's replacement is
overwritten.

Final output keeps `() => ...` inside the regenerator state machine.
Duktape happens to accept arrows, so this works at runtime, but the
transpiler is not actually emitting ES5 in this case.

Fix path: have the async rewriter's body emission consult the edit
list for ranges inside the body and substitute their replacement text
instead of copying src bytes. Non-trivial — most rewriters today emit
to src-byte ranges and don't compose.

### Async body lines drop leading whitespace in some emit paths
Fixed for `var`/`let`/`const` declarations without `await` (the
`_emit_var_decl_as_assignments` path at `transpiler.c:4173`). Other
paths in `_build_regenerator_switch_body` may still strip leading
newlines — specifically:

- Concise arrow body for async arrows (around `transpiler.c:4184`).
- Complex statements lowered via `_emit_stmt_async_lower` for
  awaits inside expressions other than simple assignments.

Same fix pattern applies: emit `src[ss..stmt_s]` before the custom
emit function runs.

### Polyfill preamble is emitted multiple times
When pass N detects polyfills not seen on earlier passes, `apply_edits`
prepends a second `if(!global._TrN_Sp){...};_TrN_Sp.load();` preamble
in front of the existing one. The output is correct but wasteful (the
double-preamble is real text in the cached `.transpiled.js`). The
fn-source polyfill-prefix detector handles this with a loop.

Fix path: in `apply_edits`, when a polyfill prefix already exists in
`src` (i.e. pass >= 1), inject new polyfills *inside* the existing
preamble's `_TrN_Sp.load = function() { ... }` body rather than
prepending a new one.

### `yield` inside loop warning is a false positive
`warn_unsupported_patterns` warns whenever a `yield_expression` has
any `for`/`while`/`do` ancestor in the same function. But basic
generator yield-in-loop works (the regenerator runtime handles it —
three of our test cases trigger the warning yet pass). The warning
should either be narrowed to actually-broken patterns or removed.

The matching warning for `await` inside a loop *is* legitimate.

---

## 4. ES2015+ features explicitly noted as TODO in tests

### Symbol type and global symbols
`test/transpile-test.js:1409` — TODO comment for full Symbol semantics.
`Symbol.iterator` works in practice; `Symbol.for` / `Symbol.keyFor` /
description / well-known symbols beyond `iterator` not covered.

### Generator methods
`test/transpile-test.js:1607` — "incomplete. TODO: write own". The
existing class-generator-method path goes through the regenerator
runtime but the integration isn't fully validated.

### Generator control flow with setTimeout
`test/transpile-test.js:1604` — duktape doesn't have setTimeout in the
pure embedding, so this is a runtime gap, not strictly a transpiler
gap. (Rampart's main loop does provide setTimeout, so it works under
rampart but is noted as untested.)

### `export default` wildcards
`test/transpile-test.js:2055` — `export * as default from "..."` (ES2020)
not implemented.

---

## 5. Not implemented (no warning emitted)

### `for await ... of`
Async iteration syntax is not handled by any rewriter. Likely a parse
error or silently broken under duktape.

### Top-level `await`
Probably not handled; not tested.

### Decorators
`@decorator class C {}` and method decorators — not in the grammar
dispatch.

### Private class fields and methods (`#name`)
Newer syntax; not handled.

### Numeric separators in BigInt literals
`1_000_000_000n` would fail at the BigInt level anyway.

### Dynamic `import()` expressions
The static `import` statement rewriter exists; dynamic `import(specifier)`
returning a promise is not implemented.

---

## 6. Architectural concerns

### MAX_PASSES = 10 ceiling
Deeply nested async-inside-async or generator-inside-generator can
require one pass per level of nesting (because each pass "opens up"
one layer of the lowering for fn-source to see). 9+ levels of nesting
will hit the ceiling and `transpiler.c` calls `exit(1)`.

`exit(1)` from a library function is itself questionable — a transpile
failure shouldn't tear down the whole process. Should return an error
result instead.

### Two-source-of-truth between rewriters
The async, generator, class, and arrow rewriters each read raw `src`
bytes and emit replacement text. They have no knowledge of edits other
rewriters are queuing for the same span. The fn-source inner-arrow
clobbering is the symptom; the root cause is that there's no shared
"emit with respect to pending edits" helper. Refactoring to thread the
edit list through the body-emission functions would compose better.

### `polysdone` is process-global (static)
Polyfill tracking persists across calls to `transpile()`. This is
intentional for module loading (don't re-emit polyfills the main
script already loaded), but it makes the transpiler non-reentrant and
makes testing harder.

---

## 7. `let`/`const` semantic gap — recommended path

The biggest user-visible gap in the transpiler is that `let`/`const`
are converted to `var` and so don't get their ES2015 semantics:
no read-only, no block scope, no TDZ, no per-iteration for-loop
binding. Doing this **properly** would require implementing
block-scoped lexical environments inside duktape itself (compiler +
VM), which is a multi-week change to the most delicate part of the
engine. It would also be a permanent fork from upstream duktape (which
is intentionally ES5-only).

**Recommendation: skip the duktape work. Close the safety gap in the
transpiler instead.** The user-visible benefit is ~80-90% of real
`let`/`const` for a fraction of the effort, with no duktape divergence.
Four items, each independently shippable:

### 7.1. `const` re-assignment as a transpile error
Cheapest, biggest visible win on safety. Walk each scope, track names
declared with `const`. If any subsequent `assignment_expression` or
`augmented_assignment_expression` writes to one of those names within
its scope, emit a transpile error pointing at the offending line.

Caveats: dynamic patterns (e.g. `eval`, computed property indirection)
slip through — accepted limitation. Real-world `const x; x = 1;` and
`const obj = {}; obj = {};` are the common typos this catches.

Cost: ~80 lines of C, single AST pass.

### 7.2. TDZ as a transpile-time check
Static check: walk forward in source order within each scope; flag any
reference to a `let`/`const`-declared name that lexically precedes its
declaration as a transpile error. Catches the obvious "wrote the read
above the let" cases. Misses cross-function dynamic cases (calling a
function whose body references a `let` before that `let` is reached in
the caller's flow), which is rare and probably-broken-anyway code.

Cost: ~100 lines of C. Same scope-tracking infrastructure as 7.1.

### 7.3. Block re-declaration check
Per-scope set of declared names. Adding the same name twice in one
scope (same block / same for-init / etc.) is a transpile error.
Function-scoped `var` is allowed to re-declare per ES5 semantics — only
`let`/`const` should error. Mixed cases (a `var` shadowing a `let` in
an outer scope) follow `let`/`const` rules: error.

Cost: shares the scope tracking from 7.1/7.2. ~30 lines on top.

### 7.4. Closure-aware for-loop `let` (the killer feature)
Today's transpiler IIFE-wraps `for (let i = 0; ...) { body }` to fake
per-iteration capture, but bails out when the body has `break` /
`continue` / `return` / `this` — falling back to the closure-captures-
final-value bug `let` was designed to fix.

Babel's approach, adapted:
- Walk the loop body for any `function`/`arrow_function` that captures
  the loop variable (or references any outer name that shadows it).
- **No captures**: don't bother wrapping at all. Closure-of-final-value
  doesn't matter if no closure is created.
- **Has captures, no flow control**: keep the IIFE wrap as today.
- **Has captures AND flow control** (break/continue/return/this):
  hoist captured closures to helpers that take the loop variable by
  value. Body stays unwrapped so break/continue/return/this still work.

Cost: ~200 lines of C. The AST walk to detect "is the loop variable
captured" is the main work; the rewriting is mechanical.

### Implementation order

Suggested order, doing 7.1 first because it has the highest user-visible
safety per line of C:

1. **7.1 const re-assignment** — quick, high-impact static check.
2. **7.4 closure-aware for-loop `let`** — fixes the canonical
   JavaScript-gotcha bug that everyone has been bitten by.
3. **7.2 TDZ static check** — catches obvious mistakes; pairs nicely
   with 7.3.
4. **7.3 block re-declaration** — same scope-tracking infrastructure
   as 7.2; cheap follow-on.

### What this approach does *not* cover

- True block scope (a `let x` inside `if (cond) { let x; }` shadows
  outer `x`): the transpiled output still has function-scoped `var`,
  so the shadowing renames. Most code doesn't actually depend on this
  beyond the for-loop case 7.4 covers.
- Dynamic TDZ (a function call that reaches a `let` before its
  declaration via control flow): static check misses it.
- `const` writes via aliasing (`var alias = constObj; alias.prop = …`):
  not the same identifier so not caught. This matches real `const`
  semantics anyway — `const` doesn't deep-freeze.
- Re-binding via `eval` / `new Function` / `with`: out of scope.

These residual gaps are academic for nearly all real code, and adding
warnings or runtime checks for them would cost more than they buy.
