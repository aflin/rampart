/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * rampart-treesitter: JS-facing tree-sitter binding.
 *
 * v1 surface — JavaScript only, proving the build wiring and shape:
 *   var ts = require('rampart-treesitter');
 *   ts.languages                     -> ['javascript']
 *   ts.extractSymbols(src, lang)     -> array of
 *      {name, kind, line, column, signature, startByte, endByte}
 *
 * Symbol-resolution model: the main rampart binary already links the
 * tree-sitter runtime and the JS grammar (for the transpiler). This
 * module declares those functions `extern` and they resolve from the
 * main process at dlopen time — exactly the pattern rampart-totext.c
 * uses for libdeflate. The .so itself contains only this file's code,
 * keeping it small and avoiding duplicate runtime objects.
 *
 * Adding a language later means: vendor the grammar's parser.c/scanner.c
 * under extern/tree-sitter/tree-sitter-<lang>/ (mirroring how the JS
 * grammar lives at extern/tree-sitter/tree-sitter-javascript/), wire
 * its parser.c (and scanner.c if present) into the rampart binary's
 * add_executable so the main process owns the tree_sitter_<lang>()
 * symbol, then add a row to LANGUAGES[] below. No changes to the .so
 * link list — symbols resolve at load time.
 */

#include "rampart.h"
#include <tree_sitter/api.h>
#include <string.h>
#include <stdlib.h>

