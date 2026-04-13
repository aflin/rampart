/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT

   To build the test
   cc -g -DTEST -o minify -Ilib/include/ minify.c \
       -I../../src/include \
       tree-sitter-javascript/src/parser.c tree-sitter-javascript/src/scanner.c \
       lib/src/lib.c
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/include/tree_sitter/api.h"
#include "minify.h"

/* ============== rp_string (header-only growable string) ============== */
#ifndef REMALLOC
#define REMALLOC(s, t)                                                         \
    do {                                                                       \
        (s) = realloc((s), (t));                                               \
        if ((char *)(s) == (char *)NULL) {                                     \
            fprintf(stderr, "error: realloc(var, %d) in %s at %d\n",           \
                    (int)(t), __FILE__, __LINE__);                             \
            abort();                                                           \
        }                                                                      \
    } while (0)
#endif

#ifndef CALLOC
#define CALLOC(s, t)                                                           \
    do {                                                                       \
        (s) = calloc(1, (t));                                                  \
        if ((char *)(s) == (char *)NULL) {                                     \
            fprintf(stderr, "error: calloc(var, %d) in %s at %d\n",           \
                    (int)(t), __FILE__, __LINE__);                             \
            abort();                                                           \
        }                                                                      \
    } while (0)
#endif

/* When building standalone test, we need the implementation.
   When linking into rampart, transpiler.c already provides it. */
#ifdef TEST
#define RP_STRING_IMPLEMENTATION
#endif
#include "rp_string.h"
#ifdef TEST
#undef RP_STRING_IMPLEMENTATION
#endif

extern const TSLanguage *tree_sitter_javascript(void);

/* ============== name generator ============== */
/* Generates short names: a,b,...,z,A,...,Z,aa,ab,...,zz,aA,... */

/* Characters allowed as the first char of an identifier */
static const char first_chars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$";
#define N_FIRST ((int)(sizeof(first_chars) - 1))

/* Characters allowed in subsequent positions */
static const char rest_chars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$";
#define N_REST ((int)(sizeof(rest_chars) - 1))

/* Write the n-th generated name into buf (must be >= 16 bytes).
   Returns the length. */
static int gen_name(int n, char *buf)
{
    /* single char: 0..53 */
    if (n < N_FIRST) {
        buf[0] = first_chars[n];
        buf[1] = '\0';
        return 1;
    }
    /* two+ chars */
    n -= N_FIRST;
    int len = 0;
    buf[len++] = first_chars[n % N_FIRST];
    n /= N_FIRST;
    while (n > 0) {
        n--; /* make it 0-based for this digit */
        buf[len++] = rest_chars[n % N_REST];
        n /= N_REST;
    }
    buf[len] = '\0';
    return len;
}

/* ============== JS reserved words ============== */
static const char *js_reserved[] = {
    "break",    "case",     "catch",    "continue", "debugger", "default",
    "delete",   "do",       "else",     "export",   "extends",  "finally",
    "for",      "function", "if",       "import",   "in",       "instanceof",
    "let",      "new",      "return",   "switch",   "this",     "throw",
    "try",      "typeof",   "var",      "void",     "while",    "with",
    "class",    "const",    "enum",     "yield",    "implements","interface",
    "package",  "private",  "protected","public",   "static",   "super",
    "null",     "true",     "false",    "undefined", "NaN",     "Infinity",
    "eval",     "arguments",
    NULL
};

static int is_reserved(const char *name, int len)
{
    for (int i = 0; js_reserved[i]; i++) {
        if ((int)strlen(js_reserved[i]) == len &&
            memcmp(js_reserved[i], name, len) == 0)
            return 1;
    }
    return 0;
}

/* ============== scope / symbol structures ============== */

typedef struct Symbol {
    char *orig_name;    /* original name (owned) */
    int   orig_len;     /* length of original name */
    char  short_name[16]; /* mangled name */
    int   short_len;    /* length of mangled name */
    int   is_global;    /* don't rename globals */
} Symbol;

typedef struct Scope {
    struct Scope *parent;
    Symbol *syms;
    int     nsyms;
    int     syms_cap;
    int     name_counter; /* next name index for gen_name */
    int     is_function;  /* function scope (var hoisting target) */
    int     has_eval;     /* contains eval() — don't rename in this scope */
    uint32_t start_byte;  /* AST node byte range */
    uint32_t end_byte;
} Scope;

