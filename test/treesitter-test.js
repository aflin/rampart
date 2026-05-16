/* rampart-treesitter test suite.
 *
 * Verifies the module loads, exposes the expected languages, refuses
 * languages that are bundled-but-not-exposed (yaml/toml/markdown),
 * and parses + extracts the expected symbols from a sample file per
 * supported language. Sample files live in ./treesitter-tests/.
 *
 * Asserts: total symbol count + every (name, kind, line) tuple. If a
 * grammar bumps line numbers or changes a node type, this catches it.
 *
 * Expected arrays are baselines captured from running the current
 * module against the sample files. Re-baseline by running the
 * `gen_expected.js` helper (kept in /tmp/ during the original test
 * authoring) when a grammar update intentionally shifts symbols.
 */
rampart.globalize(rampart.utils);
var ts = require('rampart-treesitter');

var _nfailed = 0;

function testFeature(name, test)
{
    var error = false;
    printf("testing treesitter - %-47s - ", name);
    fflush(stdout);
    if (typeof test == 'function') {
        try {
            test = test();
        } catch(e) {
            error = e;
            test = false;
        }
    }
    if (test)
        printf("passed\n");
    else {
        printf(">>>>> FAILED <<<<<\n");
        _nfailed++;
    }
    if (error) console.log(error);
}

var SAMPLES_DIR = process.scriptPath + '/treesitter-tests';

function readSample(filename) {
    return sprintf('%s', readFile(SAMPLES_DIR + '/' + filename));
}

/* Assert: result shape is {symbols, hasErrors}, hasErrors is false
 * for valid samples, total count matches AND every expected
 * (name, kind, line) appears in symbols. Order-independent.
 * Verbose-on-fail so the user can see what changed. */
function assertSymbols(result, expected) {
    if (!result || typeof result !== 'object' || !Array.isArray(result.symbols)) {
        printf("\n  [bad return shape — expected {symbols, hasErrors}]\n");
        return false;
    }
    if (result.hasErrors) {
        printf("\n  [unexpected hasErrors=true on a clean sample]\n");
        return false;
    }
    var syms = result.symbols;
    if (syms.length !== expected.length) {
        printf("\n  [count mismatch: got %d, expected %d]\n",
               syms.length, expected.length);
        return false;
    }
    var ok = true;
    expected.forEach(function(e) {
        var match = syms.some(function(s) {
            return s.name === e.name && s.kind === e.kind && s.line === e.line;
        });
        if (!match) {
            printf("\n  [missing expected: %J]", e);
            ok = false;
        }
    });
    if (!ok) printf("\n");
    return ok;
}

/* ========== module surface ========== */

testFeature("module loads", function() {
    return typeof ts.extractSymbols === 'function'
        && Array.isArray(ts.languages)
        && ts.languages.length > 0;
});

testFeature("all expected languages exposed", function() {
    /* ts.languages reports every bundled grammar — extractSymbols
     * subset PLUS the parse-only ones (yaml, toml, markdown,
     * markdown_inline, elixir). parse() works for all of these;
     * extractSymbols works for the first group only. */
    var want = ['javascript','c','cpp','python','java','go','rust',
                'typescript','tsx','csharp','ruby','bash','kotlin',
                'php','swift','lua','dart','scala','haskell','ocaml','css',
                'yaml','toml','markdown','markdown_inline','elixir'];
    if (ts.languages.length !== want.length) {
        printf("\n  [got %d langs: %J]", ts.languages.length, ts.languages);
        return false;
    }
    return want.every(function(l){ return ts.languages.indexOf(l) >= 0; });
});

/* ========== negative path ========== */

testFeature("throws on unknown language", function() {
    try { ts.extractSymbols('foo', 'nonsense-lang'); return false; }
    catch(e) { return /unsupported/i.test(e.message); }
});

testFeature("yaml: extractSymbols throws (parse-only)", function() {
    if (ts.languages.indexOf('yaml') < 0) return false;
    try { ts.extractSymbols('a: 1', 'yaml'); return false; }
    catch(e) { return /bundled but not exposed/i.test(e.message); }
});

testFeature("toml: extractSymbols throws (parse-only)", function() {
    if (ts.languages.indexOf('toml') < 0) return false;
    try { ts.extractSymbols('[a]\nx=1', 'toml'); return false; }
    catch(e) { return /bundled but not exposed/i.test(e.message); }
});

