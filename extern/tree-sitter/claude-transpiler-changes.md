# Claude assisted Transpiler Changes (`extern/tree-sitter/transpiler.c`)

February 13th, 2026

All changes are relative to commit `afedbddb` (methodRaw).

## Todo: 

1. Review each, add more tests and try on real world code.

2. Check number 3 below.  The description of the problem is wrong
   (it was a js module with "use babel" in it), so the solution
   provided may be wrong or unnecessary.

3. Continue with claude to fix the disabled tests in transpile-test.js.

---

## 1. BASE_PF Polyfill Flag

**Location:** Line ~174

**What changed:** Added a new polyfill bit flag `BASE_PF`.

**Old code:**
```c
#define FOROF_PF    (1<<3)
#define PROMISE_PF  (1<<4)
#define ASYNC_PF    (1<<5)
```

**New code:**
```c
#define FOROF_PF    (1<<3)
#define PROMISE_PF  (1<<4)
#define ASYNC_PF    (1<<5)
#define BASE_PF     (1<<6)  // ensures _TrN_Sp preamble is emitted even with no specific polyfill
```

**Why:** Some transpilations (e.g. destructuring, let/const conversion) alter code but don't set any specific polyfill flag. Without a polyfill flag set, the `_TrN_Sp` global object preamble was not emitted, causing runtime errors when the transpiled code referenced `_TrN_Sp`. The `BASE_PF` flag ensures the preamble is always emitted when any edits are made. Used at the bottom of `transpile_code()` where `if (edits.len && !polysneeded) polysneeded = BASE_PF;`.

---

## 2. CLASS_PF Polyfill: `_typeof` -> `typeof` in `possibleConstructorReturn`

**Location:** Line ~186 (CLASS_PF polyfill string)

**What changed:** In the `possibleConstructorReturn` helper, changed `_typeof(call)` to `typeof call`.

**Old code (inside polyfill string):**
```js
_TrN_Sp.possibleConstructorReturn = function(self, call) {
  if (call && (_typeof(call) === 'object' || typeof call === 'function')) {
    return call;
  }
  ...
```

**New code (inside polyfill string):**
```js
_TrN_Sp.possibleConstructorReturn = function(self, call) {
  if (call && (typeof call === 'object' || typeof call === 'function')) {
    return call;
  }
  ...
```

**Why:** The `_typeof` function was not defined/initialized at the point where `possibleConstructorReturn` is called, causing a `ReferenceError: identifier '_typeof' undefined` at runtime. The native `typeof` operator works without any setup and is sufficient here since we don't need the Babel Symbol-aware typeof behavior in this particular check.

---

## 3. PROMISE_PF Polyfill: Promise Patching After `require()`

**Location:** Line ~204 (end of PROMISE_PF polyfill string)

**What changed:** Appended Promise function saving/restoration code to the end of the Promise polyfill string.

**Old code:** The PROMISE_PF string ended after the polyfill's closing `});`.

**New code:** Appended after the closing `});`:
```js
_TrN_Sp._pAS=Promise.allSettled;
_TrN_Sp._pAn=Promise.any;
_TrN_Sp._pF=Promise.prototype['finally'];
_TrN_Sp._pP=function(){
  if(typeof Promise==='function'){
    if(!Promise.allSettled&&_TrN_Sp._pAS) Promise.allSettled=_TrN_Sp._pAS;
    if(!Promise.any&&_TrN_Sp._pAn) Promise.any=_TrN_Sp._pAn;
    if(Promise.prototype&&!Promise.prototype['finally']&&_TrN_Sp._pF)
      Promise.prototype['finally']=_TrN_Sp._pF;
  }
};
```

**Why:** Rampart's built-in `require("math")` (and potentially other C modules) replaces the global `Promise` with a native version that is missing `allSettled`, `any`, and `finally`. After our polyfill installs these methods, a `require()` call can wipe them out. The `_pP()` function saves the polyfilled methods and restores them after each require.

---

## 4. Promise Patching Calls in All Import Handlers

**Location:** Five import handler functions (~lines 2474-2669)

**What changed:** Added `if(_TrN_Sp._pP)_TrN_Sp._pP();` after every `require()` call in the transpiled import output.

**Functions affected:**

### `do_named_imports` (line ~2477)
**Old:** `"var %s=require(\"%.*s\");"`
**New:** `"var %s=require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();"`

