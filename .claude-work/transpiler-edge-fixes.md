# Transpiler Edge Case Fixes

This document details every fix and addition made to the Rampart ES2015+ → ES5
transpiler during the edge-case hardening effort. All tests are in
`test/transpile-edge-test.js` (125 tests) and `test/transpile-test.js` (184 tests).

---

## 1. Syntax Transforms (transpiler.c)

### 1.1 Optional Chaining `?.` (ES2020)

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_optional_chaining()`

Transforms `obj?.a?.b` into nested ternaries:
`(obj == null ? void 0 : obj.a == null ? void 0 : obj.a.b)`.
Handles property access, method calls (`obj?.method()`), and bracket access
(`obj?.["key"]`). Uses temp variables for non-trivial base expressions to avoid
double-evaluation.

**Tests:** 9 tests (deep chains, null mid-chain, method calls, bracket access, ternary context)

### 1.2 Nullish Coalescing `??` (ES2020)

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_nullish_coalescing()`

Transforms `a ?? b` into `(_t = a, _t != null ? _t : b)` with temp variables to
prevent double evaluation. Correctly preserves falsy values (`0`, `""`, `false`, `NaN`).

**Tests:** 9 tests (all falsy types, chaining, function call single-eval)

### 1.3 Logical Assignment `??=` `||=` `&&=` (ES2021)

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_logical_assignment()`

Transforms:
- `a ??= b` → `a = (a != null ? a : b)`
- `a ||= b` → `a = a || b`
- `a &&= b` → `a = a && b`

Handles both plain variables and member expressions (`obj.x ??= b`).

**Tests:** 7 tests (null/undefined/falsy semantics, object properties)

### 1.4 Numeric Separators (ES2021)

**File:** `extern/tree-sitter/transpiler.c` — inline in dispatch loop (number node handling)

Strips all `_` characters from number literals: `1_000_000` → `1000000`.
Handles decimal, hex, octal, and binary formats.

**Tests:** 5 tests (integer, float, hex, octal, binary)

### 1.5 Optional Catch Binding (ES2019)

**File:** `extern/tree-sitter/transpiler.c` — inline in dispatch loop (catch_clause handling)

Inserts a dummy parameter when `catch` has no binding: `catch {` → `catch(_unused) {`.

**Tests:** 2 tests (basic, with finally)

### 1.6 Class Fields (ES2022)

**File:** `extern/tree-sitter/transpiler.c` — `es5_emit_class_core()`

Moves `field_definition` initializations into the constructor body as
`this.name = value;` statements. Handles both instance and static fields.

**Tests:** 6 tests (field + method, static field, field referencing this, combo tests)

### 1.7 `super.method()` in Class Methods

**File:** `extern/tree-sitter/transpiler.c` — `copy_body_replace_super()` (line ~5890)

Scans method/constructor body text for `super.X` patterns and rewrites:
- Instance methods: `super.method(args)` → `_Super.prototype.method.call(this, args)`
- Static methods: `super.method(args)` → `_Super.method.call(this, args)`
- Property access: `super.prop` → `_Super.prototype.prop`

Applied at three call sites in `es5_emit_class_core()`: method bodies,
constructor remainder after `super()`, and the NO_SUPER_REWRITE case.

**Tests:** 4 tests (with args, no args, property access, in constructor)

### 1.8 Rest Parameters in Class Methods/Constructors

**File:** `extern/tree-sitter/transpiler.c` — in `es5_emit_class_core()`

Detects `rest_pattern` in method/constructor parameters, strips it from the
parameter list, and injects `var rest = Object.values(arguments).slice(N);`
at the body start.

**Tests:** 3 tests (constructor rest, method rest, method rest with leading params)

### 1.9 Spread in Function/New Calls (ES2015)

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_call_spread()` (line ~6913)

Transforms:
- `fn(...args)` → `fn.apply(void 0, args)`
- `obj.method(...args)` → `obj.method.apply(obj, args)`
- `new Foo(...args)` → `new (Function.prototype.bind.apply(Foo, [null].concat(args)))()`
- Mixed args: `fn(a, ...rest)` → `fn.apply(void 0, [a].concat(rest))`

**Tests:** 5 tests (simple, with leading args, method call, new expression, Math.max)

### 1.10 `const` Block Scoping

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_lexical_declaration()` (line ~4992)

Fixed: the `const` branch set `ret = 1` but not `have_let = 1`, skipping all
IIFE wrapping logic. Changed to `have_let = 1` so `const` in blocks gets the
same scoping treatment as `let`.

Also removed program-level IIFE wrapping for top-level `let`/`const` — it broke
`eval()` because the C eval replacement always runs as indirect eval (global scope).

**Tests:** 1 test (const block scoping)

### 1.11 `let` in for-of Fresh Per-Iteration Bindings

**File:** `extern/tree-sitter/transpiler.c` — `rewrite_for_of_simple()` (line ~6753)

Detects when for-of uses `let`/`const` via the `kind` field on `for_in_statement`.
When detected, IIFE-wraps the loop body: `(function(v){ body })(v);` so closures
capture a fresh binding per iteration.

Also fixed `rewrite_lexical_declaration()` to check the `left` field (not just
`initializer`) for `for_in_statement` parents.

**Tests:** 1 test (closures capture per-iteration values)

### 1.12 `transpile_eval()` — Eval Scope Preservation

**File:** `extern/tree-sitter/transpiler.c` (line ~8315), `transpiler.h` (line 52),
`src/duktape/register.c` (line ~887)

Added `transpile_eval()` entry point that passes `no_program_wrap=1` through
`transpile_code()` → `transpiler_rewrite_pass()` → `rewrite_lexical_declaration()`.
The eval replacement in `rp_eval_js()` calls `transpile_eval()` instead of
`rp_get_transpiled()` to avoid IIFE-wrapping eval'd code.

---

## 2. Destructuring Improvements (transpiler.c)

All destructuring flows through `collect_flat_destructure_bindings()` (line ~1080)
which recursively extracts `{name, repl, defval}` binding tuples, then the caller
(`rewrite_destructuring_declaration`, `rewrite_destructuring_assignment`,
`rewrite_function_destructuring_params`, etc.) emits the var declarations.

### 2.1 Object Rest (`{a, ...rest} = obj`)

**File:** `collect_flat_destructure_bindings()` — object_pattern section

Added `rest_pattern` handling. Tracks extracted key names in an `rp_string` as
the pattern is processed. When `...rest` is encountered, emits a filtering IIFE:
```
(function(s,e){var r={};for(var k in s)if(Object.prototype.hasOwnProperty.call(s,k)
&&e.indexOf(k)<0)r[k]=s[k];return r;})(base, ["a","b",...])
```

### 2.2 Array Rest After Skip (`[, ...rest] = arr`)

**File:** `collect_flat_destructure_bindings()` — array_pattern section

Added `rest_pattern` handling: `Array.prototype.slice.call(base, idx)`.

### 2.3 Nested Patterns in Array Elements

**File:** `collect_flat_destructure_bindings()` — array_pattern section

Added recursive `collect_flat_destructure_bindings()` call for `object_pattern`
and `array_pattern` children of array elements.

### 2.4 Computed Property Keys (`{[key]: name} = obj`)

**File:** `collect_flat_destructure_bindings()` — object_pattern pair_pattern handler

Added `computed_property_name` key type handling. Extracts the inner expression
and generates bracket access: `base[expr]`.

### 2.5 Nested Object with Intermediate Default (`{a: {b: x = 10} = {}} = {}`)

**File:** `collect_flat_destructure_bindings()` — pair_pattern assignment_pattern handler

Added handling for when the `assignment_pattern` left side is `object_pattern` or
`array_pattern` (not just `identifier`). Generates a safe base expression with
default: `(base.a !== undefined ? base.a : {})`, then recurses into the nested pattern.

### 2.6 Rename + Default (`{x: rx = 5} = obj`)

**File:** `collect_flat_destructure_bindings()` — pair_pattern handler

Added `assignment_pattern` case in the pair_pattern value handler. Extracts the
renamed identifier and default value.

### 2.7 Destructuring Assignment — Not Declaration (`({a, b} = obj)`)

**File:** `rewrite_destructuring_assignment()` (line ~1491)

Added handling for `parenthesized_expression` wrapping. When the expression
statement's child is `parenthesized_expression`, unwraps it to find the inner
`assignment_expression`.

### 2.8 Destructuring in Function Parameters

**File:** `rewrite_function_destructuring_params()` (line ~2675)

New function. Scans function parameters for `object_pattern`/`array_pattern`,
replaces them with temp names (`_dpN`), and injects `var` declarations at the
body start using `collect_flat_destructure_bindings()`.

### 2.9 Destructuring in for-of Loops

**File:** `rewrite_for_of_destructuring()` (line ~5143)

Extended to handle `object_pattern` (was array_pattern only). Uses
`collect_flat_destructure_bindings()` to extract bindings and generates a while
loop with per-iteration destructuring.

Also added `variable_declaration` as a valid left-side type (was
`lexical_declaration` only), so `for (var {x} of arr)` works.

### 2.10 Destructuring Defaults in Arrow Parameters

**File:** `rewrite_concise_body_with_bindings()` (line ~1567)

Fixed to emit `(repl !== undefined ? repl : defval)` when a binding has a default
value, instead of just `repl`. Also fixed block body declaration builder and
added `param_default` tracking for parameter-level defaults (`({x}={}) => ...`).

**Tests for all destructuring:** 19 tests total across sections 2.1–2.10

---

## 3. C Runtime Polyfills (register.c)

All polyfills follow the existing pattern: C functions registered on prototypes
or constructors via `duk_push_c_function` / `duk_put_prop_string`.

### 3.1 Array.from(iterable, mapFn?)
**Line:** ~303. Handles strings, arrays, Symbol.iterator iterables, and array-like objects ({length}).

### 3.2 Array.of(...args)
**Line:** ~421. Creates array from arguments.

### 3.3 String.prototype.trimStart / trimEnd
**Lines:** ~434, ~444. Trims whitespace from start or end.

### 3.4 Array.prototype.flat(depth?) / flatMap(fn)
**Lines:** ~477, ~502. `flat()` recursively flattens to given depth (default 1, supports Infinity). `flatMap()` maps then flattens by 1.

### 3.5 String.prototype.replaceAll(search, replacement)
**Line:** ~542. Replaces all occurrences of a substring. Handles empty search string specially (inserts between every character).

### 3.6 Array.prototype.at(index)
**Line:** ~589. Supports negative indices (`arr.at(-1)` = last element).

### 3.7 Object.hasOwn(obj, prop)
**Line:** ~602. Uses `Object.prototype.hasOwnProperty.call()` internally.

### 3.8 Array.prototype.findLast(fn) / findLastIndex(fn)
**Lines:** ~621, ~646. Like find/findIndex but search from end.

### 3.9 Object.fromEntries(iterable)
**Line:** ~672. Converts `[[key, value], ...]` array to object.

**Registration:**
- `add_string_funcs()` (line ~743): trimStart, trimEnd, replaceAll
- `add_extra_object_funcs()` (line ~763): hasOwn, fromEntries
- `add_array_funcs()` (extended): from, of, flat, flatMap, at, findLast, findLastIndex

**Tests:** 9 tests in `transpile-test.js` (one per polyfill)

---

## 4. Warnings for Unsupported Patterns (transpiler.c)

**File:** `extern/tree-sitter/transpiler.c` — `warn_unsupported_patterns()` (line ~515)

A read-only AST scan (no edits) run on the first pass before rewrites. Detects
and warns about three patterns the regenerator cannot handle correctly:

1. **Destructuring + await** (`const {a, b} = await expr`) — emits shorthand
   properties in regenerator output, invalid in ES5.
   Warning suggests: `const tmp = await expr; const {a, b} = tmp;`

2. **Await inside loops** (`for (...) { await expr; }`) — await is extracted
   before the loop rather than per-iteration.
   Warning suggests: move await outside the loop or use `Promise.all()`.

3. **Yield inside loops** (`for (...) { yield expr; }`) — yield statement
   disappears during regenerator transpilation.
   Warning suggests: use a manual iteration pattern.

Each warning includes the source line number via `ts_node_start_point()`.

---

## 5. Test Summary

| Suite | Count | Description |
|-------|-------|-------------|
| `test/transpile-edge-test.js` | 128 | Edge cases for all transforms |
| `test/transpile-test.js` | 184 | Main feature tests + polyfills |
| `test/bt.js` | 54 | Broader transpiler tests |
| **Total** | **366** | |

---

## 6. Future Work — Known Limitations

The following limitations are inherent to transpiling ES2015+ to ES5 on Duktape.
They are documented at the top of both test files.

### 6.1 Engine-Level Limitations (cannot be fixed by transpilation)

- **`const` is not read-only.** Both our transpiler and Babel convert `const` to
  `var`. Duktape (ES5.1) has no mechanism to enforce immutability on variable
  bindings. There is no workaround short of modifying the Duktape engine.

- **`let`/`const` at top level attach to the global object.** Converting to `var`
  at global scope creates a property on `global`. In ES2015+, `let`/`const` at
  top level occupy a separate "script scope" that is not on the global object.
  IIFE wrapping the entire program would fix this but breaks `eval()` because the
  C eval replacement (`rp_eval_js`) always runs as indirect eval (global scope).

- **BigInt literals (`123n`) and operators are not supported.** Duktape has no
  BigInt type. The `rampart-crypto` module provides `JSBI.BigInt()` with function-
  call syntax (`JSBI.add(a, b)` instead of `a + b`). Transpiling operator syntax
  would require wrapping every arithmetic operator with a runtime type check,
  which would degrade performance for all numeric code.

- **Intl (Internationalization API) is not available.** This is a large runtime
  library not included in Duktape. It cannot be provided by transpilation.

### 6.2 Regenerator Limitations (could be fixed with significant effort)

These three patterns are correctly handled by Babel's regenerator but not by ours.
A comparison test is in `test/babel-limitations-test.js` (which uses Babel) showing
all three pass under Babel.

#### Destructuring + await

```javascript
// This does not work:
const {a, b} = await somePromise();
// Workaround:
const tmp = await somePromise();
const {a, b} = tmp;
```

**Root cause:** Our regenerator emits `{a, b} = _context.sent` which uses ES2015
shorthand property syntax, invalid in ES5.

**Babel's approach:** Babel assigns `_context.sent` to a temp variable
(`_yield$Promise$resolv`), then destructures from it with separate assignments:
```javascript
_yield$Promise$resolv = _context.sent;
a = _yield$Promise$resolv.a;
b = _yield$Promise$resolv.b;
```

#### Await inside loops

```javascript
// This does not work:
for (var i = 0; i < 3; i++) { sum += await Promise.resolve(i); }
// Workaround: move await outside the loop or use Promise.all()
```

**Root cause:** Our regenerator processes statements linearly. It extracts the
`await` before the loop rather than decomposing the loop into per-iteration states.

**Babel's approach:** The loop is broken into numbered states in a `switch`
statement. Each `await` point gets its own `case` label, with the loop condition,
body, and increment as separate states connected by explicit `_context.next` jumps:
```javascript
case 2: if (!(i <= 3)) { _context.next = 10; break; }  // condition
_context.next = 6; return Promise.resolve(i);            // await/yield
case 6: sum += _context.sent;                            // resume
case 7: i++; _context.next = 2; break;                   // increment + loop back
```

#### Yield inside loops

```javascript
// This does not work:
function* range(s, e) { for (let i = s; i < e; i++) yield i; }
// Workaround: use a manual iteration pattern
```

**Root cause:** The yield statement inside the loop body is dropped during
transpilation. Our regenerator does not decompose loop bodies into sub-states.

**Babel's approach:** Same state machine decomposition as await-in-loops. The
`for` loop becomes a set of `case` labels:
```javascript
case 0: i = start;                                        // init
case 1: if (!(i < end)) { _context.next = 7; break; }    // condition
_context.next = 4; return i;                               // yield point
case 4: i++; _context.next = 1; break;                    // increment + loop back
```

### 6.3 What It Would Take to Fix the Regenerator

Fixing the three regenerator limitations would require rewriting the state machine
builder in `transpiler.c` (`rewrite_async_await_to_regenerator` and
`rewrite_generator_to_regenerator`) to handle **control flow graphs** rather than
linear statement sequences. Specifically:

1. Each `await`/`yield` point must be assigned a unique state number.
2. Loops must be decomposed: the condition check, body (split at yield points),
   and increment each become separate states.
3. `break`/`continue` inside loops must map to the correct state transitions.
4. Nested loops and conditionals containing `await`/`yield` multiply the
   complexity — each level of nesting requires its own set of states.

This is a substantial undertaking. The current regenerator handles the common case
(sequential awaits, yield outside loops) correctly. For complex async/generator
patterns requiring loops, the Babel transpiler (`"use babel"`) can be used instead.