testFeature("markdown: extractSymbols throws (parse-only)", function() {
    if (ts.languages.indexOf('markdown') < 0) return false;
    try { ts.extractSymbols('# heading', 'markdown'); return false; }
    catch(e) { return /bundled but not exposed/i.test(e.message); }
});

testFeature("elixir: extractSymbols throws (parse-only)", function() {
    /* elixir's `def foo` parses as a `call` node — the grammar
     * intentionally doesn't distinguish defs from generic function
     * calls. Available via parse() instead. */
    if (ts.languages.indexOf('elixir') < 0) return false;
    try { ts.extractSymbols('def foo, do: :ok', 'elixir'); return false; }
    catch(e) { return /bundled but not exposed/i.test(e.message); }
});

testFeature("throws without source arg", function() {
    try { ts.extractSymbols(undefined, 'javascript'); return false; }
    catch(e) { return true; }
});

testFeature("throws without language arg", function() {
    try { ts.extractSymbols('var x = 1;'); return false; }
    catch(e) { return true; }
});

/* ========== parse-error surface ========== */

testFeature("returns {symbols, hasErrors} wrapper", function() {
    var r = ts.extractSymbols('function foo() {}', 'javascript');
    return r && Array.isArray(r.symbols) && typeof r.hasErrors === 'boolean';
});

testFeature("hasErrors=false on valid input", function() {
    var r = ts.extractSymbols('function foo() {}', 'javascript');
    return r.hasErrors === false && r.symbols.length === 1;
});

testFeature("hasErrors=true on broken input", function() {
    var r = ts.extractSymbols('function foo() { @#$%^', 'javascript');
    return r.hasErrors === true;
});

testFeature("strict=true throws on broken input", function() {
    try {
        ts.extractSymbols('function foo() { @#$%^', 'javascript', {strict: true});
        return false;
    } catch (e) { return /parse error/i.test(e.message); }
});

testFeature("strict=true returns clean on valid input", function() {
    try {
        var r = ts.extractSymbols('function foo() {}', 'javascript', {strict: true});
        return r.hasErrors === false && r.symbols.length === 1;
    } catch (e) { return false; }
});

testFeature("strict=false returns partial on broken input", function() {
    /* Partial result expected — tree-sitter recovers and surfaces
     * whatever it can. hasErrors=true tells the caller it's incomplete. */
    var r = ts.extractSymbols(
        'function foo() { @#$%^\nfunction bar() { return 2; }', 'javascript');
    return r.hasErrors === true && Array.isArray(r.symbols);
});

/* ========== parse() ========== */

testFeature("parse: returns root with type/line/children", function() {
    var t = ts.parse('function foo() {}', 'javascript');
    return t.type === 'program'
        && t.line === 1
        && t.column === 1
        && Array.isArray(t.children)
        && t.children.length === 1
        && t.children[0].type === 'function_declaration';
});

testFeature("parse: nested children walked recursively", function() {
    var t = ts.parse('class C { m() {} }', 'javascript');
    var cls = t.children[0];
    return cls.type === 'class_declaration'
        && cls.children.length > 0
        /* Drill into the class body until we find the method def. */
        && JSON.stringify(t).indexOf('method_definition') > 0;
});

testFeature("parse: hasError=true on broken input", function() {
    var t = ts.parse('function foo() { @#$%^', 'javascript');
    return t.hasError === true;
});

testFeature("parse: strict=true throws on broken input", function() {
    try { ts.parse('function foo() { @#$%^', 'javascript', {strict: true}); return false; }
    catch (e) { return /parse error/i.test(e.message); }
});

testFeature("parse: works on parse-only grammar (yaml)", function() {
    var t = ts.parse('foo:\n  bar: 1\n', 'yaml');
    return t.type === 'stream' && t.hasError === false;
});

testFeature("parse: works on parse-only grammar (toml)", function() {
    var t = ts.parse('[section]\nfoo = "bar"\n', 'toml');
    return Array.isArray(t.children) && t.hasError === false;
});

testFeature("parse: works on parse-only grammar (markdown)", function() {
    var t = ts.parse('# hi\n\nparagraph\n', 'markdown');
    return Array.isArray(t.children) && t.hasError === false;
});