/* Grammar entry points. `tree_sitter_javascript` lives in the main
 * rampart binary (transpiler uses it) and resolves at dlopen. The rest
 * are statically linked into this .so via the CMake source list. */
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_java(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_rust(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_tsx(void);
extern const TSLanguage *tree_sitter_c_sharp(void);
extern const TSLanguage *tree_sitter_ruby(void);
extern const TSLanguage *tree_sitter_bash(void);
extern const TSLanguage *tree_sitter_kotlin(void);
extern const TSLanguage *tree_sitter_php_only(void);
extern const TSLanguage *tree_sitter_swift(void);
extern const TSLanguage *tree_sitter_lua(void);
extern const TSLanguage *tree_sitter_dart(void);
extern const TSLanguage *tree_sitter_scala(void);
extern const TSLanguage *tree_sitter_haskell(void);
extern const TSLanguage *tree_sitter_ocaml(void);
/* Elixir grammar is bundled but not surfaced via extractSymbols. Its
 * grammar deliberately parses `def foo` / `defmodule Foo` as generic
 * `call` nodes (because in Elixir those ARE function calls, evaluated
 * as macros). Extracting symbols requires walking call nodes and
 * filtering by the callee name — per-language logic that doesn't fit
 * the current LANGUAGES[]-table dispatch. Available for a future
 * parse() API or a dedicated elixir helper. */
extern const TSLanguage *tree_sitter_elixir(void);
/* Markup / config grammars — linked but not currently surfaced via
 * extractSymbols. The symbols remain present for a future parse() API
 * or other callers that want to do their own AST walks. */
extern const TSLanguage *tree_sitter_css(void);
extern const TSLanguage *tree_sitter_toml(void);
extern const TSLanguage *tree_sitter_yaml(void);
extern const TSLanguage *tree_sitter_markdown(void);
extern const TSLanguage *tree_sitter_markdown_inline(void);

/* Per-language dispatch table.
 *   fn_node_types       — NULL-terminated array of tree-sitter node-type
 *                         names that represent the "top-level definitions"
 *                         extractSymbols should surface.
 *   name_field          — tree-sitter field name carrying the symbol's
 *                         identifier (works for JS-style "name=X" grammars).
 *   fallback_name_types — NULL-terminated list of node types to scan for
 *                         as a fallback when field lookup misses. Used for
 *                         grammars (like C) where the identifier is buried
 *                         inside a declarator chain; we descend depth-first
 *                         and pick the first matching descendant. */
typedef struct {
    const char  *name;
    const TSLanguage *(*get_language)(void);
    const char  *fn_node_types[12];       /* NULL-terminated */
    const char  *name_field;              /* NULL → skip field lookup */
    const char  *fallback_name_types[12]; /* NULL-terminated */
} lang_def_t;

static const lang_def_t LANGUAGES[] = {
    {
        "javascript",
        tree_sitter_javascript,
        { "function_declaration", "class_declaration", "method_definition", NULL },
        "name",
        { NULL }
    },
    {
        "c",
        tree_sitter_c,
        /* preproc_def covers `#define X value`; preproc_function_def
         * covers `#define FOO(x) ...`. Both have an identifier as their
         * first named child (the macro name). They get treated as
         * symbols so list_project_symbols / recall_project_def can find
         * macros without a separate code path. */
        { "function_definition", "struct_specifier", "enum_specifier",
          "union_specifier", "type_definition",
          "preproc_def", "preproc_function_def", NULL },
        "name",
        /* C function_definition has no "name" field; the identifier is
         * buried inside the declarator. First identifier or type_identifier
         * descendant in source order is what we want. */
        { "identifier", "type_identifier", NULL }
    },
    {
        "cpp",
        tree_sitter_cpp,
        /* C++ shares C's function_definition structure but adds class,
         * namespace, and template declarations. Skipping template_declaration
         * here — its body is a class/function we want to capture at the
         * inner level so we don't double-emit.
         * preproc_def / preproc_function_def included for symmetry with
         * the C grammar — both capture macro names. */
        { "function_definition", "class_specifier", "struct_specifier",
          "enum_specifier", "union_specifier", "namespace_definition",
          "type_definition",
          "preproc_def", "preproc_function_def", NULL },
        "name",
        /* Only "identifier" in the fallback list — NOT type_identifier.
         * C++ template functions like `T identity(T)` have the return
         * type's type_identifier appear before the function's identifier
         * in source order; including type_identifier here would make
         * depth-first match pick the return type name as the function
         * name. With identifier-only, qualified-name functions like
         * `int ns::Foo::bar()` still resolve correctly because
         * namespace_identifier is a distinct node type. */
        { "identifier", NULL }
    },
    {
        "python",
        tree_sitter_python,
        /* function_definition and class_definition cover top-level defs
         * AND decorated ones — the recursive walk picks them up inside
         * decorated_definition wrappers, so no need to list that. */
        { "function_definition", "class_definition", NULL },
        "name",
        { NULL }
    },
    {
        "java",
        tree_sitter_java,
        { "method_declaration", "constructor_declaration",
          "class_declaration", "interface_declaration",
          "enum_declaration", "annotation_type_declaration",
          "record_declaration", NULL },
        "name",
        { NULL }
    },
    {
        "go",
        tree_sitter_go,
        /* type_spec sits inside type_declaration and is where the actual
         * named type lives — emitting it directly avoids the wrapper
         * having no useful name field. */
        { "function_declaration", "method_declaration", "type_spec", NULL },
        "name",
        { NULL }
    },
    {
        "rust",
        tree_sitter_rust,
        /* impl_item has no `name` field — its identity is "impl <Type>"
         * or "impl <Trait> for <Type>". Skipping it for v1; the methods
         * inside (function_item) are picked up by the recursive walk. */
        { "function_item", "struct_item", "enum_item", "trait_item",
          "type_item", "mod_item", "const_item", "static_item",
          "macro_definition", NULL },
        "name",
        { NULL }
    },
    {
        "typescript",
        tree_sitter_typescript,
        { "function_declaration", "class_declaration", "method_definition",
          "abstract_class_declaration", "abstract_method_signature",
          "interface_declaration", "type_alias_declaration",
          "enum_declaration", NULL },
        "name",
        { NULL }
    },
    {
        "tsx",
        tree_sitter_tsx,
        /* TSX is a superset of TypeScript syntax for files that include
         * JSX expressions; its symbol-extraction shape is identical. */
        { "function_declaration", "class_declaration", "method_definition",
          "abstract_class_declaration", "abstract_method_signature",
          "interface_declaration", "type_alias_declaration",
          "enum_declaration", NULL },
        "name",
        { NULL }
    },
    {
        /* tree-sitter calls the grammar "c_sharp" (underscore) — keep
         * the JS-facing name "csharp" to match what users will type. */
        "csharp",
        tree_sitter_c_sharp,
        { "method_declaration", "constructor_declaration",
          "class_declaration", "struct_declaration",
          "interface_declaration", "enum_declaration",
          "record_declaration", "delegate_declaration",
          "namespace_declaration", "property_declaration", NULL },
        "name",
        { NULL }
    },
    {
        "ruby",
        tree_sitter_ruby,
        /* Ruby's method/class structure. `singleton_method` is the
         * "def self.foo" form. */
        { "method", "singleton_method", "class", "module", NULL },
        "name",
        { NULL }
    },
    {
        "bash",
        tree_sitter_bash,
        /* Bash function definitions: `foo() { ... }` and `function foo
         * { ... }` — both parse to function_definition. */
        { "function_definition", NULL },
        "name",
        { NULL }
    },
    {
        "kotlin",
        tree_sitter_kotlin,
        { "function_declaration", "class_declaration",
          "object_declaration", "property_declaration",
          "type_alias", NULL },
        "simple_identifier",  /* kotlin grammar uses simple_identifier instead of plain "name" */
        { "simple_identifier", "type_identifier", NULL }
    },
    {
        /* PHP grammar function is tree_sitter_php_only (we vendored the
         * php_only variant, which assumes pure-PHP files). Surface name
         * "php" for ergonomic ts.extractSymbols(src, 'php'). */
        "php",
        tree_sitter_php_only,
        { "function_definition", "method_declaration",
          "class_declaration", "interface_declaration",
          "trait_declaration", "enum_declaration",
          "namespace_definition", NULL },
        "name",
        { NULL }
    },
    {
        "swift",
        tree_sitter_swift,
        /* Swift uses class_declaration/protocol_declaration etc. The
         * declaration node has a "name" field carrying a simple_identifier
         * or type_identifier in most cases. */
        { "function_declaration", "class_declaration",
          "protocol_declaration", "deinit_declaration",
          "init_declaration", "subscript_declaration",
          "typealias_declaration", "associatedtype_declaration",
          "property_declaration", "enum_declaration",
          "enum_entry", NULL },
        "name",
        { "simple_identifier", "type_identifier", NULL }
    },
    {
        "lua",
        tree_sitter_lua,
        /* Lua doesn't have classes natively; functions and tables are
         * the primary structure. function_declaration covers `function
         * foo()` and `function M.foo()` at top level; local_function
         * for `local function foo()`. */
        { "function_declaration", "local_function", "function_definition_statement", NULL },
        "name",
        { NULL }
    },
    {
        "dart",
        tree_sitter_dart,
        /* Dart top-level + class member defs. We use function_signature
         * (NOT method_signature) — method_signature wraps function_signature
         * in the AST for class methods, so matching both would emit
         * the same symbol twice. function_signature alone catches
         * both top-level functions and class methods. constructor /
         * getter / setter / factory have their own *_signature nodes
         * for distinct shapes. */
        { "class_definition", "mixin_declaration", "enum_declaration",
          "extension_declaration", "function_signature",
          "getter_signature", "setter_signature", "constructor_signature",
          "factory_constructor_signature", NULL },
        "name",
        { "identifier", NULL }
    },
    {
        "scala",
        tree_sitter_scala,
        /* Scala 3 + Scala 2 features. function_declaration is abstract,
         * function_definition has a body. object_definition covers
         * Scala's singletons. */
        { "function_declaration", "function_definition",
          "class_definition", "object_definition", "trait_definition",
          "enum_definition", "given_definition", "type_definition",
          "val_definition", "val_declaration", NULL },
        "name",
        { NULL }
    },
    {
        "haskell",
        tree_sitter_haskell,
        /* Haskell top-level defs use unusual node names: `function`
         * for `foo x = ...`, `signature` for `foo :: Int -> Int`,
         * `data_type` / `newtype` / `type_synonym` for type decls,
         * `class` / `instance` for typeclass stuff.
         *
         * QUIRK: tree-sitter-haskell uses the SAME `function` node
         * type for function DEFINITIONS and for function-TYPE
         * expressions inside signatures (e.g. `Int -> Int`). The
         * walker can't distinguish them, so type sigs introduce
         * phantom function rows with names like 'Int' or 'a'. Locked
         * in tests; agent users see them as noise but they're
         * mostly harmless. */
        { "function", "signature", "data_type", "newtype",
          "type_synonym", "class", "instance", "module", NULL },
        "name",
        /* haskell grammar uses 'variable' for function names, not plain
         * identifier. type_identifier-ish for types. */
        { "variable", "name", "constructor", "module_id", NULL }
    },
    {
        "ocaml",
        tree_sitter_ocaml,
        /* OCaml top-level: value_definition for `let foo = ...`,
         * type_binding for type defs, module_binding for modules,
         * exception_definition for exceptions (where the name is a
         * constructor_name since exceptions are constructors). */
        { "value_definition", "type_binding", "module_binding",
          "exception_definition", "class_binding", "class_definition",
          "external", "method_definition", NULL },
        "name",
        { "value_name", "type_constructor", "module_name",
          "value_pattern", "constructor_name", NULL }
    },
    {
        /* CSS is not a traditional programming language but has real
         * "definitions": named rules (rule_set with class/id/tag
         * selectors), @keyframes (named), @font-face. tree-sitter-css
         * uses positional children for rule_set's selectors (no `name`
         * field), so we lean on the fallback walker: the first
         * selector descendant becomes the symbol's name. For
         * `.button { ... }` the name is `.button`; for `#main` it's
         * `#main`; for `h1, h2 { ... }` it's the first selector found
         * (h1). For @keyframes the `name` field works directly. */
        "css",
        tree_sitter_css,
        { "rule_set", "keyframes_statement",
          "font_face_statement", "media_statement", NULL },
        "name",
        { "class_selector", "id_selector", "tag_name",
          "attribute_selector", "pseudo_class_selector",
          "pseudo_element_selector", "nesting_selector",
          "keyframes_name", NULL }
    },
    /* Below: grammars bundled in the module for parse() but NOT
     * exposed via extractSymbols. fn_node_types[0]==NULL is the
     * marker — extractSymbols throws "bundled but not exposed", parse
     * works normally. ts.languages filters these out; ts.parseLanguages
     * lists them. */
    { "yaml",            tree_sitter_yaml,            { NULL }, NULL, { NULL } },
    { "toml",            tree_sitter_toml,            { NULL }, NULL, { NULL } },
    { "markdown",        tree_sitter_markdown,        { NULL }, NULL, { NULL } },
    { "markdown_inline", tree_sitter_markdown_inline, { NULL }, NULL, { NULL } },
    { "elixir",          tree_sitter_elixir,          { NULL }, NULL, { NULL } },
    { NULL, NULL, { NULL }, NULL, { NULL } }
};

static const lang_def_t *find_language(const char *name)
{
    for (int i = 0; LANGUAGES[i].name; i++) {
        if (strcmp(LANGUAGES[i].name, name) == 0) return &LANGUAGES[i];
    }
    return NULL;
}

/* Match `type` against the language's known function-like node types. */
static int is_fn_node(const lang_def_t *lang, const char *type)
{
    for (int i = 0; lang->fn_node_types[i]; i++) {
        if (strcmp(lang->fn_node_types[i], type) == 0) return 1;
    }
    return 0;
}

/* Depth-first search under `node` for the first descendant whose type is
 * in the language's fallback_name_types list. Returns (TSNode){0} if
 * nothing matches. Used when ts_node_child_by_field_name comes up empty
 * — typical for C function_definitions where the identifier is several
 * levels deep inside a declarator chain. */
static TSNode find_fallback_name(const lang_def_t *lang, TSNode node)
{
    if (!lang->fallback_name_types[0]) return (TSNode){0};
    const char *type = ts_node_type(node);
    for (int i = 0; lang->fallback_name_types[i]; i++) {
        if (strcmp(type, lang->fallback_name_types[i]) == 0) return node;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode hit = find_fallback_name(lang, ts_node_named_child(node, i));
        if (!ts_node_is_null(hit)) return hit;
    }
    return (TSNode){0};
}

/* Push a string copy of the source range covered by `node` onto the
 * duktape stack. Caller owns the stack slot. */
static void push_node_text(duk_context *ctx, const char *src, TSNode node)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end   = ts_node_end_byte(node);
    duk_push_lstring(ctx, src + start, (duk_size_t)(end - start));
}

/* Cap a node's source range when pushed — used for `signature`, where
 * we don't want the whole function body inlined into the result. We
 * take up to `max` bytes from the start of the node and append "..."
 * if there was more. */
#define SIGNATURE_MAX 256

static void push_signature(duk_context *ctx, const char *src, TSNode node)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end   = ts_node_end_byte(node);
    uint32_t len   = end - start;
    if (len <= SIGNATURE_MAX) {
        duk_push_lstring(ctx, src + start, (duk_size_t)len);
        return;
    }
    /* Truncate at first '{' if there is one before the cap — for
     * function bodies, that's a natural break between signature and
     * implementation. Otherwise hard-cap at SIGNATURE_MAX. */
    uint32_t cut = SIGNATURE_MAX;
    for (uint32_t i = start; i < start + SIGNATURE_MAX && i < end; i++) {
        if (src[i] == '{') { cut = i - start; break; }
    }
    duk_push_lstring(ctx, src + start, (duk_size_t)cut);
    duk_push_string(ctx, "...");
    duk_concat(ctx, 2);
}