### `do_namespace_import` (line ~2531)
**Old:** `"var %.*s=_TrN_Sp._interopRequireWildcard(require(\"%.*s\"));"`
**New:** `"var %.*s=_TrN_Sp._interopRequireWildcard(require(\"%.*s\"));if(_TrN_Sp._pP)_TrN_Sp._pP();"`

### `do_default_import` (line ~2552)
**Old:** `"var %.*s=_TrN_Sp._interopDefault(require(\"%.*s\"));"`
**New:** `"var %.*s=_TrN_Sp._interopDefault(require(\"%.*s\"));if(_TrN_Sp._pP)_TrN_Sp._pP();"`

### `do_default_and_named_imports` (line ~2575)
**Old:** `"var %s=require(\"%.*s\");"`
**New:** `"var %s=require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();"`

### bare `require` in `rewrite_import_node` (line ~2669)
**Old:** `snprintf(out, slen + 13, "require(\"%.*s\");", ...)`
**New:** `snprintf(out, outlen, "require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();", ...)`

**Why:** Each transpiled `import` statement becomes a `require()` call. Each `require()` can replace the global `Promise`, stripping the polyfilled methods. The `_pP()` call after each require re-patches `Promise.allSettled`, `Promise.any`, and `Promise.prototype.finally` if they were lost. The calls are inline (same line as the require) to preserve line numbering.

---

## 5. Binding Infrastructure: Default Values Support

**Location:** Lines ~931-980 (Binding struct and helpers)

**What changed:** Added a `defval` field to the `Binding` struct and created a `binds_add_def()` function that accepts default values. The original `binds_add()` now delegates to `binds_add_def()` with `NULL`.

**Old code:**
```c
typedef struct {
    char *name;
    char *repl;
} Binding;

static void binds_add(Bindings *b, const char *name, size_t nlen, const char *repl)
{
    ...
    b->a[b->len].repl = strdup(repl);
    b->len++;
}
```

**New code:**
```c
typedef struct {
    char *name;
    char *repl;
    char *defval;  // default value expression (NULL if none)
} Binding;

static void binds_add_def(Bindings *b, const char *name, size_t nlen, const char *repl, const char *defval)
{
    ...
    b->a[b->len].repl = strdup(repl);
    b->a[b->len].defval = defval ? strdup(defval) : NULL;
    b->len++;
}
static void binds_add(Bindings *b, const char *name, size_t nlen, const char *repl)
{
    binds_add_def(b, name, nlen, repl, NULL);
}
```

Also updated `binds_free()` to free `defval`.

**Why:** Destructuring with defaults (e.g. `let { b = 2 } = obj`) needs to track the default value expression for each binding so it can emit `b = _d.b !== undefined ? _d.b : 2`.

---

## 6. Nested Destructuring in `collect_flat_destructure_bindings`

**Location:** Lines ~1023-1140

**What changed:** Extended the `shorthand_property_pair` handling in object patterns to support:
- **Nested patterns:** `{ a: { x, y } } = obj` — recursively collects bindings from nested object/array patterns.
- **Default values in array patterns:** `[a, b = 5] = arr` — extracts the default value from `assignment_pattern` nodes.
- **Default values in object patterns:** `{ b = 2 } = obj` — handles `object_assignment_pattern` and `assignment_pattern` nodes with default values.

**Old code:** Only handled `identifier` values in `shorthand_property_pair`; no default value extraction; no nested patterns.

**New code:**
- For `shorthand_property_pair`, checks if the value is an `identifier`, `object_pattern`, or `array_pattern`. For nested patterns, recursively calls `collect_flat_destructure_bindings` with the nested base path (e.g., `_d.a`).
- For `assignment_pattern` in arrays, extracts the `right` child as `defval` and calls `binds_add_def()`.
- Added new handler for `object_assignment_pattern`/`assignment_pattern` nodes (shorthand with default, like `{ b = 2 }`).

**Why:** The original code only supported flat destructuring. ES2015+ allows deeply nested patterns and defaults, which are used in test files (ttest4.js for nested, ttest5.js for defaults).

---

## 7. General Destructuring Rewriting (New Functions)

**Location:** Lines ~1143-1330 (two new functions)

**What changed:** Added two new functions for transpiling destructuring syntax to ES5.