static Scope *scope_new(Scope *parent, int is_function,
                        uint32_t start_byte, uint32_t end_byte)
{
    Scope *s;
    CALLOC(s, sizeof(*s));
    s->parent = parent;
    s->is_function = is_function;
    s->name_counter = 0;
    s->start_byte = start_byte;
    s->end_byte = end_byte;
    return s;
}

static void scope_free(Scope *s)
{
    for (int i = 0; i < s->nsyms; i++)
        free(s->syms[i].orig_name);
    free(s->syms);
    free(s);
}

/* Find a symbol in this scope only */
static Symbol *scope_find_local(Scope *s, const char *name, int len)
{
    for (int i = 0; i < s->nsyms; i++) {
        if (s->syms[i].orig_len == len &&
            memcmp(s->syms[i].orig_name, name, len) == 0)
            return &s->syms[i];
    }
    return NULL;
}

/* Find a symbol walking up the scope chain */
static Symbol *scope_find(Scope *s, const char *name, int len)
{
    for (Scope *cur = s; cur; cur = cur->parent) {
        Symbol *sym = scope_find_local(cur, name, len);
        if (sym) return sym;
    }
    return NULL;
}

/* Add a declaration to a scope */
static Symbol *scope_add(Scope *s, const char *name, int len, int is_global)
{
    /* don't add duplicates */
    Symbol *existing = scope_find_local(s, name, len);
    if (existing) return existing;

    if (s->nsyms >= s->syms_cap) {
        s->syms_cap = s->syms_cap ? s->syms_cap * 2 : 16;
        REMALLOC(s->syms, s->syms_cap * sizeof(Symbol));
    }
    Symbol *sym = &s->syms[s->nsyms++];
    CALLOC(sym->orig_name, len + 1);
    memcpy(sym->orig_name, name, len);
    sym->orig_name[len] = '\0';
    sym->orig_len = len;
    sym->short_name[0] = '\0';
    sym->short_len = 0;
    sym->is_global = is_global;
    return sym;
}

/* Find the enclosing function scope (for var hoisting) */
static Scope *find_var_scope(Scope *s)
{
    while (s && !s->is_function)
        s = s->parent;
    return s;
}

/* Check if a name collides with anything visible from this scope */
static int name_collides(Scope *s, const char *name, int len)
{
    if (is_reserved(name, len))
        return 1;
    for (Scope *cur = s; cur; cur = cur->parent) {
        for (int i = 0; i < cur->nsyms; i++) {
            Symbol *sym = &cur->syms[i];
            /* collides with an original name that won't be renamed */
            if (sym->is_global && sym->orig_len == len &&
                memcmp(sym->orig_name, name, len) == 0)
                return 1;
            /* collides with an already-assigned short name */
            if (sym->short_len == len &&
                memcmp(sym->short_name, name, len) == 0)
                return 1;
        }
    }
    return 0;
}

/* Assign short names to all non-global symbols in a scope */
static void scope_assign_names(Scope *s)
{
    /* Inherit counter from parent so sibling scopes don't collide */
    int counter = 0;
    if (s->parent)
        counter = s->parent->name_counter;

    for (int i = 0; i < s->nsyms; i++) {
        Symbol *sym = &s->syms[i];
        if (sym->is_global) continue;
        if (sym->short_len > 0) continue; /* already assigned */

        char buf[16];
        int len;
        do {
            len = gen_name(counter++, buf);
        } while (name_collides(s, buf, len));

        memcpy(sym->short_name, buf, len + 1);
        sym->short_len = len;
    }
    s->name_counter = counter;
}

/* ============== scope tree (keeps all scopes for freeing) ============== */

typedef struct {
    Scope **scopes;
    int     nscopes;
    int     cap;
} ScopeList;

static void scopelist_init(ScopeList *sl)
{
    sl->scopes = NULL;
    sl->nscopes = 0;
    sl->cap = 0;
}

static void scopelist_add(ScopeList *sl, Scope *s)
{
    if (sl->nscopes >= sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 32;
        REMALLOC(sl->scopes, sl->cap * sizeof(Scope *));
    }
    sl->scopes[sl->nscopes++] = s;
}