/* True for C and C++ — those grammars share a function_definition shape
 * where the identifier is buried inside a function_declarator subtree
 * via the `declarator` field, AND prototypes appear as `declaration`
 * nodes (free-function decls in headers) or `field_declaration` (class
 * member decls inside a class body). Both cases need treatment that
 * other tree-sitter grammars don't. */
static int is_c_family(const lang_def_t *lang)
{
    return lang && lang->name &&
        (strcmp(lang->name, "c") == 0 || strcmp(lang->name, "cpp") == 0);
}

/* For a C/C++ `declaration` node, determine whether its declarator is
 * a function_declarator (i.e., this is a function prototype, not a
 * variable / type / extern declaration). Returns the function_declarator
 * subtree on match, else (TSNode){0}. */
static TSNode c_proto_declarator(TSNode decl)
{
    TSNode d = ts_node_child_by_field_name(decl, "declarator", 10);
    if (ts_node_is_null(d)) return (TSNode){0};
    /* Walk down through pointer_declarator wrappers (for return-type
     * pointers like `char *foo();`). The innermost declarator is the
     * function_declarator if this is a prototype. */
    while (!ts_node_is_null(d) &&
           strcmp(ts_node_type(d), "pointer_declarator") == 0) {
        TSNode inner = ts_node_child_by_field_name(d, "declarator", 10);
        if (ts_node_is_null(inner)) break;
        d = inner;
    }
    if (!ts_node_is_null(d) &&
        strcmp(ts_node_type(d), "function_declarator") == 0)
        return d;
    return (TSNode){0};
}