### `rewrite_destructuring_declaration`
Handles `variable_declaration` nodes with destructuring patterns:
```js
// Input:
var [a, , b] = expr;
var {x, y} = obj;
// Output:
var _d1 = expr; var a = _d1[0]; var b = _d1[2];
var _d2 = obj; var x = _d2.x; var y = _d2.y;
```
When a binding has a default value:
```js
// Input:
var { b = 2 } = obj;
// Output:
var _d3 = obj; var b = _d3.b !== undefined ? _d3.b : 2;
```

### `rewrite_destructuring_assignment`
Handles expression statements with destructuring assignment:
```js
// Input:
[b, a] = [a, b];
// Output:
var _d4 = [a, b]; b = _d4[0]; a = _d4[1];
```

**Why:** Duktape does not support destructuring syntax. These functions flatten destructuring declarations and assignments into simple variable assignments using a temporary variable with a unique counter suffix (`_d1`, `_d2`, etc.).

---

## 8. Class Methods: Getter/Setter Support

**Location:** Lines ~5079-5120 (`es5_emit_class_core`)

**What changed:** Added detection and handling for `get` and `set` method modifiers in class definitions.

**Old code:**
```c
int is_static = 0;
// detect "static" modifier
for (...) {
    if (se > ss && strncmp(src + ss, "static", 6) == 0)
    {
        is_static = 1;
        break;
    }
}
// ... later:
rp_string_puts(bucket, "{key:'");
rp_string_putsn(bucket, src + ks, ke - ks);
rp_string_puts(bucket, "',value:function ");
rp_string_putsn(bucket, src + ks, ke - ks);
```

**New code:**
```c
int is_static = 0;
int is_getter = 0;
int is_setter = 0;
// detect "static", "get", "set" modifiers
for (...) {
    size_t slen = se - ss;
    if (slen == 6 && strncmp(src + ss, "static", 6) == 0)
        is_static = 1;
    else if (slen == 3 && strncmp(src + ss, "get", 3) == 0)
        is_getter = 1;
    else if (slen == 3 && strncmp(src + ss, "set", 3) == 0)
        is_setter = 1;
}
// ... later, uses appropriate descriptor field:
const char *desc_field = "value";
if (is_getter) desc_field = "get";
else if (is_setter) desc_field = "set";
// emits {key:'name',get:function (){...}} for getters
// emits {key:'name',set:function (v){...}} for setters
// emits {key:'name',value:function name(){...}} for regular methods
```

Also omits the method name after `function` for getters/setters (they should be anonymous).

**Why:** `get color()` in a class was being emitted as `{key:'color',value:function color(){}}` instead of the correct `{key:'color',get:function(){}}`. Object.defineProperty uses the `get`/`set` descriptor fields, not `value`, for accessor properties.

---

## 9. Class Methods: Computed Property Name Support

**Location:** Lines ~5105-5130 (`es5_emit_class_core`)

**What changed:** Extended method processing to handle `computed_property_name` nodes (e.g., `[Symbol.iterator]()` in a class).

**Old code:**
```c
if (ts_node_is_null(nname) || strcmp(ts_node_type(nname), "property_identifier") != 0)
    continue;
size_t ks = ts_node_start_byte(nname), ke = ts_node_end_byte(nname);
// emits: {key:'name',value:function name(){...}}
```

**New code:**
```c
int is_computed = 0;
size_t ks, ke;
if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "property_identifier") == 0)
{
    ks = ts_node_start_byte(nname);
    ke = ts_node_end_byte(nname);
}
else if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "computed_property_name") == 0)
{
    is_computed = 1;
    ks = ts_node_start_byte(nname) + 1;  // skip '['
    ke = ts_node_end_byte(nname) - 1;    // skip ']'
}
else
    continue;

// For computed: {key:<expr>,value:function(){...}}
// For regular:  {key:'name',value:function name(){...}}
```

**Why:** Computed method names like `[Symbol.iterator]()` in classes need the key to be an expression (unquoted) rather than a string literal. The brackets are stripped since `_TrN_Sp.createClass` evaluates the key expression at runtime.

---

## 10. Super Call: Spread Argument Handling

**Location:** Lines ~5245-5280 (`es5_emit_class_core`)

**What changed:** When a class constructor calls `super(...args)` with a single spread argument, the transpiler now emits `_super.apply(this, args)` instead of `_super.call(this, ...args)`.

**Old code:**
```c
rp_string_puts(out, "_this = _super.call(this, ");
if (call_rp > args_s)
    rp_string_putsn(out, b + args_s, call_rp - args_s);
rp_string_puts(out, ");");
```