testFeature("parse: includeText attaches src slice per node", function() {
    var t = ts.parse('var x = 1;', 'javascript', {includeText: true});
    return typeof t.text === 'string' && t.text.indexOf('var x = 1') === 0;
});

testFeature("parse: omits text by default", function() {
    var t = ts.parse('var x = 1;', 'javascript');
    return typeof t.text === 'undefined';
});

testFeature("parse: includeUnnamed includes punct nodes", function() {
    /* Named-only walk skips '{' / '}' / ';' etc.; includeUnnamed
     * surfaces them as children too. The function body should have
     * MORE children with the flag than without. */
    var named  = ts.parse('function foo() { return 1; }', 'javascript');
    var allKids = ts.parse('function foo() { return 1; }', 'javascript', {includeUnnamed: true});
    /* Count total nodes via JSON length as a coarse comparison. */
    return JSON.stringify(allKids).length > JSON.stringify(named).length;
});

testFeature("parse: throws on unknown language", function() {
    try { ts.parse('foo', 'nonsense'); return false; }
    catch (e) { return /unsupported/i.test(e.message); }
});

testFeature("parse: throws without source arg", function() {
    try { ts.parse(undefined, 'javascript'); return false; }
    catch (e) { return true; }
});

/* ========== expected baselines (inline; generated from samples) ========== */

var expected_javascript = [
    {name: "add", kind: "function_declaration", line: 4},
    {name: "noArgs", kind: "function_declaration", line: 8},
    {name: "Greeter", kind: "class_declaration", line: 12},
    {name: "constructor", kind: "method_definition", line: 13},
    {name: "greet", kind: "method_definition", line: 16},
    {name: "farewell", kind: "method_definition", line: 19},
    {name: "FancyGreeter", kind: "class_declaration", line: 24},
    {name: "constructor", kind: "method_definition", line: 25},
    {name: "greet", kind: "method_definition", line: 29},
    {name: "topLevelLast", kind: "function_declaration", line: 39}
];

var expected_c = [
    {name: "add", kind: "function_definition", line: 9},
    {name: "read_record", kind: "function_definition", line: 13},
    {name: "Point", kind: "struct_specifier", line: 17},
    {name: "Color", kind: "enum_specifier", line: 22},
    {name: "Wrapper", kind: "type_definition", line: 24},
    {name: "Wrapper", kind: "struct_specifier", line: 24},
    /* QUIRK: typedef-wrapped anonymous struct yields both a type_definition
     * (with the typedef name) AND a phantom struct_specifier with name
     * '(anonymous)'. Locking in current behavior. */
    {name: "AnonPoint", kind: "type_definition", line: 31},
    {name: "(anonymous)", kind: "struct_specifier", line: 31},
    {name: "Variant", kind: "union_specifier", line: 36},
    {name: "callback_id", kind: "type_definition", line: 41},
    {name: "main", kind: "function_definition", line: 43},
    {name: "Point", kind: "struct_specifier", line: 44},
    /* Typedef-return-type fix: the function name is "fancy_returner",
     * NOT "my_ret_t" (the return type). Before the walk_collect fix
     * this row would have shown name="my_ret_t". */
    {name: "my_ret_t", kind: "type_definition", line: 52},
    {name: "fancy_returner", kind: "function_definition", line: 54},
    /* Pointer return with typedef-able type. The pointer_declarator
     * descent in c_proto_declarator (mirrored implicitly in
     * function_definition narrowing) lets us find the function name. */
    {name: "typed_ptr_returner", kind: "function_definition", line: 61},
    /* Function PROTOTYPES — new in this build. kind="function_declaration"
     * distinguishes them from the body-containing function_definition. */
    {name: "prototype_a", kind: "function_declaration", line: 68},
    {name: "prototype_b", kind: "function_declaration", line: 69},
    {name: "prototype_c", kind: "function_declaration", line: 70}
    /* some_global_var / another_var / an_extern_var on lines 75-77
     * are variable declarations — they must NOT appear here. The
     * c_proto_declarator function rejects them because their
     * declarator is not a function_declarator. */
];