static void scopelist_free_all(ScopeList *sl)
{
    for (int i = 0; i < sl->nscopes; i++)
        scope_free(sl->scopes[i]);
    free(sl->scopes);
}

/* ============== helper: node type/field checks ============== */

static inline int node_is(TSNode n, const char *type)
{
    return !ts_node_is_null(n) && strcmp(ts_node_type(n), type) == 0;
}

/* Does this node create a function scope? */
static int is_function_node(TSNode n)
{
    if (ts_node_is_null(n)) return 0;
    const char *t = ts_node_type(n);
    return strcmp(t, "function_declaration") == 0 ||
           strcmp(t, "function_expression") == 0 ||
           strcmp(t, "arrow_function") == 0 ||
           strcmp(t, "generator_function_declaration") == 0 ||
           strcmp(t, "generator_function") == 0 ||
           strcmp(t, "method_definition") == 0;
}

/* Is this identifier in a position where it should NOT be renamed?
   (property access, property key in object literal, label, etc.) */
static int is_property_position(TSNode id, TSNode parent)
{
    if (ts_node_is_null(parent)) return 0;
    const char *pt = ts_node_type(parent);

    /* obj.property — the property_identifier is a different node type,
       but just in case: */
    if (strcmp(pt, "member_expression") == 0) {
        const char *field = NULL;
        uint32_t count = ts_node_child_count(parent);
        for (uint32_t i = 0; i < count; i++) {
            if (ts_node_eq(ts_node_child(parent, i), id)) {
                field = ts_node_field_name_for_child(parent, i);
                break;
            }
        }
        if (field && strcmp(field, "property") == 0)
            return 1;
    }

    /* { key: value } in object literal — the key is property_identifier,
       not identifier, but pair_pattern keys are property_identifier too */
    if (strcmp(pt, "pair") == 0 || strcmp(pt, "method_definition") == 0) {
        const char *field = NULL;
        uint32_t count = ts_node_child_count(parent);
        for (uint32_t i = 0; i < count; i++) {
            if (ts_node_eq(ts_node_child(parent, i), id)) {
                field = ts_node_field_name_for_child(parent, i);
                break;
            }
        }
        if (field && strcmp(field, "key") == 0)
            return 1;
    }

    /* label: or break label; or continue label; */
    if (strcmp(pt, "labeled_statement") == 0 || strcmp(pt, "break_statement") == 0 ||
        strcmp(pt, "continue_statement") == 0)
        return 1;

    return 0;
}

/* ============== Pass 1: collect declarations ============== */

/* Forward declare */
static void collect_binding_names(TSNode pattern, const char *src,
                                  Scope *scope, int is_global);
static void scan_declarations(TSNode node, const char *src, Scope *scope,
                              ScopeList *all_scopes);

/* Extract binding names from destructuring patterns */
static void collect_binding_names(TSNode pattern, const char *src,
                                  Scope *scope, int is_global)
{
    const char *t = ts_node_type(pattern);

    if (strcmp(t, "identifier") == 0) {
        uint32_t s = ts_node_start_byte(pattern);
        uint32_t e = ts_node_end_byte(pattern);
        scope_add(scope, src + s, e - s, is_global);
        return;
    }

    if (strcmp(t, "shorthand_property_identifier_pattern") == 0) {
        uint32_t s = ts_node_start_byte(pattern);
        uint32_t e = ts_node_end_byte(pattern);
        scope_add(scope, src + s, e - s, is_global);
        return;
    }

    if (strcmp(t, "object_pattern") == 0) {
        uint32_t count = ts_node_named_child_count(pattern);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(pattern, i);
            const char *ct = ts_node_type(child);
            if (strcmp(ct, "shorthand_property_identifier_pattern") == 0) {
                uint32_t s = ts_node_start_byte(child);
                uint32_t e = ts_node_end_byte(child);
                scope_add(scope, src + s, e - s, is_global);
            } else if (strcmp(ct, "pair_pattern") == 0) {
                /* { key: binding } — the value side is the binding */
                TSNode val = ts_node_child_by_field_name(child, "value", 5);
                if (!ts_node_is_null(val))
                    collect_binding_names(val, src, scope, is_global);
            } else if (strcmp(ct, "rest_pattern") == 0) {
                uint32_t nc = ts_node_named_child_count(child);
                if (nc > 0)
                    collect_binding_names(ts_node_named_child(child, 0), src,
                                          scope, is_global);
            } else if (strcmp(ct, "assignment_pattern") == 0) {
                /* default value: binding = default */
                TSNode left = ts_node_child_by_field_name(child, "left", 4);
                if (!ts_node_is_null(left))
                    collect_binding_names(left, src, scope, is_global);
            } else {
                collect_binding_names(child, src, scope, is_global);
            }
        }
        return;
    }

    if (strcmp(t, "array_pattern") == 0) {
        uint32_t count = ts_node_named_child_count(pattern);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(pattern, i);
            collect_binding_names(child, src, scope, is_global);
        }
        return;
    }

    if (strcmp(t, "rest_pattern") == 0) {
        uint32_t nc = ts_node_named_child_count(pattern);
        if (nc > 0)
            collect_binding_names(ts_node_named_child(pattern, 0), src,
                                  scope, is_global);
        return;
    }

    if (strcmp(t, "assignment_pattern") == 0) {
        TSNode left = ts_node_child_by_field_name(pattern, "left", 4);
        if (!ts_node_is_null(left))
            collect_binding_names(left, src, scope, is_global);
        return;
    }
}