/* Visit `node`. If it's a definition we care about, push one object
 * onto the array at `arr_idx`. Then recurse into children — class
 * bodies contain method_definitions which we want to surface.
 *
 * C/C++ special cases:
 *   - function_definition: the identifier lives inside the
 *     `declarator` subtree, NOT at the top of the node. Restricting
 *     the fallback search to that subtree avoids picking the return
 *     type (a type_identifier for typedef returns like `duk_ret_t fn`)
 *     as the function name.
 *   - declaration: if the declarator is a function_declarator, this is
 *     a function prototype (typical of headers). Emit it with
 *     kind="function_declaration" so callers can filter; the name
 *     extraction uses the same declarator-subtree narrowing.
 */
static void walk_collect(duk_context *ctx, const char *src,
                         const lang_def_t *lang,
                         TSNode node, duk_idx_t arr_idx, duk_uarridx_t *out_i)
{
    const char *type = ts_node_type(node);

    /* Determine whether this node should be emitted, what subtree to
     * search for the name, and what kind label to attach. */
    int emit               = is_fn_node(lang, type);
    const char *kind_label = type;
    TSNode subject         = node;     /* node-wide name search by default */

    if (is_c_family(lang)) {
        if (strcmp(type, "function_definition") == 0) {
            /* Narrow the name search to the declarator subtree so the
             * return type's type_identifier (e.g., duk_ret_t) can't be
             * mistaken for the function name via depth-first match. */
            TSNode d = ts_node_child_by_field_name(node, "declarator", 10);
            if (!ts_node_is_null(d)) subject = d;
        } else if (!emit && strcmp(type, "declaration") == 0) {
            /* Function prototype detection. Only emit when the
             * declarator is a function_declarator; ordinary variable
             * and typedef-extern declarations stay invisible. */
            TSNode d = c_proto_declarator(node);
            if (!ts_node_is_null(d)) {
                emit       = 1;
                kind_label = "function_declaration";
                subject    = d;
            }
        }
    }

    if (emit) {
        TSNode name_node = (TSNode){0};
        /* Field-name lookup is meaningless on a `declaration` node
         * (its top-level field is `declarator`, not `name`); skip it
         * when we routed through the prototype branch above. The
         * existing field-name lookup is fine for everything else. */
        int try_field = lang->name_field &&
                        !(is_c_family(lang) &&
                          strcmp(type, "declaration") == 0);
        if (try_field) {
            name_node = ts_node_child_by_field_name(
                node, lang->name_field, (uint32_t)strlen(lang->name_field));
        }
        if (ts_node_is_null(name_node))
            name_node = find_fallback_name(lang, subject);
        TSPoint p = ts_node_start_point(node);

        duk_push_object(ctx);

        if (!ts_node_is_null(name_node)) {
            push_node_text(ctx, src, name_node);
        } else {
            duk_push_string(ctx, "(anonymous)");
        }
        duk_put_prop_string(ctx, -2, "name");

        duk_push_string(ctx, kind_label);
        duk_put_prop_string(ctx, -2, "kind");

        duk_push_uint(ctx, p.row + 1);          /* 1-based */
        duk_put_prop_string(ctx, -2, "line");
        duk_push_uint(ctx, p.column + 1);
        duk_put_prop_string(ctx, -2, "column");

        push_signature(ctx, src, node);
        duk_put_prop_string(ctx, -2, "signature");

        duk_push_uint(ctx, ts_node_start_byte(node));
        duk_put_prop_string(ctx, -2, "startByte");
        duk_push_uint(ctx, ts_node_end_byte(node));
        duk_put_prop_string(ctx, -2, "endByte");

        duk_put_prop_index(ctx, arr_idx, (*out_i)++);
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        walk_collect(ctx, src, lang,
                     ts_node_named_child(node, i), arr_idx, out_i);
    }
}