var expected_cpp = [
    {name: "app", kind: "namespace_definition", line: 10},
    {name: "Greeter", kind: "class_specifier", line: 12},
    {name: "Greeter", kind: "function_definition", line: 14},
    /* Out-of-class method def `std::string Greeter::greet()` resolves
     * the function name (not the namespace prefix). */
    {name: "greet", kind: "function_definition", line: 21},
    {name: "Point", kind: "struct_specifier", line: 25},
    {name: "Color", kind: "enum_specifier", line: 30},
    /* QUIRK: cpp fallback omits type_identifier to avoid picking
     * template return types as function names. Side effect: plain
     * typedefs like `typedef int callback_id;` produce a type_definition
     * with name '(anonymous)'. Acceptable trade-off — locked in here. */
    {name: "(anonymous)", kind: "type_definition", line: 32},
    /* TEMPLATE fix: `T identity(T)` correctly extracts 'identity', not 'T'. */
    {name: "identity", kind: "function_definition", line: 36},
    {name: "make_range", kind: "function_definition", line: 41},
    {name: "main", kind: "function_definition", line: 47}
];

var expected_python = [
    {name: "hello", kind: "function_definition", line: 8},
    {name: "no_args", kind: "function_definition", line: 12},
    {name: "Greeter", kind: "class_definition", line: 15},
    {name: "__init__", kind: "function_definition", line: 16},
    {name: "greet", kind: "function_definition", line: 19},
    {name: "farewell", kind: "function_definition", line: 22},
    {name: "factory", kind: "function_definition", line: 26},
    {name: "FancyGreeter", kind: "class_definition", line: 29},
    {name: "__init__", kind: "function_definition", line: 30},
    {name: "greet", kind: "function_definition", line: 34},
    /* Decorated functions surface via the recursive walk inside
     * decorated_definition; no explicit decorated_definition row. */
    {name: "decorated", kind: "function_definition", line: 38},
    {name: "double_decorated", kind: "function_definition", line: 43},
    {name: "top_level_last", kind: "function_definition", line: 46}
];

var expected_java = [
    {name: "Greeter", kind: "class_declaration", line: 8},
    {name: "Greeter", kind: "constructor_declaration", line: 11},
    {name: "greet", kind: "method_declaration", line: 15},
    {name: "farewell", kind: "method_declaration", line: 19},
    {name: "factory", kind: "method_declaration", line: 23},
    {name: "GreeterImpl", kind: "class_declaration", line: 28},
    {name: "GreeterImpl", kind: "constructor_declaration", line: 29},
    {name: "onEvent", kind: "method_declaration", line: 32},
    {name: "Listener", kind: "interface_declaration", line: 38},
    {name: "onEvent", kind: "method_declaration", line: 39},
    {name: "Color", kind: "enum_declaration", line: 42},
    {name: "isWarm", kind: "method_declaration", line: 44},
    {name: "MyAnnotation", kind: "annotation_type_declaration", line: 47},
    {name: "Point", kind: "record_declaration", line: 51}
];

var expected_go = [
    {name: "Add", kind: "function_declaration", line: 10},
    {name: "noArgs", kind: "function_declaration", line: 14},
    {name: "Point", kind: "type_spec", line: 18},
    {name: "Magnitude", kind: "method_declaration", line: 22},
    {name: "Scale", kind: "method_declaration", line: 26},
    {name: "Greeter", kind: "type_spec", line: 31},
    {name: "StringList", kind: "type_spec", line: 35},
    {name: "TopLevelLast", kind: "function_declaration", line: 39},
    {name: "main", kind: "function_declaration", line: 43}
];

var expected_rust = [
    {name: "add", kind: "function_item", line: 9},
    {name: "no_args", kind: "function_item", line: 13},
    {name: "Point", kind: "struct_item", line: 17},
    /* impl_item is intentionally NOT in LANGUAGES[]; methods inside it
     * still surface as function_items via the recursive walk. */
    {name: "new", kind: "function_item", line: 23},
    {name: "magnitude", kind: "function_item", line: 26},
    {name: "Color", kind: "enum_item", line: 31},
    {name: "Greet", kind: "trait_item", line: 37},
    {name: "helpers", kind: "mod_item", line: 41},
    {name: "util", kind: "function_item", line: 42},
    {name: "HELPER_NUM", kind: "const_item", line: 43},
    {name: "MY_CONST", kind: "const_item", line: 46},
    {name: "MY_STATIC", kind: "static_item", line: 48},
    {name: "IntMap", kind: "type_item", line: 50},
    {name: "say_hi", kind: "macro_definition", line: 52},
    {name: "top_level_last", kind: "function_item", line: 56}
];