/* Check if a subtree contains eval() or with statement */
static int contains_eval_or_with(TSNode node, const char *src)
{
    if (ts_node_is_null(node)) return 0;
    const char *type = ts_node_type(node);

    /* Don't descend into nested functions — their eval/with is their own */
    if (is_function_node(node)) return 0;

    /* with statement makes all vars dynamically resolvable */
    if (strcmp(type, "with_statement") == 0) return 1;

    if (strcmp(type, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(fn) && node_is(fn, "identifier")) {
            uint32_t s = ts_node_start_byte(fn);
            uint32_t e = ts_node_end_byte(fn);
            if (e - s == 4 && memcmp(src + s, "eval", 4) == 0)
                return 1;
        }
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        if (contains_eval_or_with(ts_node_child(node, i), src))
            return 1;
    }
    return 0;
}

/* Scan a subtree for declarations, building scopes */
static void scan_declarations(TSNode node, const char *src, Scope *scope,
                              ScopeList *all_scopes)
{
    if (ts_node_is_null(node)) return;
    const char *type = ts_node_type(node);

    /* Function nodes: create a new function scope, collect params + body decls */
    if (is_function_node(node)) {
        Scope *fn_scope = scope_new(scope, 1,
                                    ts_node_start_byte(node),
                                    ts_node_end_byte(node));
        scopelist_add(all_scopes, fn_scope);

        /* function name — for declarations, the name belongs to the OUTER scope;
           for expressions, it belongs to the inner scope (self-reference) */
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name) && node_is(name, "identifier")) {
            uint32_t s = ts_node_start_byte(name);
            uint32_t e = ts_node_end_byte(name);
            if (strcmp(type, "function_declaration") == 0 ||
                strcmp(type, "generator_function_declaration") == 0) {
                /* named function declaration: name goes in outer/var scope */
                Scope *target = find_var_scope(scope);
                if (!target) target = scope;
                scope_add(target, src + s, e - s, target->parent == NULL);
            } else {
                /* function expression: name is local to function body */
                scope_add(fn_scope, src + s, e - s, 0);
            }
        }

        /* parameters */
        TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
        if (!ts_node_is_null(params)) {
            uint32_t count = ts_node_named_child_count(params);
            for (uint32_t i = 0; i < count; i++) {
                TSNode p = ts_node_named_child(params, i);
                collect_binding_names(p, src, fn_scope, 0);
            }
        }

        /* scan body */
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body))
            scan_declarations(body, src, fn_scope, all_scopes);

        /* If the function body contains eval() or with, mark all its
           symbols as non-renameable (they may be accessed by original name) */
        if (!ts_node_is_null(body) && contains_eval_or_with(body, src)) {
            fn_scope->has_eval = 1;
            for (int i = 0; i < fn_scope->nsyms; i++)
                fn_scope->syms[i].is_global = 1;
        }

        return;
    }

    /* catch clause: parameter gets its own block scope */
    if (strcmp(type, "catch_clause") == 0) {
        Scope *catch_scope = scope_new(scope, 0,
                                       ts_node_start_byte(node),
                                       ts_node_end_byte(node));
        scopelist_add(all_scopes, catch_scope);

        TSNode param = ts_node_child_by_field_name(node, "parameter", 9);
        if (!ts_node_is_null(param))
            collect_binding_names(param, src, catch_scope, 0);

        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body))
            scan_declarations(body, src, catch_scope, all_scopes);

        return;
    }

    /* for-in / for-of: the loop variable is an identifier(left), not
       a variable_declaration node. Collect it into the var scope. */
    if (strcmp(type, "for_in_statement") == 0 || strcmp(type, "for_of_statement") == 0) {
        /* Check for "var"/"let"/"const" keyword child */
        int has_var = 0, has_lexical = 0;
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            if (strcmp(ct, "var") == 0) has_var = 1;
            if (strcmp(ct, "let") == 0 || strcmp(ct, "const") == 0) has_lexical = 1;
        }

        Scope *loop_scope = scope;
        if (has_lexical) {
            loop_scope = scope_new(scope, 0,
                                   ts_node_start_byte(node),
                                   ts_node_end_byte(node));
            scopelist_add(all_scopes, loop_scope);
        }

        /* The loop variable is the "left" field */
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        if (!ts_node_is_null(left)) {
            if (has_var) {
                Scope *target = find_var_scope(scope);
                if (!target) target = scope;
                collect_binding_names(left, src, target,
                                      target->parent == NULL);
            } else if (has_lexical) {
                collect_binding_names(left, src, loop_scope, 0);
            }
            /* else: bare identifier like `for (x in obj)` — it's a
               reference, not a declaration */
        }

        /* scan children (body, etc.) */
        for (uint32_t i = 0; i < cc; i++)
            scan_declarations(ts_node_child(node, i), src, loop_scope,
                              all_scopes);
        return;
    }

    /* for statement with lexical decl: create block scope */
    if (strcmp(type, "for_statement") == 0) {
        /* Check if initializer is a lexical declaration */
        int has_lexical = 0;
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (node_is(child, "lexical_declaration")) {
                has_lexical = 1;
                break;
            }
        }

        if (has_lexical) {
            Scope *for_scope = scope_new(scope, 0,
                                         ts_node_start_byte(node),
                                         ts_node_end_byte(node));
            scopelist_add(all_scopes, for_scope);

            /* scan all children in the for-scope */
            uint32_t cc = ts_node_child_count(node);
            for (uint32_t i = 0; i < cc; i++)
                scan_declarations(ts_node_child(node, i), src, for_scope,
                                  all_scopes);

            return;
        }
        /* else fall through to normal child scanning */
    }

    /* statement_block: creates a block scope if it contains let/const
       and is NOT a function body (function bodies are handled above) */
    if (strcmp(type, "statement_block") == 0 && !is_function_node(ts_node_parent(node))) {
        /* Check for lexical declarations */
        int has_lexical = 0;
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            if (node_is(ts_node_named_child(node, i), "lexical_declaration")) {
                has_lexical = 1;
                break;
            }
        }

        if (has_lexical) {
            Scope *block_scope = scope_new(scope, 0,
                                           ts_node_start_byte(node),
                                           ts_node_end_byte(node));
            scopelist_add(all_scopes, block_scope);

            uint32_t cc = ts_node_child_count(node);
            for (uint32_t i = 0; i < cc; i++)
                scan_declarations(ts_node_child(node, i), src, block_scope,
                                  all_scopes);

            return;
        }
    }

    /* variable_declaration (var): hoisted to function scope */
    if (strcmp(type, "variable_declaration") == 0) {
        Scope *target = find_var_scope(scope);
        if (!target) target = scope; /* global */
        int is_global = (target->parent == NULL);

        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode decl = ts_node_named_child(node, i);
            if (node_is(decl, "variable_declarator")) {
                TSNode name = ts_node_child_by_field_name(decl, "name", 4);
                if (!ts_node_is_null(name))
                    collect_binding_names(name, src, target, is_global);
                /* scan the value side for nested functions etc */
                TSNode val = ts_node_child_by_field_name(decl, "value", 5);
                if (!ts_node_is_null(val))
                    scan_declarations(val, src, scope, all_scopes);
            }
        }
        return;
    }

    /* lexical_declaration (let/const): stays in current scope */
    if (strcmp(type, "lexical_declaration") == 0) {
        int is_global = (scope->parent == NULL);

        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode decl = ts_node_named_child(node, i);
            if (node_is(decl, "variable_declarator")) {
                TSNode name = ts_node_child_by_field_name(decl, "name", 4);
                if (!ts_node_is_null(name))
                    collect_binding_names(name, src, scope, is_global);
                TSNode val = ts_node_child_by_field_name(decl, "value", 5);
                if (!ts_node_is_null(val))
                    scan_declarations(val, src, scope, all_scopes);
            }
        }
        return;
    }

    /* default: recurse into children */
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++)
        scan_declarations(ts_node_child(node, i), src, scope, all_scopes);
}