**New code:**
```c
const char *atext = b + args_s;
size_t alen = call_rp - args_s;
// trim whitespace...
if (alen > 3 && atext[0] == '.' && atext[1] == '.' && atext[2] == '.'
    && memchr(atext + 3, ',', alen - 3) == NULL)
{
    // single spread: super(...expr) -> _super.apply(this, expr)
    rp_string_puts(out, "_this = _super.apply(this, ");
    rp_string_putsn(out, atext + 3, alen - 3);
    rp_string_puts(out, ");");
}
else
{
    rp_string_puts(out, "_this = _super.call(this, ");
    if (call_rp > args_s)
        rp_string_putsn(out, b + args_s, call_rp - args_s);
    rp_string_puts(out, ");");
}
```

**Why:** `_super.call(this, ...args)` still contains spread syntax (`...`), which Duktape cannot execute. When there's a single spread argument, `_super.apply(this, args)` achieves the same result using only ES5 syntax.

---

## 11. Empty Subclass Constructor Calls Super

**Location:** Line ~5297 (`es5_emit_class_core`)

**What changed:** When a class with `extends` has no explicit constructor, the generated default constructor now calls the super constructor.

**Old code:**
```c
rp_string_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
rp_string_putsn(out, cname, cname_len);
rp_string_puts(out, ");return _this;");
```

**New code:**
```c
rp_string_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
rp_string_putsn(out, cname, cname_len);
rp_string_puts(out, ");_this = _super.apply(this, arguments);return _this;");
```

**Why:** `class Rectangle extends X {}` (no constructor) generated a constructor where `_this` was declared but never assigned from the super constructor, so the instance was broken. Adding `_this = _super.apply(this, arguments)` ensures the parent constructor is called with all forwarded arguments, matching ES2015 semantics.

---

## 12. For-Of Loop: Iterator Protocol Support

**Location:** Lines ~5667-5720 (`rewrite_for_of_simple`)

**What changed:** Rewrote the for-of transpilation from simple array indexing to a dual-mode approach that uses `Symbol.iterator` when available, with an array indexing fallback.

**Old code:**
```js
// Generated:
for (var _i = 0, _x = <rhs>; _i < _x.length; _i++) {
    var n = _x[_i];
    ...
}
```

**New code:**
```js
// Generated:
var _x = <rhs>,
    _it = (typeof Symbol!=='undefined' && typeof _x[Symbol.iterator]==='function')
          ? _x[Symbol.iterator]() : null,
    _i = 0, _r;
while (_it ? !(_r=_it.next()).done : _i<_x.length) {
    var n = _it ? _r.value : _x[_i++];
    ...
}
```

Uses separate variable names: `_it`/`_itN` for iterator, `_r`/`_rN` for result, `_i`/`_iN` for counter, `_x`/`_xN` for collection.

**Why:** The old array-indexing approach only worked with arrays. ES2015 for-of should work with any iterable that implements the `Symbol.iterator` protocol (e.g., Set, Map, custom iterables). The new code checks for `Symbol.iterator` first, and falls back to array indexing for plain arrays. Separate variable names prevent collisions (previously `_i` was reused).

---

## 13. Computed Method Shorthand in Object Literals (New Function)

**Location:** Lines ~5716-5770 (`rewrite_computed_method_shorthand`)

**What changed:** Added a new function to transpile computed method shorthand syntax in object literals.

```js
// Input:
{ [Symbol.iterator]() { body } }
// Output:
{ [Symbol.iterator]: function() { body } }
```

**Why:** Duktape supports `[expr]: value` (computed property names) and `name() {}` (method shorthand), but NOT the combination `[expr]() {}` (computed method shorthand). This function converts the computed method shorthand to a computed property with a function value, which Duktape can handle.

---

## 14. Generator Function Support (New Functions)

**Location:** Lines ~3764-4113 (multiple new functions)

**What changed:** Added a complete generator function lowering pipeline that converts `function*` / `yield` to a state machine using `_TrN_Sp.regeneratorRuntime.wrap()`.

### New functions:

- **`_collect_yields_shallow()`** — Walks the AST to find `yield_expression` nodes within a function body, stopping at nested function/class boundaries.
- **`_emit_stmt_yield_lower()`** — Lowers a single statement containing yields into state machine steps. Each yield becomes `_context.next = N; return (expr); case N: _context.sent;` with surrounding code reassembled.
- **`_build_regenerator_switch_body_for_yield()`** — Builds the complete `return _TrN_Sp.regeneratorRuntime.wrap(function _callee$(_context){while(1){switch(_context.prev=_context.next){case 0: ... }})` body. Iterates over statements, detects yields, and emits case labels.
- **`_is_generator_function_like()`** — Detects generator functions (by node type) and generator methods (by finding `*` token in method_definition children).
- **`_emit_generator_decl_replacement()`** — Converts `function* name(params) { body }` to `var name = _TrN_Sp.regeneratorRuntime.mark(function name(params) { <switch body> })`.
- **`_emit_generator_method_replacement()`** — Converts `*name(params) { body }` in an object literal to `name: _TrN_Sp.regeneratorRuntime.mark(function name(params) { <switch body> })`.
- **`_emit_generator_expr_replacement()`** — Converts `function*(params) { body }` expression to `_TrN_Sp.regeneratorRuntime.mark(function _gen(params) { <switch body> })`.
- **`rewrite_generator_to_regenerator()`** — Dispatch function that detects generator nodes and routes to the appropriate emitter.

### Example transformation:
```js
// Input:
function* gen() {
    let x = 1;
    yield x;
    let y = x + 1;
    yield y;
}

// Output:
var gen = _TrN_Sp.regeneratorRuntime.mark(function gen() {
  return _TrN_Sp.regeneratorRuntime.wrap(function _callee$(_context) {
    while(1) {
      switch(_context.prev = _context.next) {
        case 0:
          let x = 1;
          _context.next = 3; return (x);
        case 3:
          _context.sent;
          let y = x + 1;
          _context.next = 6; return (y);
        case 6:
          _context.sent;
        case 9:
        case "end":
          return _context.stop();
      }
    }
  });
})
```

**Why:** Duktape does not support generator function syntax (`function*`, `yield`). This lowering reuses the existing `regeneratorRuntime` (already used for async/await) to implement generators as state machines. The `mark()` wrapper is a no-op that tags the function, and `wrap()` creates an iterator object with a `next()` method that drives the state machine.

---

## 15. Dispatch Loop: Generator and Destructuring Integration

**Location:** Lines ~6022-6074 (`transpiler_rewrite_pass`)

**What changed:** Added dispatch entries for the new transpilation functions in the main rewrite pass loop.

**New dispatch entries:**
```c
// Generator functions
if (!handled && (strcmp(nt, "generator_function_declaration") == 0 || ...))
{
    handled = rewrite_generator_to_regenerator(edits, src, n, &claimed, overlaps);
    if (handled)
        *polysneeded |= ASYNC_PF;
}

// Computed method shorthand
if (!handled && strcmp(nt, "method_definition") == 0)
{
    handled = rewrite_computed_method_shorthand(edits, src, n, &claimed, overlaps);
}

// Destructuring declarations
if (!handled && strcmp(nt, "variable_declaration") == 0)
{
    handled = rewrite_destructuring_declaration(edits, src, n, &claimed, overlaps);
}

// Destructuring assignments
if (!handled && strcmp(nt, "expression_statement") == 0)
{
    handled = rewrite_destructuring_assignment(edits, src, n, &claimed, overlaps);
}
```

**Why:** The new transpilation functions need to be called from the main dispatch loop. Generators require the `ASYNC_PF` polyfill (which includes `regeneratorRuntime`). The dispatch order matters: generators before computed method shorthand (a generator method like `*[Symbol.iterator]()` should be handled as a generator first), and destructuring after function processing.

---

## 16. BASE_PF Polyfill Emission Logic

**Location:** Line ~6194 (`transpile_code`)

**What changed:** Added logic to ensure the `_TrN_Sp` preamble is emitted whenever any code edits are made.

**New code:**
```c
if (edits.len || polysneeded)
{
    // Always emit the _TrN_Sp preamble when code is altered
    if (edits.len && !polysneeded)
        polysneeded = BASE_PF;
    ...
}
```

**Why:** When only non-polyfill edits are made (e.g., `let` -> `var`, destructuring), `polysneeded` stays 0, and the `_TrN_Sp` global object is never created. But the transpiled code may reference `_TrN_Sp` (e.g., destructuring in code that also uses other features). Setting `BASE_PF` ensures the preamble is always present when edits are applied.