var expected_typescript = [
    {name: "add", kind: "function_declaration", line: 5},
    {name: "noArgs", kind: "function_declaration", line: 9},
    {name: "Greeter", kind: "class_declaration", line: 13},
    {name: "constructor", kind: "method_definition", line: 15},
    {name: "greet", kind: "method_definition", line: 18},
    {name: "Listener", kind: "interface_declaration", line: 23},
    {name: "StringOrNum", kind: "type_alias_declaration", line: 27},
    {name: "Callback", kind: "type_alias_declaration", line: 29},
    {name: "Status", kind: "enum_declaration", line: 31},
    {name: "InlineEnum", kind: "enum_declaration", line: 37},
    {name: "topLevelLast", kind: "function_declaration", line: 42}
];

var expected_tsx = [
    {name: "Hello", kind: "function_declaration", line: 5},
    {name: "Counter", kind: "function_declaration", line: 9},
    {name: "Modal", kind: "class_declaration", line: 13},
    {name: "constructor", kind: "method_definition", line: 15},
    {name: "render", kind: "method_definition", line: 16},
    {name: "Props", kind: "interface_declaration", line: 23},
    {name: "Renderable", kind: "type_alias_declaration", line: 28},
    {name: "Theme", kind: "enum_declaration", line: 30},
    {name: "topLevelLast", kind: "function_declaration", line: 35}
];

var expected_csharp = [
    {name: "App.Sample", kind: "namespace_declaration", line: 9},
    {name: "Greeter", kind: "class_declaration", line: 11},
    {name: "Name", kind: "property_declaration", line: 12},
    {name: "Greeter", kind: "constructor_declaration", line: 14},
    {name: "Greet", kind: "method_declaration", line: 18},
    {name: "Factory", kind: "method_declaration", line: 20},
    {name: "Point", kind: "struct_declaration", line: 25},
    {name: "X", kind: "property_declaration", line: 26},
    {name: "Y", kind: "property_declaration", line: 27},
    {name: "Point", kind: "constructor_declaration", line: 28},
    {name: "IService", kind: "interface_declaration", line: 31},
    {name: "Run", kind: "method_declaration", line: 32},
    {name: "Color", kind: "enum_declaration", line: 35},
    {name: "PersonRecord", kind: "record_declaration", line: 37},
    {name: "EventHandler", kind: "delegate_declaration", line: 39}
];

var expected_ruby = [
    {name: "Greeter", kind: "class", line: 4},
    {name: "initialize", kind: "method", line: 5},
    {name: "greet", kind: "method", line: 9},
    {name: "farewell", kind: "method", line: 13},
    {name: "factory", kind: "singleton_method", line: 17},
    {name: "FancyGreeter", kind: "class", line: 22},
    {name: "initialize", kind: "method", line: 23},
    {name: "greet", kind: "method", line: 28},
    {name: "Greetings", kind: "module", line: 33},
    {name: "hello", kind: "singleton_method", line: 34},
    {name: "goodbye", kind: "singleton_method", line: 38},
    {name: "Nested", kind: "module", line: 42},
    {name: "deep", kind: "singleton_method", line: 43},
    {name: "top_level_method", kind: "method", line: 49}
];

var expected_bash = [
    {name: "add", kind: "function_definition", line: 6},
    {name: "no_args", kind: "function_definition", line: 10},
    {name: "greet", kind: "function_definition", line: 15},
    {name: "farewell", kind: "function_definition", line: 19},
    {name: "do_setup", kind: "function_definition", line: 24},
    {name: "cleanup", kind: "function_definition", line: 30}
];