/* ============== scope lookup by byte position ============== */

/* Find the innermost scope containing a byte position.
   Scopes store their own byte ranges (set during scan_declarations). */
static Scope *scopelist_find(ScopeList *sl, uint32_t pos)
{
    Scope *best = NULL;
    uint32_t best_size = UINT32_MAX;
    for (int i = 0; i < sl->nscopes; i++) {
        Scope *s = sl->scopes[i];
        if (pos >= s->start_byte && pos < s->end_byte) {
            uint32_t size = s->end_byte - s->start_byte;
            /* <= so that when sizes tie, the later (more nested) scope wins */
            if (size <= best_size) {
                best_size = size;
                best = s;
            }
        }
    }
    return best;
}

/* ============== Pass 2: emit minified output ============== */

/* Tokens that need a space separator when adjacent.
   In JS, identifiers, keywords, and numbers need spaces between them.
   We track the last emitted char class to insert spaces only when needed. */

typedef enum {
    CC_NONE,    /* start / punctuation */
    CC_WORD,    /* identifier, keyword, number */
    CC_OP,      /* operator chars like + - */
} CharClass;

static CharClass char_class_of(char c)
{
    if (isalnum((unsigned char)c) || c == '_' || c == '$')
        return CC_WORD;
    if (c == '+' || c == '-')
        return CC_OP;
    return CC_NONE;
}