/* Recursive serializer: push a JS object describing `node` onto the
 * duktape stack. Used by parse() to materialize the entire tree as
 * nested plain objects.
 *
 * include_text:    if true, attach a `text` field with the source
 *                  bytes covering this node. Off by default to keep
 *                  result size manageable for big files.
 * include_unnamed: if true, walk ALL children (named + anonymous —
 *                  e.g. keywords, punctuation). Off by default; most
 *                  callers want only the named (semantic) nodes. */
static void push_node_tree(duk_context *ctx, TSNode node, const char *src,
                           int include_text, int include_unnamed)
{
    duk_idx_t obj = duk_push_object(ctx);

    duk_push_string(ctx, ts_node_type(node));
    duk_put_prop_string(ctx, obj, "type");

    TSPoint sp = ts_node_start_point(node);
    duk_push_uint(ctx, sp.row + 1);
    duk_put_prop_string(ctx, obj, "line");
    duk_push_uint(ctx, sp.column + 1);
    duk_put_prop_string(ctx, obj, "column");

    duk_push_uint(ctx, ts_node_start_byte(node));
    duk_put_prop_string(ctx, obj, "startByte");
    duk_push_uint(ctx, ts_node_end_byte(node));
    duk_put_prop_string(ctx, obj, "endByte");

    duk_push_boolean(ctx, ts_node_is_error(node));
    duk_put_prop_string(ctx, obj, "isError");
    duk_push_boolean(ctx, ts_node_has_error(node));
    duk_put_prop_string(ctx, obj, "hasError");

    if (include_text) {
        uint32_t s = ts_node_start_byte(node);
        uint32_t e = ts_node_end_byte(node);
        duk_push_lstring(ctx, src + s, (duk_size_t)(e - s));
        duk_put_prop_string(ctx, obj, "text");
    }

    /* Children — named-only by default. Each child is recursed and
     * placed at its index in the children array. */
    uint32_t n = include_unnamed
        ? ts_node_child_count(node)
        : ts_node_named_child_count(node);
    duk_idx_t children = duk_push_array(ctx);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = include_unnamed
            ? ts_node_child(node, i)
            : ts_node_named_child(node, i);
        push_node_tree(ctx, child, src, include_text, include_unnamed);
        duk_put_prop_index(ctx, children, i);
    }
    duk_put_prop_string(ctx, obj, "children");
}