var expected_kotlin = [
    {name: "Greeter", kind: "class_declaration", line: 8},
    {name: "greet", kind: "function_declaration", line: 9},
    {name: "farewell", kind: "function_declaration", line: 11},
    {name: "factory", kind: "function_declaration", line: 16},
    {name: "FancyGreeter", kind: "class_declaration", line: 20},
    {name: "decoratedGreet", kind: "function_declaration", line: 21},
    {name: "Greet", kind: "class_declaration", line: 24},
    {name: "greet", kind: "function_declaration", line: 25},
    {name: "Singleton", kind: "object_declaration", line: 28},
    {name: "constant", kind: "property_declaration", line: 29},
    {name: "doSomething", kind: "function_declaration", line: 30},
    {name: "Color", kind: "class_declaration", line: 33},
    {name: "UserMap", kind: "type_alias", line: 35},
    {name: "topLevel", kind: "function_declaration", line: 37},
    {name: "topLevelLast", kind: "function_declaration", line: 39}
];

var expected_php = [
    {name: "App\\Sample", kind: "namespace_definition", line: 8},
    {name: "Greeter", kind: "class_declaration", line: 10},
    {name: "__construct", kind: "method_declaration", line: 13},
    {name: "greet", kind: "method_declaration", line: 17},
    {name: "farewell", kind: "method_declaration", line: 21},
    {name: "factory", kind: "method_declaration", line: 25},
    {name: "Greetable", kind: "interface_declaration", line: 30},
    {name: "greet", kind: "method_declaration", line: 31},
    {name: "Loggable", kind: "trait_declaration", line: 34},
    {name: "log", kind: "method_declaration", line: 35},
    {name: "Status", kind: "enum_declaration", line: 40},
    {name: "topLevelFn", kind: "function_definition", line: 46},
    {name: "anotherFn", kind: "function_definition", line: 50}
];

var expected_swift = [
    {name: "Greeter", kind: "class_declaration", line: 9},
    {name: "name", kind: "property_declaration", line: 10},
    {name: "init", kind: "init_declaration", line: 12},
    {name: "greet", kind: "function_declaration", line: 16},
    {name: "farewell", kind: "function_declaration", line: 20},
    {name: "factory", kind: "function_declaration", line: 24},
    {name: "Listener", kind: "protocol_declaration", line: 29},
    /* Swift's grammar models `enum`, `struct`, and `class` under the
     * same class_declaration node type. */
    {name: "Color", kind: "class_declaration", line: 33},
    {name: "red", kind: "enum_entry", line: 34},
    {name: "green", kind: "enum_entry", line: 35},
    {name: "blue", kind: "enum_entry", line: 36},
    {name: "Point", kind: "class_declaration", line: 39},
    {name: "x", kind: "property_declaration", line: 40},
    {name: "y", kind: "property_declaration", line: 41},
    {name: "topLevel", kind: "function_declaration", line: 44},
    {name: "UserMap", kind: "typealias_declaration", line: 48},
    {name: "topLevelLast", kind: "function_declaration", line: 50}
];

var expected_lua = [
    {name: "add", kind: "function_declaration", line: 5},
    {name: "no_args", kind: "function_declaration", line: 9},
    /* Member-style `function M.method(x)` parses as a single
     * function_declaration with name "M.method". */
    {name: "M.method", kind: "function_declaration", line: 18},
    {name: "M.other", kind: "function_declaration", line: 22},
    {name: "helper", kind: "function_declaration", line: 27},
    {name: "top_level_last", kind: "function_declaration", line: 31}
];

var expected_dart = [
    {name: "Greeter", kind: "class_definition", line: 8},
    {name: "Greeter", kind: "constructor_signature", line: 11},
    /* Methods inside classes emit as function_signature, not
     * method_signature — we deliberately drop method_signature from
     * the LANGUAGES[] list to avoid double-counting the same symbol. */
    {name: "greet", kind: "function_signature", line: 13},
    {name: "farewell", kind: "function_signature", line: 15},
    {name: "factory", kind: "function_signature", line: 19},
    {name: "FancyGreeter", kind: "class_definition", line: 22},
    {name: "FancyGreeter", kind: "constructor_signature", line: 24},
    {name: "greet", kind: "function_signature", line: 27},
    {name: "Logger", kind: "mixin_declaration", line: 30},
    {name: "log", kind: "function_signature", line: 31},
    {name: "StringExt", kind: "extension_declaration", line: 36},
    {name: "reverse", kind: "function_signature", line: 37},
    {name: "Color", kind: "enum_declaration", line: 42},
    {name: "topLevel", kind: "function_signature", line: 44},
    {name: "topLevelLast", kind: "function_signature", line: 48}
];