/* Check if a char can start an identifier continuation */
static inline int is_id_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '$';
}

/* We need to handle:
   - ASI (automatic semicolon insertion): we add semicolons explicitly
   - regex vs division ambiguity: we don't transform, just preserve source
   - string/regex/template literals: preserve verbatim
   - comments: strip them
*/

typedef struct {
    rp_string *out;
    const char *src;
    ScopeList  *all_scopes;
    Scope      *global_scope;
    CharClass   last_cc;
} EmitCtx;

static void emit_raw(EmitCtx *ctx, const char *text, int len)
{
    if (len <= 0) return;
    CharClass first_cc = char_class_of(text[0]);
    /* Need a space to separate two word tokens or two same-sign operators */
    if ((ctx->last_cc == CC_WORD && first_cc == CC_WORD) ||
        (ctx->last_cc == CC_OP && first_cc == CC_OP)) {
        rp_string_putc(ctx->out, ' ');
    }
    rp_string_putsn(ctx->out, text, len);
    ctx->last_cc = char_class_of(text[len - 1]);
}

static void emit_char(EmitCtx *ctx, char c)
{
    emit_raw(ctx, &c, 1);
}

static void emit_node(EmitCtx *ctx, TSNode node);

/* Emit a leaf token: the raw source text of a terminal node */
static void emit_leaf(EmitCtx *ctx, TSNode node)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    const char *text = ctx->src + start;
    int len = (int)(end - start);

    const char *type = ts_node_type(node);

    /* Skip comments entirely */
    if (strcmp(type, "comment") == 0)
        return;

    /* Identifiers: look up in scope for renaming */
    if (strcmp(type, "identifier") == 0) {
        /* Check if this is in a property position */
        TSNode parent = ts_node_parent(node);
        if (!is_property_position(node, parent)) {
            Scope *s = scopelist_find(ctx->all_scopes, start);
            if (!s) s = ctx->global_scope;
            Symbol *sym = scope_find(s, text, len);
            if (sym && !sym->is_global && sym->short_len > 0) {
                emit_raw(ctx, sym->short_name, sym->short_len);
                return;
            }
        }
        emit_raw(ctx, text, len);
        return;
    }

    /* shorthand_property_identifier / shorthand_property_identifier_pattern:
       In object literals: {x} means {x: x}. If x is renamed, emit {x: newname}.
       In destructuring:   {x} = obj means {x: x} = obj. Same logic. */
    if (strcmp(type, "shorthand_property_identifier_pattern") == 0 ||
        strcmp(type, "shorthand_property_identifier") == 0) {
        Scope *s = scopelist_find(ctx->all_scopes, start);
        if (!s) s = ctx->global_scope;
        Symbol *sym = scope_find(s, text, len);
        if (sym && !sym->is_global && sym->short_len > 0) {
            /* Keep the original as the property key, add : newname */
            emit_raw(ctx, text, len);
            emit_char(ctx, ':');
            emit_raw(ctx, sym->short_name, sym->short_len);
            return;
        }
        emit_raw(ctx, text, len);
        return;
    }

    /* Preserve strings, regexes, template strings verbatim */
    if (strcmp(type, "string") == 0 || strcmp(type, "regex") == 0 ||
        strcmp(type, "template_string") == 0 || strcmp(type, "string_fragment") == 0 ||
        strcmp(type, "escape_sequence") == 0 || strcmp(type, "regex_pattern") == 0 ||
        strcmp(type, "regex_flags") == 0)
    {
        emit_raw(ctx, text, len);
        return;
    }

    /* Everything else: emit the raw text */
    emit_raw(ctx, text, len);
}