static duk_ret_t es_parse(duk_context *ctx)
{
    duk_size_t src_sz;
    const char *src = REQUIRE_LSTRING(ctx, 0, &src_sz,
        "parse: first argument must be the source string");
    const char *lang_name = REQUIRE_STRING(ctx, 1,
        "parse: second argument must be the language name");

    /* opts: {strict, includeText, includeUnnamed} — all optional. */
    int strict = 0;
    int include_text = 0;
    int include_unnamed = 0;
    if (duk_is_object(ctx, 2)) {
        if (duk_get_prop_string(ctx, 2, "strict"))
            strict = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "includeText"))
            include_text = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "includeUnnamed"))
            include_unnamed = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
    }

    const lang_def_t *lang = find_language(lang_name);
    if (!lang)
        RP_THROW(ctx, "parse: unsupported language '%s'", lang_name);
    /* parse() accepts grammars even without extractSymbols support
     * (fn_node_types == NULL) — yaml, toml, markdown, elixir. */

    TSParser *parser = ts_parser_new();
    if (!parser)
        RP_THROW(ctx, "parse: ts_parser_new failed");
    if (!ts_parser_set_language(parser, lang->get_language())) {
        ts_parser_delete(parser);
        RP_THROW(ctx,
            "parse: ts_parser_set_language failed for '%s' "
            "(grammar ABI mismatch with runtime?)", lang_name);
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)src_sz);
    if (!tree) {
        ts_parser_delete(parser);
        RP_THROW(ctx, "parse: parse failed");
    }

    TSNode root = ts_tree_root_node(tree);
    if (strict && ts_node_has_error(root)) {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        RP_THROW(ctx,
            "parse: parse error(s) detected in %s source (strict mode). "
            "Without strict, you get the partial tree with hasError=true "
            "on the affected nodes.",
            lang_name);
    }

    push_node_tree(ctx, root, src, include_text, include_unnamed);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 1;
}