var expected_scala = [
    {name: "App", kind: "object_definition", line: 8},
    {name: "main", kind: "function_definition", line: 9},
    {name: "Greeter", kind: "class_definition", line: 14},
    {name: "greet", kind: "function_definition", line: 15},
    {name: "farewell", kind: "function_definition", line: 17},
    {name: "FancyGreeter", kind: "class_definition", line: 22},
    {name: "greet", kind: "function_definition", line: 23},
    {name: "Logger", kind: "trait_definition", line: 26},
    {name: "log", kind: "function_declaration", line: 27},
    {name: "Named", kind: "trait_definition", line: 30},
    {name: "name", kind: "val_declaration", line: 31},
    {name: "display", kind: "function_definition", line: 32},
    {name: "Color", kind: "enum_definition", line: 35},
    {name: "StringMap", kind: "type_definition", line: 39},
    /* QUIRK: typed val_definition with type ascription
     * (`val constant: Int = 42`) lands as '(anonymous)' because the
     * binding pattern is structured differently from plain
     * `val foo = ...` in scala's grammar. */
    {name: "(anonymous)", kind: "val_definition", line: 41},
    {name: "topLevel", kind: "function_definition", line: 43},
    {name: "topLevelLast", kind: "function_definition", line: 45}
];

var expected_haskell = [
    {name: "Demo", kind: "module", line: 13},
    {name: "foo", kind: "signature", line: 15},
    /* QUIRK: tree-sitter-haskell uses the same `function` node type
     * for function definitions AND function-type expressions inside
     * signatures (e.g. `Int -> Int` is parsed as a `function` node).
     * Each type signature thus introduces a phantom `function` row
     * with name like "Int", "String", or the type variable. Locked
     * in here. */
    {name: "Int", kind: "function", line: 15},
    {name: "foo", kind: "function", line: 16},
    {name: "bar", kind: "signature", line: 18},
    {name: "String", kind: "function", line: 18},
    {name: "String", kind: "function", line: 18},
    {name: "bar", kind: "function", line: 19},
    {name: "Color", kind: "data_type", line: 21},
    {name: "Wrap", kind: "newtype", line: 23},
    {name: "Greet", kind: "class", line: 27},
    {name: "greet", kind: "signature", line: 28},
    {name: "a", kind: "function", line: 28},
    {name: "Greet", kind: "instance", line: 30},
    {name: "greet", kind: "function", line: 31},
    {name: "topLevelLast", kind: "signature", line: 33}
];

var expected_ocaml = [
    {name: "add", kind: "value_definition", line: 7},
    {name: "no_args", kind: "value_definition", line: 9},
    {name: "color", kind: "type_binding", line: 11},
    {name: "point", kind: "type_binding", line: 13},
    {name: "M", kind: "module_binding", line: 15},
    {name: "bar", kind: "value_definition", line: 16},
    {name: "baz", kind: "value_definition", line: 17},
    {name: "Other", kind: "module_binding", line: 20},
    {name: "helper", kind: "value_definition", line: 21},
    /* Exception names extract via the constructor_name fallback
     * (exceptions are constructors in OCaml). */
    {name: "NotFound", kind: "exception_definition", line: 24},
    {name: "InvalidInput", kind: "exception_definition", line: 25},
    {name: "top_level_last", kind: "value_definition", line: 27}
];

var expected_css = [
    /* For rule_set the "name" is the first selector descendant —
     * class_selector / id_selector / tag_name / pseudo_class_selector.
     * Compound selectors like .button.primary parse as a single
     * class_selector node spanning the whole compound. */
    {name: ".button", kind: "rule_set", line: 7},
    {name: ".button.primary", kind: "rule_set", line: 12},
    {name: "#main-nav", kind: "rule_set", line: 17},
    {name: "h1", kind: "rule_set", line: 22},
    {name: "h2", kind: "rule_set", line: 27},
    {name: "a:hover", kind: "rule_set", line: 32},
    {name: ":root", kind: "rule_set", line: 37},
    {name: "fadeIn", kind: "keyframes_statement", line: 43},
    {name: "slideOut", kind: "keyframes_statement", line: 48},
    /* @media wraps inner rule_sets; both the media_statement (with the
     * inner selector as its name, found via recursion) AND the inner
     * rule_set itself surface. */
    {name: ".button", kind: "media_statement", line: 54},
    {name: ".button", kind: "rule_set", line: 55}
];