/* Check if a node needs a semicolon after it.
   Only statements that end in ';' in the source need this. */
static int needs_semicolon(TSNode node)
{
    const char *t = ts_node_type(node);
    return strcmp(t, "variable_declaration") == 0 ||
           strcmp(t, "lexical_declaration") == 0 ||
           strcmp(t, "expression_statement") == 0 ||
           strcmp(t, "return_statement") == 0 ||
           strcmp(t, "throw_statement") == 0 ||
           strcmp(t, "break_statement") == 0 ||
           strcmp(t, "continue_statement") == 0 ||
           strcmp(t, "do_statement") == 0 ||
           strcmp(t, "import_statement") == 0 ||
           strcmp(t, "export_statement") == 0 ||
           strcmp(t, "debugger_statement") == 0 ||
           strcmp(t, "empty_statement") == 0;
}

/* Emit a full AST node (recursive) */
static void emit_node(EmitCtx *ctx, TSNode node)
{
    if (ts_node_is_null(node)) return;

    const char *type = ts_node_type(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    /* Skip comment nodes */
    if (strcmp(type, "comment") == 0)
        return;

    /* String, regex: emit entire source verbatim
       (don't recurse into child nodes which are fragments) */
    if (strcmp(type, "string") == 0 || strcmp(type, "regex") == 0) {
        emit_raw(ctx, ctx->src + start, (int)(end - start));
        return;
    }

    /* Template strings: must recurse so identifiers inside ${} get renamed.
       But emit the static parts (backticks, string_fragment) verbatim. */
    if (strcmp(type, "template_string") == 0) {
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);
            if (strcmp(ct, "`") == 0 || strcmp(ct, "string_fragment") == 0 ||
                strcmp(ct, "escape_sequence") == 0) {
                /* static parts: emit verbatim */
                uint32_t cs = ts_node_start_byte(child);
                uint32_t ce = ts_node_end_byte(child);
                rp_string_putsn(ctx->out, ctx->src + cs, ce - cs);
                ctx->last_cc = CC_NONE;
            } else {
                /* template_substitution: recurse to rename identifiers */
                emit_node(ctx, child);
            }
        }
        return;
    }

    uint32_t child_count = ts_node_child_count(node);

    /* Leaf node: emit directly */
    if (child_count == 0) {
        emit_leaf(ctx, node);
        return;
    }

    /* For program and statement_block, emit children and ensure
       semicolons are present where required */
    if (strcmp(type, "program") == 0 || strcmp(type, "statement_block") == 0) {
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char *ct = ts_node_type(child);

            if (strcmp(ct, "{") == 0 || strcmp(ct, "}") == 0) {
                emit_leaf(ctx, child);
                continue;
            }
            if (strcmp(ct, "comment") == 0)
                continue;

            emit_node(ctx, child);

            /* If this statement type needs a semicolon and the source
               didn't have one (ASI), insert it now */
            if (needs_semicolon(child)) {
                uint32_t ce = ts_node_end_byte(child);
                if (ce == 0 || ctx->src[ce - 1] != ';') {
                    rp_string_putc(ctx->out, ';');
                    ctx->last_cc = CC_NONE;
                }
            }
        }
        return;
    }

    /* Default: emit all children recursively.
       This handles the whitespace stripping — we skip whitespace between
       tokens by only emitting actual node content. */
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        emit_node(ctx, child);
    }
}