static duk_ret_t es_extractSymbols(duk_context *ctx)
{
    duk_size_t src_sz;
    const char *src = REQUIRE_LSTRING(ctx, 0, &src_sz,
        "extractSymbols: first argument must be the source string");
    const char *lang_name = REQUIRE_STRING(ctx, 1,
        "extractSymbols: second argument must be the language name");

    /* Optional third arg: opts object (or undefined). Currently honored:
     *   strict (bool)  — throw on any parse error (ERROR node anywhere
     *                    in the tree). Default false: caller gets
     *                    partial results plus the hasErrors flag. */
    int strict = 0;
    if (duk_is_object(ctx, 2)) {
        if (duk_get_prop_string(ctx, 2, "strict"))
            strict = duk_to_boolean(ctx, -1);
        duk_pop(ctx);
    }

    const lang_def_t *lang = find_language(lang_name);
    if (!lang)
        RP_THROW(ctx, "extractSymbols: unsupported language '%s'", lang_name);
    if (!lang->fn_node_types[0])
        RP_THROW(ctx,
            "extractSymbols: '%s' is bundled but not exposed for symbol "
            "extraction (no LANGUAGES[] fn_node_types defined). Use "
            "parse() instead, or extract symbols from the raw tree.",
            lang_name);

    TSParser *parser = ts_parser_new();
    if (!parser)
        RP_THROW(ctx, "extractSymbols: ts_parser_new failed");
    if (!ts_parser_set_language(parser, lang->get_language())) {
        ts_parser_delete(parser);
        RP_THROW(ctx,
            "extractSymbols: ts_parser_set_language failed for '%s' "
            "(grammar ABI mismatch with runtime?)", lang_name);
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)src_sz);
    if (!tree) {
        ts_parser_delete(parser);
        RP_THROW(ctx, "extractSymbols: parse failed");
    }

    TSNode root = ts_tree_root_node(tree);
    int has_error = ts_node_has_error(root) ? 1 : 0;

    if (strict && has_error) {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        RP_THROW(ctx,
            "extractSymbols: parse error(s) detected in %s source "
            "(strict mode). Tree-sitter is fault-tolerant — without "
            "strict mode you get the partial results plus hasErrors=true.",
            lang_name);
    }

    /* Build the wrapper object: { symbols: [...], hasErrors: bool }. */
    duk_idx_t obj_idx = duk_push_object(ctx);

    duk_idx_t arr_idx = duk_push_array(ctx);
    duk_uarridx_t out_i = 0;
    walk_collect(ctx, src, lang, root, arr_idx, &out_i);
    duk_put_prop_string(ctx, obj_idx, "symbols");

    duk_push_boolean(ctx, has_error);
    duk_put_prop_string(ctx, obj_idx, "hasErrors");

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 1;
}

/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);

    /* extractSymbols(source, language [, opts]) — three args, opts
     * optional. Returns {symbols, hasErrors}. */
    duk_push_c_function(ctx, es_extractSymbols, 3);
    duk_put_prop_string(ctx, -2, "extractSymbols");

    /* parse(source, language [, opts]) — returns the root node as a
     * nested plain-object tree. See push_node_tree above for the
     * per-node shape. */
    duk_push_c_function(ctx, es_parse, 3);
    duk_put_prop_string(ctx, -2, "parse");

    /* .languages — ALL bundled grammars. parse() works for every
     * name in this list. extractSymbols works for the subset whose
     * LANGUAGES[] entry has a non-empty fn_node_types list; for
     * the rest (currently yaml, toml, markdown, markdown_inline,
     * elixir) it throws a "bundled but not exposed for symbol
     * extraction — use parse() instead" error. */
    duk_idx_t arr_langs = duk_push_array(ctx);
    for (int i = 0; LANGUAGES[i].name; i++) {
        duk_push_string(ctx, LANGUAGES[i].name);
        duk_put_prop_index(ctx, arr_langs, (duk_uarridx_t)i);
    }
    duk_put_prop_string(ctx, -2, "languages");

    return 1;
}