/* ========== per-language extraction tests ========== */

testFeature("javascript: 10 symbols", function() {
    return assertSymbols(ts.extractSymbols(readSample('javascript.js'), 'javascript'),
                         expected_javascript);
});

testFeature("c: 18 symbols returned", function() {
    return assertSymbols(ts.extractSymbols(readSample('c.c'), 'c'),
                         expected_c);
});

testFeature("cpp: 10 symbols (template name fix)", function() {
    return assertSymbols(ts.extractSymbols(readSample('cpp.cpp'), 'cpp'),
                         expected_cpp);
});

testFeature("python: 13 symbols (decorated fns)", function() {
    return assertSymbols(ts.extractSymbols(readSample('python.py'), 'python'),
                         expected_python);
});

testFeature("java: 14 symbols (record, annotation)", function() {
    return assertSymbols(ts.extractSymbols(readSample('java.java'), 'java'),
                         expected_java);
});

testFeature("go: 9 symbols (type_spec)", function() {
    return assertSymbols(ts.extractSymbols(readSample('go.go'), 'go'),
                         expected_go);
});

testFeature("rust: 15 symbols (impl methods via recursion)", function() {
    return assertSymbols(ts.extractSymbols(readSample('rust.rs'), 'rust'),
                         expected_rust);
});

testFeature("typescript: 11 symbols (aliases, enums)", function() {
    return assertSymbols(ts.extractSymbols(readSample('typescript.ts'), 'typescript'),
                         expected_typescript);
});

testFeature("tsx: 9 symbols (JSX bodies ok)", function() {
    return assertSymbols(ts.extractSymbols(readSample('tsx.tsx'), 'tsx'),
                         expected_tsx);
});

testFeature("csharp: 15 symbols (record, delegate)", function() {
    return assertSymbols(ts.extractSymbols(readSample('csharp.cs'), 'csharp'),
                         expected_csharp);
});

testFeature("ruby: 14 symbols (singleton methods)", function() {
    return assertSymbols(ts.extractSymbols(readSample('ruby.rb'), 'ruby'),
                         expected_ruby);
});

testFeature("bash: 6 symbols (both fn syntaxes)", function() {
    return assertSymbols(ts.extractSymbols(readSample('bash.sh'), 'bash'),
                         expected_bash);
});

testFeature("kotlin: 15 symbols (object, typealias)", function() {
    return assertSymbols(ts.extractSymbols(readSample('kotlin.kt'), 'kotlin'),
                         expected_kotlin);
});

testFeature("php: 13 symbols (trait, enum)", function() {
    return assertSymbols(ts.extractSymbols(readSample('php.php'), 'php'),
                         expected_php);
});

testFeature("swift: 17 symbols (enum entries)", function() {
    return assertSymbols(ts.extractSymbols(readSample('swift.swift'), 'swift'),
                         expected_swift);
});

testFeature("lua: 6 symbols (incl member-style)", function() {
    return assertSymbols(ts.extractSymbols(readSample('lua.lua'), 'lua'),
                         expected_lua);
});

testFeature("dart: 15 symbols (mixin, extension)", function() {
    return assertSymbols(ts.extractSymbols(readSample('dart.dart'), 'dart'),
                         expected_dart);
});

testFeature("scala: 17 symbols (object, trait, enum)", function() {
    return assertSymbols(ts.extractSymbols(readSample('scala.scala'), 'scala'),
                         expected_scala);
});

testFeature("haskell: 16 symbols (phantom fn rows)", function() {
    return assertSymbols(ts.extractSymbols(readSample('haskell.hs'), 'haskell'),
                         expected_haskell);
});

testFeature("ocaml: 12 symbols (let, type, module, exn)", function() {
    return assertSymbols(ts.extractSymbols(readSample('ocaml.ml'), 'ocaml'),
                         expected_ocaml);
});

testFeature("css: 11 symbols (selectors, @keyframes)", function() {
    return assertSymbols(ts.extractSymbols(readSample('css.css'), 'css'),
                         expected_css);
});

process.exit(_nfailed ? 1 : 0);