/* ============== main API ============== */

MinifyResult minify(const char *src, size_t src_len)
{
    MinifyResult result = {NULL, 0, NULL};

    if (!src_len) src_len = strlen(src);

    /* Parse */
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_javascript());
    TSTree *tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)src_len);
    TSNode root = ts_tree_root_node(tree);

    if (ts_node_has_error(root)) {
        /* Find the error node for a useful message */
        result.error = strdup("parse error in input");
        /* Still try to minify — tree-sitter does error recovery */
    }

    /* Pass 1: build scopes and collect declarations */
    Scope *global_scope = scope_new(NULL, 1, 0, (uint32_t)src_len);
    ScopeList all_scopes;
    scopelist_init(&all_scopes);
    scopelist_add(&all_scopes, global_scope);

    scan_declarations(root, src, global_scope, &all_scopes);

    /* Assign names top-down (parents first so children see parent names) */
    for (int i = 0; i < all_scopes.nscopes; i++)
        scope_assign_names(all_scopes.scopes[i]);

    /* Pass 2: emit minified output */
    rp_string *out = rp_string_new(src_len); /* usually smaller */
    EmitCtx ctx;
    ctx.out = out;
    ctx.src = src;
    ctx.all_scopes = &all_scopes;
    ctx.global_scope = global_scope;
    ctx.last_cc = CC_NONE;

    emit_node(&ctx, root);

    result.code = rp_string_steal(out);
    result.code_len = strlen(result.code);
    rp_string_free(out);

    /* Cleanup */
    scopelist_free_all(&all_scopes);
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return result;
}

void minify_result_free(MinifyResult *r)
{
    free(r->code);
    free(r->error);
    r->code = NULL;
    r->error = NULL;
}

/* ============== TEST main ============== */
#ifdef TEST

static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "rb");
    long n;
    size_t r;
    char *buf = NULL;

    if (!f) {
        perror("open");
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    n = ftell(f);
    if (n < 0) {
        perror("ftell");
        exit(1);
    }
    fseek(f, 0, SEEK_SET);

    REMALLOC(buf, (size_t)n + 1);
    r = fread(buf, 1, (size_t)n, f);
    if (strcmp(path, "-") != 0)
        fclose(f);

    buf[r] = '\0';
    if (out_len) *out_len = r;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <path-to-js> [-o outfile]\n"
                "       Use '-' to read from stdin.\n",
                argv[0]);
        return 2;
    }

    const char *outpath = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outpath = argv[++i];
    }

    size_t src_len = 0;
    char *src = read_entire_file(argv[1], &src_len);

    MinifyResult res = minify(src, src_len);

    if (res.error)
        fprintf(stderr, "warning: %s\n", res.error);

    if (res.code) {
        if (outpath) {
            FILE *f = fopen(outpath, "wb");
            if (!f) {
                perror("open output");
                free(src);
                minify_result_free(&res);
                return 1;
            }
            fwrite(res.code, 1, res.code_len, f);
            fclose(f);
            fprintf(stderr, "%zu -> %zu bytes (%.1f%% reduction)\n",
                    src_len, res.code_len,
                    (1.0 - (double)res.code_len / src_len) * 100.0);
        } else {
            fwrite(res.code, 1, res.code_len, stdout);
        }
    }

    free(src);
    minify_result_free(&res);
    return 0;
}

#endif /* TEST */
