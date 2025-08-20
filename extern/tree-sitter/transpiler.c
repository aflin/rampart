#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transpiler.h"

#ifndef REMALLOC
#define REMALLOC(s, t)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        (s) = realloc((s), (t));                                                                                       \
        if ((char *)(s) == (char *)NULL)                                                                               \
        {                                                                                                              \
            fprintf(stderr, "error: realloc(var, %d) in %s at %d\n", (int)(t), __FILE__, __LINE__);                    \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif

#ifndef CALLOC
#define CALLOC(s, t)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        (s) = calloc(1, (t));                                                                                          \
        if ((char *)(s) == (char *)NULL)                                                                               \
        {                                                                                                              \
            fprintf(stderr, "error: calloc(var, %d) in %s at %d\n", (int)(t), __FILE__, __LINE__);                     \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif

static int cmp_desc(const void *a, const void *b)
{
    const Edit *ea = (const Edit *)a;
    const Edit *eb = (const Edit *)b;
    if (ea->start < eb->start)
        return 1; // sort by start descending
    if (ea->start > eb->start)
        return -1;
    return 0;
}

void init_edits(EditList *e)
{
    e->items = NULL;
    e->len = 0;
    e->cap = 0;
}

static void push(EditList *e, Edit it)
{
    if (e->len == e->cap)
    {
        size_t ncap = e->cap ? e->cap * 2 : 8;
        REMALLOC(e->items, ncap * sizeof(Edit));
        e->cap = ncap;
    }
    e->items[e->len++] = it;
}

void add_edit(EditList *e, size_t start, size_t end, const char *replacement)
{
    Edit it = {start, end, strdup(replacement), 1};
    push(e, it);
}

void add_edit_take_ownership(EditList *e, size_t start, size_t end, char *replacement)
{
    Edit it = {start, end, replacement, 1};
    push(e, it);
}

// stolen from babel.  Babel, like this prog is MIT licensed - see https://github.com/babel/babel/blob/main/LICENSE
const char *spread_polyfill =
    //"if(!global._TrN_Sp){global._TrN_Sp={};};_TrN_Sp.__spreadO = function (target) {function ownKeys(object, enumerableOnly) {var keys = Object.keys(object);if (Object.getOwnPropertySymbols) {var symbols = Object.getOwnPropertySymbols(object);if (enumerableOnly) symbols = symbols.filter(function (sym) {return Object.getOwnPropertyDescriptor(object, sym).enumerable;});keys.push.apply(keys, symbols);}return keys;}function _defineProperty(obj, key, value) {if (key in obj) {Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true });} else {obj[key] = value;}return obj;}for (var i = 1; i < arguments.length; i++){var source = arguments[i] != null ? arguments[i] : {};if (i % 2){ownKeys(Object(source), true).forEach(function (key) {_defineProperty(target, key, source[key]);});} else if (Object.getOwnPropertyDescriptors)  {Object.defineProperties(target, Object.getOwnPropertyDescriptors(source));} else {ownKeys(Object(source)).forEach(function (key) {Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key));});}}return target;};_TrN_Sp.__spreadA = function(target, arr) {if(arr instanceof Array)return target.concat(arr);function _nonIterableSpread() {throw new TypeError(\"Invalid attempt to spread non-iterable instance. In order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");}function _unsupportedIterableToArray(o, minLen) {if (!o)return;if (typeof o === \"string\") return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor);n = o.constructor.name;if (n === \"Map\" || n === \"Set\")return Array.from(o);if(n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n))return target.concat(_arrayLikeToArray(o, minLen));}function _iterableToArray(iter) {if (typeof Symbol !== \"undefined\" && Symbol.iterator in Object(iter))return target.concat(Array.from(iter));}function _arrayLikeToArray(arr, len) {if (len == null || len > arr.length) len = arr.length;for (var i = 0, arr2 = new Array(len);i < len;i++){arr2[i] = arr[i];}return target.concat(arr2);}function _arrayWithoutHoles(arr){if (Array.isArray(arr)) return target.concat(_arrayLikeToArray(arr));}return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();};Array.prototype._addchain = function(items) {var self=this;items.forEach(function(item) {self.push(item);});return this;};Array.prototype._concat=Array.prototype.concat;Object.prototype._addchain = function (key, value) {if( typeof key == 'object' )Object.assign(this, key);else this[key] = value;return this;};Object.prototype._concat=Object.prototype._addchain;";
    "if(!global._TrN_Sp){global._TrN_Sp={};};_TrN_Sp.__spreadO = function(target) {function ownKeys(object, enumerableOnly){var keys = Object.keys(object);if (Object.getOwnPropertySymbols){var symbols = Object.getOwnPropertySymbols(object);if (enumerableOnly)symbols = symbols.filter(function(sym) {return Object.getOwnPropertyDescriptor(object, sym).enumerable;});keys.push.apply(keys, symbols);}return keys;}function _defineProperty(obj, key, value){if (key in obj){Object.defineProperty(obj, key, {value : value, enumerable : true, configurable : true, writable : true});}else{obj[key] = value;}return obj;}for (var i = 1; i < arguments.length; i++){var source = arguments[i] != null ? arguments[i] : {};if (i % 2){ownKeys(Object(source), true).forEach(function(key) {_defineProperty(target, key, source[key]);});}else if (Object.getOwnPropertyDescriptors){Object.defineProperties(target, Object.getOwnPropertyDescriptors(source));}else{ownKeys(Object(source)).forEach(function(key) {Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key));});}}return target;};_TrN_Sp.__spreadA = function(target, arr) {if (arr instanceof Array)return target.concat(arr);function _nonIterableSpread(){throw new TypeError(\"Invalid attempt to spread non-iterable instance. In order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");}function _unsupportedIterableToArray(o, minLen){if (!o)return;if (typeof o === \"string\")return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor);n = o.constructor.name;if (n === \"Map\" || n === \"Set\")return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n))return target.concat(_arrayLikeToArray(o, minLen));}function _iterableToArray(iter){if (typeof Symbol !== \"undefined\" && Symbol.iterator in Object(iter))return target.concat(Array.from(iter));}function _arrayLikeToArray(arr, len){if (len == null || len > arr.length)len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++){arr2[i] = arr[i];}return target.concat(arr2);}function _arrayWithoutHoles(arr){if (Array.isArray(arr))return target.concat(_arrayLikeToArray(arr));}return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();};_TrN_Sp._arrayConcat = function(items){var self = this;items.forEach(function(item) {self.push(item);});return this;};_TrN_Sp._newArray = function() {Object.defineProperty(Array.prototype, '_addchain', {value: _TrN_Sp._arrayConcat,writable: true,configurable: true,enumerable: false});Object.defineProperty(Array.prototype, '_concat', {value: Array.prototype._addchain,writable: true,configurable: true,enumerable: false});return [];};_TrN_Sp._objectAddchain = function(key, value) {if (typeof key == 'object'){Object.assign(this, key)}else{this[key] = value;}return this;};_TrN_Sp._newObject = function() {Object.defineProperty(Object.prototype, '_addchain', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});Object.defineProperty(Object.prototype, '_concat', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});return {};};";
const char *import_polyfill =
    "if(!global._TrN_Sp){global._TrN_Sp={};};_TrN_Sp._typeof=function(obj) {\"@babel/helpers - typeof\";if (typeof Symbol === \"function\" && typeof Symbol.iterator === \"symbol\") {_TrN_Sp._typeof = function(obj) {return typeof obj;};} else {_TrN_Sp._typeof = function(obj) {return obj && typeof Symbol === \"function\" && obj.constructor === Symbol && obj !== Symbol.prototype ? \"symbol\" : typeof obj;};}return _TrN_Sp._typeof(obj);};_TrN_Sp._getRequireWildcardCache=function() {if (typeof WeakMap !== \"function\") return null;var cache = new WeakMap();_TrN_Sp._getRequireWildcardCache=function(){return cache;};return cache;};_TrN_Sp._interopRequireWildcard=function(obj) {if (obj && obj.__esModule) {return obj;}if (obj === null || _TrN_Sp._typeof(obj) !== \"object\" && typeof obj !== \"function\") {return { \"default\": obj };}var cache = _TrN_Sp._getRequireWildcardCache();if (cache && cache.has(obj)) {return cache.get(obj);}var newObj = {};var hasPropertyDescriptor = Object.defineProperty && Object.getOwnPropertyDescriptor;for (var key in obj) {if (Object.prototype.hasOwnProperty.call(obj, key)) {var desc = hasPropertyDescriptor ? Object.getOwnPropertyDescriptor(obj, key) : null;if (desc && (desc.get || desc.set)) {Object.defineProperty(newObj, key, desc);} else {newObj[key] = obj[key];}}}newObj[\"default\"] = obj;if (cache) {cache.set(obj, newObj);}return newObj;};";

char *apply_edits(const char *src, size_t src_len, EditList *e, uint8_t polysneeded)
{
    size_t out_cap, out_len;

    // Sort by start desc so offsets stay valid while splicing
    qsort(e->items, e->len, sizeof(Edit), cmp_desc);

    // Estimate final size

    /* will never work.  if you insert 50, then clip 10, you still need 50 extra for the insert before the remove,
       but this would give you only 40.
    long delta = 0;
    for (size_t i=0;i<e->len;i++) {
        long removed = (long)(e->items[i].end - e->items[i].start);
        long added   = (long)strlen(e->items[i].text);
        delta += (added - removed);
    }
    size_t out_cap = src_len + (delta > 0 ? (size_t)delta : 0) + 1000;

    */

    out_cap = src_len + 1;
    out_len = src_len;

    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        size_t before = ed->start;
        size_t after = ed->end;
        size_t rep_len = strlen(ed->text);
        long removed = (long)(after - before);
        long diff = (long)rep_len - removed;
        // Find the largest end of buffer we will write to.
        if (diff != 0)
        {
            out_len = (size_t)((long)out_len + diff);
            if (out_len > out_cap)
                out_cap = out_len;
        }
    }

    out_cap++;
    // printf("src_len=%d, outcap=%d\n", (int)src_len, (int)out_cap);
    char *out = NULL, *ret = NULL;
    size_t out_offset = 0;

    // check for needed polyfills
    if (polysneeded)
    {
        size_t spread_sz = 0, import_sz = 0;

        if (polysneeded & SPREAD_PF)
        {
            spread_sz = strlen(spread_polyfill);
            out_cap += spread_sz;
        }

        if (polysneeded & IMPORT_PF)
        {
            import_sz += strlen(import_polyfill);
            out_cap += import_sz;
        }

        REMALLOC(out, out_cap);
        ret = out;

        // check for !#/my/prog\n
        if (*src == '#' && *(src + 1) == '!')
        {
            const char *p = src;
            size_t len = 0;

            while (p && *p != '\n')
                p++;
            if (p == src)
                return NULL;
            p++;
            len = p - src;
            memcpy(out, src, len);
            src = p;
            src_len -= len;
            out += len;
            out_offset = len;
        }

        if (polysneeded & SPREAD_PF)
        {
            memcpy(out, spread_polyfill, spread_sz);
            out += spread_sz;
        }

        if (polysneeded & IMPORT_PF)
        {
            memcpy(out, import_polyfill, import_sz);
            out += import_sz;
        }
    }
    else
    {
        REMALLOC(out, out_cap);
        ret = out;
    }

    out_len = src_len;
    memcpy(out, src, src_len);
    out[out_len] = '\0';

    /*
    first we add some:
    outlen = 100
    replen = 25
    before = 75;
    after  = 80
    removed = 5
    diff   = 20
    move to 100 (before + replen)
       from  80 (after)
       size  20 (outlen - after)
    newsize  120 (outlen + diff)

    Then we take some away
    outlen = 120
    replen = 5
    before = 50;
    after  = 75
    removed =25
    diff  = -20
    move to  55 (before + replen)
       from  75 (after)
       size  45 (outlen - after)
    newsize 100 (outlen + diff)

    end size is 100, but we need 120.  See above.
    */

    // Splice in place using memmove (still safe due to descending order)
    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        size_t before = ed->start - out_offset;
        size_t after = ed->end - out_offset;
        size_t rep_len = strlen(ed->text);
        long removed = (long)(after - before);
        long diff = (long)rep_len - removed;
        size_t edlen = out_len - after;
        // Make room or close gap
        if (diff != 0)
        {
            // printf("moving to %lu(before+rep_len) from %lu (after), size=%lu (out_len-after), end=%lu, outlen=%lu\n",
            // before + rep_len, after, edlen, before + rep_len+edlen, out_len);
            memmove(out + before + rep_len, out + after, edlen);
            out_len = (size_t)((long)out_len + diff);
        }
        memcpy(out + before, ed->text, rep_len);
        out[out_len] = '\0';
    }

    return ret;
}

void free_edits(EditList *e)
{
    for (size_t i = 0; i < e->len; i++)
    {
        if (e->items[i].own_text && e->items[i].text)
            free(e->items[i].text);
    }
    free(e->items);
    e->items = NULL;
    e->len = e->cap = 0;
}

static inline bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Provided by tree-sitter-javascript (parser.c)
extern const TSLanguage *tree_sitter_javascript(void);

/*
// Returns the field name for `node` relative to its parent, or NULL if none.
// The returned pointer is owned by the language and remains valid for the
// lifetime of the language; do not free it.
static const char *ts_node_field_name(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent))
    {
        return NULL; // no parent => no field
    }

    const char *result = NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(parent);

    if (ts_tree_cursor_goto_first_child(&cursor))
    {
        do
        {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            if (ts_node_eq(child, node))
            {
                result = ts_tree_cursor_current_field_name(&cursor); // may be NULL
                break;
            }
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
    return result;
}
// Optional AST outline (use with --printTree)

static void print_outline(const char *src, TSNode node, int depth, FILE *f)
{
    const char *type = ts_node_type(node);
    const char *field_name = ts_node_field_name(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    int bare = !ts_node_is_named(node);

    for (int i = 0; i < depth; i++)
        fputs("  ", f);
    fprintf(f, "%s%s%s%s%s%s [%u,%u]\n", bare ? "\"" : "", type, bare ? (field_name ? "\" " : "\"") : "",
            field_name ? "(" : "", field_name ? field_name : "", field_name ? ")" : "", start, end);

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        print_outline(src, ts_node_child(node, i), depth + 1, f);
    }
}
*/
static void print_outline(const char *src, TSNode root, int depth, FILE *f)
{
    (void)src; // not needed here, keep to match your signature

    TSTreeCursor cur = ts_tree_cursor_new(root);
    int d = depth;

    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cur);

        const char *type       = ts_node_type(node);
        const char *field_name = ts_tree_cursor_current_field_name(&cur); // field in parent
        uint32_t start         = ts_node_start_byte(node);
        uint32_t end           = ts_node_end_byte(node);
        int bare               = !ts_node_is_named(node);

        for (int i = 0; i < d; i++) fputs("  ", f);
        fprintf(f, "%s%s%s%s%s%s [%u,%u]\n",
                bare ? "\"" : "",
                type,
                bare ? (field_name ? "\" " : "\"") : "",
                field_name ? "(" : "",
                field_name ? field_name : "",
                field_name ? ")" : "",
                start, end);

        // Preorder traversal using the cursor:
        if (ts_tree_cursor_goto_first_child(&cur)) {
            d++;
            continue;
        }
        // No children; walk up until we can take a next sibling
        for (;;) {
            if (ts_tree_cursor_goto_next_sibling(&cur)) {
                // same depth
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cur)) {
                // back at the root; we're done
                ts_tree_cursor_delete(&cur);
                return;
            }
            d--;
        }
    }
}

// ===================== Helper functions =====================

static TSNode find_child_type(TSNode node, const char *type, uint32_t *start)
{
    uint32_t n = ts_node_child_count(node);
    TSNode ret = {{0}};
    uint32_t curpos = (start ? *start : 0);

    for (uint32_t i = curpos; i < n; i++)
    {
        TSNode child = ts_node_child(node, i);
        const char *t = ts_node_type(child);
        if (t && strcmp(t, type) == 0)
        {
            ret = child;
            if (start)
                *start = i;
            break;
        }
    }

    return ret;
}

static inline int vappendf(char **bufp, const char *fmt, va_list ap)
{
    if (!bufp || !fmt)
        return -1;

    const char *old = *bufp;
    size_t oldlen = 0;
    if (old)
    {
        /* must be a valid NUL-terminated string */
        oldlen = strlen(old);
    }

    /* Determine required length for the new formatted chunk */
    va_list ap_count;
    va_copy(ap_count, ap);
    int need = vsnprintf(NULL, 0, fmt, ap_count);
    va_end(ap_count);
    if (need < 0)
    {
        return -1; /* formatting error */
    }

    /* Allocate a new buffer safely (do NOT use realloc yet).
       We only free the old buffer after successful formatting. */
    size_t newsize = oldlen + (size_t)need + 1; /* +1 for NUL */
    char *newbuf = NULL;
    REMALLOC(newbuf, newsize);

    /* Preserve old content (if any) */
    if (oldlen)
    {
        memcpy(newbuf, old, oldlen);
    }
    newbuf[oldlen] = '\0';

    /* Format into the tail */
    va_list ap_write;
    va_copy(ap_write, ap);
    int wrote = vsnprintf(newbuf + oldlen, (size_t)need + 1, fmt, ap_write);
    va_end(ap_write);

    if (wrote < 0)
    {
        /* Shouldn't happen in conforming C99 for supported encodings,
           but handle defensively. */
        free(newbuf);
        return -1;
    }

    /* Success: install the new buffer */
    if (old)
        free((void *)old);
    *bufp = newbuf;
    return wrote; /* number of chars appended (no NUL) */
}

/* Varargs front-end *
static inline int appendf(char **bufp, const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = vappendf(bufp, fmt, ap);
    va_end(ap);
    return r;
}
*/
/* Convenience API: returns new pointer (or NULL on error). Original 'buf' remains owned by caller. */
static inline char *appendf(char *buf, size_t *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *tmp = buf;
    int r = vappendf(&tmp, fmt, ap);
    va_end(ap);
    if (r < 0)
    {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = r;
    return tmp;
}

// ============== range bookkeeping for one-pass ==============
typedef struct
{
    size_t s, e;
} Range;
typedef struct
{
    Range *a;
    size_t len, cap;
} RangeList;

static void rl_init(RangeList *rl)
{
    rl->a = NULL;
    rl->len = 0;
    rl->cap = 0;
}
static int rl_overlaps(const RangeList *rl, size_t s, size_t e)
{
    for (size_t i = 0; i < rl->len; i++)
    {
        size_t S = rl->a[i].s, E = rl->a[i].e;
        if (!(e <= S || E <= s))
            return 1;
    }
    return 0;
}
static void rl_add(RangeList *rl, size_t s, size_t e)
{
    if (rl->len == rl->cap)
    {
        size_t nc = rl->cap ? rl->cap * 2 : 8;

        REMALLOC(rl->a, nc * sizeof(Range));

        rl->cap = nc;
    }
    rl->a[rl->len++] = (Range){s, e};
}


// === Added: default parameter lowering helpers & passes ===

static int params_has_assignment_pattern(TSNode params) {
    if (ts_node_is_null(params)) return 0;
    uint32_t c = ts_node_named_child_count(params);
    for (uint32_t i = 0; i < c; i++) {
        TSNode p = ts_node_named_child(params, i);
        if (strcmp(ts_node_type(p), "assignment_pattern") == 0) return 1;
    }
    return 0;
}

// Build injected "var ..." initializers from params in order using arguments[i].
// Supports identifiers and assignment_pattern with identifier on the left.
static char *build_param_default_inits(const char *src, TSNode params) {
    uint32_t c = ts_node_named_child_count(params);
    size_t cap = 256, len = 0;
    char *buf = NULL; REMALLOC(buf, cap); if (buf) buf[0] = '\0';
    #define APPEND_FMT(...) do { \
        char tmp[1024]; \
        int n = snprintf(tmp, sizeof(tmp), __VA_ARGS__); \
        if (n > 0) { \
            if (len + (size_t)n + 1 > cap) { cap = (len + n + 1) * 2; REMALLOC(buf, cap); } \
            memcpy(buf + len, tmp, (size_t)n); \
            len += (size_t)n; buf[len] = 0; \
        } \
    } while (0)

    for (uint32_t i = 0, pi = 0; i < c; i++, pi++) {
        TSNode p = ts_node_named_child(params, i);
        const char *pt = ts_node_type(p);
        if (strcmp(pt, "identifier") == 0) {
            size_t ns = ts_node_start_byte(p), ne = ts_node_end_byte(p);
            APPEND_FMT("var %.*s = arguments.length > %u ? arguments[%u] : undefined;", (int)(ne-ns), src+ns, pi, pi);
        } else if (strcmp(pt, "assignment_pattern") == 0) {
            TSNode left  = ts_node_child_by_field_name(p, "left", 4);
            TSNode right = ts_node_child_by_field_name(p, "right", 5);
            if (!ts_node_is_null(left) && !ts_node_is_null(right) && strcmp(ts_node_type(left), "identifier") == 0) {
                size_t ls = ts_node_start_byte(left),  le = ts_node_end_byte(left);
                size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                APPEND_FMT("var %.*s = arguments.length > %u && arguments[%u] !== undefined ? arguments[%u] : %.*s;",
                           (int)(le-ls), src+ls, pi, pi, pi, (int)(re-rs), src+rs);
            } else {
                // unsupported here
                free(buf); return NULL;
            }
        } else {
            free(buf); return NULL;
        }
    }
    return buf;
}

// Lower defaults for function-like nodes (decls, expressions, generators, methods).
// Only fires when params contain at least one assignment_pattern.
static int rewrite_function_like_default_params(EditList *edits, const char *src, TSNode node, RangeList *claimed)
{
    const char *nt = ts_node_type(node);
    if (strcmp(nt, "function_declaration") != 0 &&
        strcmp(nt, "function")               != 0 &&
        strcmp(nt, "function_expression")     != 0 &&
        strcmp(nt, "generator_function_declaration") != 0 &&
        strcmp(nt, "generator_function")           != 0 &&
        strcmp(nt, "generator_function_expression") != 0 &&
        strcmp(nt, "method_definition")            != 0)
        return 0;

    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    TSNode body   = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body)) return 0;
    if (!params_has_assignment_pattern(params)) return 0;

    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body),   be = ts_node_end_byte(body);
    if (rl_overlaps(claimed, ps, pe) || rl_overlaps(claimed, bs, be)) return 0;

    char *decls = build_param_default_inits(src, params);
    if (!decls) return 0;

    // Insert after the opening '{'
    size_t insert_at = bs + 1;
    add_edit_take_ownership(edits, insert_at, insert_at, decls);
    rl_add(claimed, bs, be);

    // Replace params list with "()"
    add_edit(edits, ps, pe, "()");
    rl_add(claimed, ps, pe);

    return 1;
}

// Single-pass: convert `var f = function (…) {` → `function f() {` AND inject default initializers.

// Preserve `var f = function (…) { … }` but lower defaults:
//   var f = function() { var a = arguments[0]…; … }
static int rewrite_var_function_expression_defaults(EditList *edits, const char *src, TSNode node, RangeList *claimed)
{
    if (strcmp(ts_node_type(node), "variable_declaration") != 0) return 0;
    if (ts_node_named_child_count(node) != 1) return 0;

    TSNode decl = ts_node_named_child(node, 0);
    if (strcmp(ts_node_type(decl), "variable_declarator") != 0) return 0;

    TSNode val  = ts_node_child_by_field_name(decl, "value", 5);
    if (ts_node_is_null(val)) return 0;

    const char *vt = ts_node_type(val);
    if (strcmp(vt, "function") != 0 && strcmp(vt, "generator_function") != 0 &&
        strcmp(vt, "function_expression") != 0 && strcmp(vt, "generator_function_expression") != 0) return 0;

    TSNode params = ts_node_child_by_field_name(val, "parameters", 10);
    TSNode body   = ts_node_child_by_field_name(val, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body)) return 0;
    if (!params_has_assignment_pattern(params)) return 0;

    // Build initializers
    char *decls = build_param_default_inits(src, params);
    if (!decls) return 0;

    // Replace params with "()"
    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    if (rl_overlaps(claimed, ps, pe)) { free(decls); return 0; }
    add_edit(edits, ps, pe, "()");
    rl_add(claimed, ps, pe);

    // Insert declarations at start of body
    size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);
    if (rl_overlaps(claimed, bs, be)) { free(decls); return 0; }
    add_edit_take_ownership(edits, bs + 1, bs + 1, decls);
    rl_add(claimed, bs, be);

    return 1;
}


// === End added helpers/passes ===
// ============== generic helpers ==============
static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int slice_starts_with_paren(const char *s, size_t a, size_t b)
{
    while (a < b && is_space_char(s[a]))
        a++;
    return (a < b && s[a] == '(');
}

// Quote a JS string literal using double quotes (basic escapes).
static char *js_quote_literal(const char *src, size_t start, size_t end, int *nnl)
{
    size_t cap = (end - start) * 2 + 3;
    char *out = NULL;

    REMALLOC(out, cap);

    size_t j = 0;
    out[j++] = '"';
    *nnl = 0;

    for (size_t i = start; i < end; i++)
    {
        unsigned char c = (unsigned char)src[i];
        if (j + 6 >= cap)
        {
            cap *= 2;
            REMALLOC(out, cap);
        }
        switch (c)
        {
        case '\\':
            //out[j++] = '\\';
            out[j++] = '\\';
            break;
        case '"':
            out[j++] = '\\';
            out[j++] = '"';
            break;
        case '\n':
            out[j++] = '\\';
            out[j++] = 'n';
            (*nnl)++;
            break;
        case '\r':
            out[j++] = '\\';
            out[j++] = 'r';
            break;
        case '\t':
            out[j++] = '\\';
            out[j++] = 't';
            break;
        case '\b':
            out[j++] = '\\';
            out[j++] = 'b';
            break;
        case '\f':
            out[j++] = '\\';
            out[j++] = 'f';
            break;
        default:
            out[j++] = (char)c;
        }
    }
    out[j++] = '"';
    out[j++] = '\0';

    return out;
}

// ============== template slicing helpers ==============
typedef struct
{
    int is_expr;
    size_t start, end;
} Piece;

static void collect_template_by_offsets(TSNode tpl, Piece **lits, size_t *nl, Piece **exprs, size_t *ne)
{
    *lits = NULL;
    *exprs = NULL;
    *nl = 0;
    *ne = 0;
    size_t capL = 0, capE = 0;
    size_t tpl_start = ts_node_start_byte(tpl), tpl_end = ts_node_end_byte(tpl);
    uint32_t c = ts_node_child_count(tpl);
    size_t open_tick = tpl_start, close_tick = tpl_end;

    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(tpl, i);
        if (strcmp(ts_node_type(kid), "`") == 0)
        {
            if (open_tick == tpl_start)
                open_tick = ts_node_end_byte(kid);
            close_tick = ts_node_start_byte(kid);
        }
    }
    if (open_tick == tpl_start)
        open_tick = tpl_start + 1;
    if (close_tick == tpl_end)
        close_tick = (tpl_end > tpl_start) ? tpl_end - 1 : tpl_end;

    size_t cursor = open_tick;
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(tpl, i);
        if (strcmp(ts_node_type(kid), "template_substitution") == 0)
        {
            size_t sub_s = ts_node_start_byte(kid);

            if (sub_s > cursor)
            { // literal before ${...}
                if (*nl == capL)
                {
                    capL = capL ? capL * 2 : 8;
                    REMALLOC(*lits, capL * sizeof(Piece));
                }
                (*lits)[(*nl)++] = (Piece){0, cursor, sub_s};
            }
            uint32_t nexp = ts_node_child_count(kid);
            TSNode expr = ts_node_named_child(kid, 0);
            TSNode lexpr = expr;

            // get all text in expression, even if one is ERROR
            // i.e. ${%s:myvar} -> rampart.utils.printf (see below)
            for (uint32_t i = 1; i < nexp; i++)
            {
                TSNode t = ts_node_named_child(kid, i);
                if (!ts_node_is_null(t))
                    lexpr = t;
            }

            if (!ts_node_is_null(expr))
            {

                size_t es = ts_node_start_byte(expr), ee = ts_node_end_byte(lexpr);
                if (*ne == capE)
                {
                    capE = capE ? capE * 2 : 8;
                    REMALLOC(*exprs, capE * sizeof(Piece));
                }
                (*exprs)[(*ne)++] = (Piece){1, es, ee};
            }

            cursor = ts_node_end_byte(kid);
        }
    }
    if (close_tick > cursor)
    { // trailing literal
        if (*nl == capL)
        {
            capL = capL ? capL * 2 : 8;
            REMALLOC(*lits, capL * sizeof(Piece));
        }
        (*lits)[(*nl)++] = (Piece){0, cursor, close_tick};
    }
    if (*nl == 0)
    { // ensure at least 1 slot
        if (*nl == capL)
        {
            capL = capL ? capL * 2 : 8;
            REMALLOC(*lits, capL * sizeof(Piece));
        }
        (*lits)[(*nl)++] = (Piece){0, open_tick, open_tick};
    }
}

// nearest previous named sibling
static TSNode prev_named_sibling(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent))
        return (TSNode){0};
    uint32_t n = ts_node_child_count(parent);
    TSNode prev = (TSNode){0};
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode kid = ts_node_child(parent, i);
        if (ts_node_eq(kid, node))
            return prev;
        if (ts_node_is_named(kid))
            prev = kid;
    }
    return (TSNode){0};
}

// ============== arrow destructuring helpers ==============
typedef struct
{
    char *name;
    char *repl;
} Binding;
typedef struct
{
    Binding *a;
    size_t len, cap;
} Bindings;

static void binds_init(Bindings *b)
{
    b->a = NULL;
    b->len = 0;
    b->cap = 0;
}
static void binds_add(Bindings *b, const char *name, size_t nlen, const char *repl)
{
    if (b->len == b->cap)
    {
        size_t nc = b->cap ? b->cap * 2 : 8;
        REMALLOC(b->a, nc * sizeof(Binding));
        b->cap = nc;
    }

    b->a[b->len].name = NULL;
    REMALLOC(b->a[b->len].name, nlen + 1);
    memcpy(b->a[b->len].name, name, nlen);
    b->a[b->len].name[nlen] = '\0';
    b->a[b->len].repl = strdup(repl);
    b->len++;
}
static void binds_free(Bindings *b)
{
    for (size_t i = 0; i < b->len; i++)
    {
        free(b->a[i].name);
        free(b->a[i].repl);
    }
    free(b->a);
    b->a = NULL;
    b->len = b->cap = 0;
}

static int collect_flat_destructure_bindings(TSNode pattern, const char *src, const char *base, Bindings *out)
{
    const char *pt = ts_node_type(pattern);
    if (strcmp(pt, "array_pattern") == 0)
    {
        uint32_t c = ts_node_child_count(pattern);
        int idx = 0;
        int last_was_comma_or_open = 1; // at start, as if after '['
        for (uint32_t i = 0; i < c; i++)
        {
            TSNode k = ts_node_child(pattern, i);
            if (!ts_node_is_named(k))
            {
                const char *ktok = ts_node_type(k);
                if (strcmp(ktok, ",") == 0)
                {
                    if (last_was_comma_or_open)
                        idx++; // hole/elision
                    last_was_comma_or_open = 1;
                }
                else if (strcmp(ktok, "[") == 0)
                {
                    last_was_comma_or_open = 1;
                }
                else
                {
                    // ']' or other punct
                }
                continue;
            }
            const char *kt = ts_node_type(k);
            if (strcmp(kt, "identifier") == 0)
            {
                size_t ns = ts_node_start_byte(k), ne = ts_node_end_byte(k);
                char buf[64];
                snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                binds_add(out, src + ns, ne - ns, buf);
                idx++;
                last_was_comma_or_open = 0;
            }
            else if (strcmp(kt, "assignment_pattern") == 0)
            {
                TSNode left = ts_node_child_by_field_name(k, "left", 4);
                if (!ts_node_is_null(left) && strcmp(ts_node_type(left), "identifier") == 0)
                {
                    size_t ns = ts_node_start_byte(left), ne = ts_node_end_byte(left);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                    binds_add(out, src + ns, ne - ns, buf);
                }
                idx++;
                last_was_comma_or_open = 0;
            }
            else
            {
                return 0; // nested not supported
            }
        }
        return 1;
    }
    else if (strcmp(pt, "object_pattern") == 0)
    {
        uint32_t c = ts_node_child_count(pattern);
        for (uint32_t i = 0; i < c; i++)
        {
            TSNode k = ts_node_child(pattern, i);
            if (!ts_node_is_named(k))
                continue;
            const char *kt = ts_node_type(k);
            if (strcmp(kt, "pair_pattern") == 0 || strcmp(kt, "pair") == 0)
            {
                TSNode key = ts_node_child_by_field_name(k, "key", 3);
                TSNode val = ts_node_child_by_field_name(k, "value", 5);
                if (ts_node_is_null(key) || ts_node_is_null(val) || strcmp(ts_node_type(val), "identifier") != 0)
                    return 0;
                if (strcmp(ts_node_type(key), "property_identifier") == 0 ||
                    strcmp(ts_node_type(key), "identifier") == 0)
                {
                    size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                    size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                    size_t klen = ke - ks;
                    char *repl = NULL;
                    REMALLOC(repl, strlen(base) + 1 + klen + 1);
                    sprintf(repl, "%s.%.*s", base, (int)klen, src + ks);
                    binds_add(out, src + vs, ve - vs, repl);
                    free(repl);
                }
                else
                    return 0;
            }
            else if (strcmp(kt, "shorthand_property_identifier_pattern") == 0 ||
                     strcmp(kt, "shorthand_property_identifier") == 0)
            {
                size_t ns = ts_node_start_byte(k), ne = ts_node_end_byte(k);
                size_t nlen = ne - ns;
                char *repl = NULL;
                REMALLOC(repl, strlen(base) + 1 + nlen + 1);
                sprintf(repl, "%s.%.*s", base, (int)nlen, src + ns);
                binds_add(out, src + ns, nlen, repl);
                free(repl);
            }
            else
                return 0;
        }
        return 1;
    }
    return 0;
}

static char *rewrite_concise_body_with_bindings(const char *src, TSNode expr, const Bindings *b)
{
    size_t es = ts_node_start_byte(expr), ee = ts_node_end_byte(expr);
    EditList tmp;
    init_edits(&tmp);
    TSTreeCursor c = ts_tree_cursor_new(expr);
    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&c);
        if (strcmp(ts_node_type(n), "identifier") == 0)
        {
            size_t ns = ts_node_start_byte(n), ne = ts_node_end_byte(n), nlen = ne - ns;
            for (size_t i = 0; i < b->len; i++)
            {
                size_t blen = strlen(b->a[i].name);
                if (nlen == blen && strncmp(src + ns, b->a[i].name, nlen) == 0)
                {
                    add_edit(&tmp, ns - es, ne - es, b->a[i].repl);
                    break;
                }
            }
        }
        if (ts_tree_cursor_goto_first_child(&c))
            continue;
        while (!ts_tree_cursor_goto_next_sibling(&c))
        {
            if (!ts_tree_cursor_goto_parent(&c))
            {
                ts_tree_cursor_delete(&c);
                goto APPLY;
            }
        }
    }
APPLY:;
    char *slice = NULL;

    REMALLOC(slice, ee - es + 1);
    memcpy(slice, src + es, ee - es);
    slice[ee - es] = '\0';
    char *out = apply_edits(slice, ee - es, &tmp, 0);
    free(slice);
    free_edits(&tmp);
    return out;
}

// ============== handler prototypes ==============
static int rewrite_template_node(EditList *edits, const char *src, TSNode tpl_node, RangeList *claimed);
static int rewrite_arrow_function_node(EditList *edits, const char *src, TSNode arrow_node, RangeList *claimed);
static void rewrite_lexical_declaration(EditList *edits, const char *src, TSNode lexical_decl);

// Detect whether a subtree contains a `this` that belongs to the current lexical scope of an arrow function.
// We DO NOT descend into non-arrow function bodies or class bodies (they introduce a new `this`).
static int contains_lexical_this(TSNode node)
{
    const char *t = ts_node_type(node);
    // If this node itself is the `this` token (anonymous/unnamed)
    if (strcmp(t, "this") == 0)
    {
        return 1;
    }
    // Do not descend into nodes that create their own `this` binding (non-arrow functions, classes)
    if (ts_node_is_named(node))
    {
        // function_declaration, function_expression, generator_function, method_definition, etc.
        // We avoid any node type that contains "function" but is NOT "arrow_function".
        if (strstr(t, "function") && strcmp(t, "arrow_function") != 0)
            return 0;
        if (strstr(t, "method") != NULL)
            return 0;
        if (strstr(t, "class") != NULL)
            return 0;
    }
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode c = ts_node_child(node, i);
        if (contains_lexical_this(c))
            return 1;
    }
    return 0;
}
// ============== handlers ==============

// Rewrite JS rest parameters:  function f(x,y,...a){ ... }
// -> function f(x,y){ var a = Object.values(arguments).slice(2); ... }
static int rewrite_function_rest(EditList *edits, const char *src, TSNode func_node, RangeList *claimed)
{
    const char *nt = ts_node_type(func_node);
    // Cover decl/expr and generator forms
    if (strcmp(nt, "function_declaration") != 0 && strcmp(nt, "function") != 0 &&
        strcmp(nt, "generator_function_declaration") != 0 && strcmp(nt, "generator_function") != 0)
    {
        return 0;
    }

    TSNode params = ts_node_child_by_field_name(func_node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(func_node, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;

    // Find a rest_parameter
    uint32_t nparams = ts_node_named_child_count(params);
    TSNode rest = {0};
    uint32_t rest_index = 0;
    bool found = false;

    for (uint32_t i = 0; i < nparams; i++)
    {
        TSNode ch = ts_node_named_child(params, i);
        if (strcmp(ts_node_type(ch), "rest_pattern") == 0)
        {
            rest = ch;
            rest_index = i;
            found = true;
            break;
        }
    }
    if (!found)
        return 0;

    // Count non-rest params before the rest (slice start)
    uint32_t before_count = rest_index;

    // Name of the rest identifier
    nparams = ts_node_named_child_count(rest);
    TSNode rest_pat = {{0}};

    for (int i = 0; i < nparams; i++)
    {
        TSNode ch = ts_node_named_child(rest, i);
        if (strcmp(ts_node_type(ch), "identifier") == 0)
        {
            rest_pat = ch;
            rest_index = i;
            found = true;
            break;
        }
    }

    if (ts_node_is_null(rest_pat))
        return 0;
    size_t name_s = ts_node_start_byte(rest_pat);
    size_t name_e = ts_node_end_byte(rest_pat);

    // Remove the rest parameter token, including a preceding comma if present
    size_t params_s = ts_node_start_byte(params);
    size_t params_e = ts_node_end_byte(params);
    size_t rest_s = ts_node_start_byte(rest);
    size_t rest_e = ts_node_end_byte(rest);

    // Walk back over whitespace to see if there is a preceding comma
    size_t del_s = rest_s;
    size_t i = rest_s;
    while (i > params_s && is_ws(src[i - 1]))
        i--;
    if (i > params_s && src[i - 1] == ',')
    {
        del_s = i - 1;
        while (del_s > params_s && is_ws(src[del_s - 1]))
            del_s--;
    }
    else
    {
        // Otherwise try to eat a trailing comma if present (rest first in list)
        size_t j = rest_e;
        while (j < params_e && is_ws(src[j]))
            j++;
        if (j < params_e && src[j] == ',')
        {
            // remove trailing comma and the rest param
            rest_e = j + 1;
        }
    }
    add_edit(edits, del_s, rest_e, "");

    // Insert the shim at the start of the body block (after '{' and any whitespace)
    size_t body_s = ts_node_start_byte(body);
    size_t body_e = ts_node_end_byte(body);
    if (body_e <= body_s)
        return 1;                  // odd, but we've already removed the rest
    size_t insert_at = body_s + 1; // skip '{'
    while (insert_at < body_e && is_ws(src[insert_at]))
        insert_at++;

    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)before_count);

    const char *p1 = "var ";
    const char *p2 = " = Object.values(arguments).slice(";
    const char *p3 = "); ";
    size_t name_len = name_e - name_s;
    size_t repl_len = strlen(p1) + name_len + strlen(p2) + strlen(numbuf) + strlen(p3);
    char *repl = NULL;
    size_t k = 0;

    REMALLOC(repl, repl_len + 1);

    memcpy(repl + k, p1, strlen(p1));
    k += strlen(p1);
    memcpy(repl + k, src + name_s, name_len);
    k += name_len;
    memcpy(repl + k, p2, strlen(p2));
    k += strlen(p2);
    memcpy(repl + k, numbuf, strlen(numbuf));
    k += strlen(numbuf);
    memcpy(repl + k, p3, strlen(p3));
    k += strlen(p3);
    repl[k] = '\0';

    add_edit_take_ownership(edits, insert_at, insert_at, repl);
    rl_add(claimed, params_s, params_e);
    rl_add(claimed, body_s, body_e);
    return 1;
}

static char *make_raw_rep(const char *orig, size_t l)
{
    const char *s = orig;
    const char *e = s + l;
    size_t newsz = 3; // beginning " and ending "'\0'

    while (s < e)
    {
        switch (*s)
        {
        case '"':
        case '\\':
            newsz++;
            break;
        case '\n': // literal "\n"
            newsz += 2;
            break;
        }
        newsz++;
        s++;
    }

    char *ret, *out = NULL;

    REMALLOC(out, newsz);
    ret = out;

    *(out++) = '"';

    s = orig;
    while (s < e)
    {
        switch (*s)
        {
        case '"':
        case '\\':
            *(out++) = '\\';
            *(out++) = *(s++);
            break;
        case '\n': // +"\n"
//            *(out++) = '"';
//            *(out++) = '\n';
//            *(out++) = '+';
//            *(out++) = '"';
            *(out++) = '\\';
            *(out++) = 'n';
            s++;
            break;
        default:
            *(out++) = *(s++);
        }
    }
    *(out++) = '"';
    *(out++) = '\0';
    // printf("strlen=%d, malloc=%d + 1, l=%d\n", (int)strlen(ret), (int) newsz-1, (int)l);
    return ret;
}

static int rewrite_raw_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed)
{
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);
    if (rl_overlaps(claimed, ns, ne))
        return 0;

    TSNode raw = find_child_type(snode, "string_fragment_raw", NULL);

    if (ts_node_is_null(raw))
        return 0;

    size_t start = ts_node_start_byte(raw), end = ts_node_end_byte(raw);

    char *out = make_raw_rep(src + start, end - start);

    start = ts_node_start_byte(snode);
    end = ts_node_end_byte(snode);

    add_edit_take_ownership(edits, start, end, out);
    return 1;
}

// Import
static int do_named_imports(EditList *edits, const char *src, TSNode snode, TSNode named_imports, TSNode string_frag,
                            size_t start, size_t end)
{
    static uint32_t tmpn = 0;
    uint32_t pos = 0;
    char *out = NULL;
    char buf[32];
    TSNode n = find_child_type(named_imports, "import_specifier", &pos);

    if (ts_node_is_null(n))
        return 0;

    size_t mod_name_end = ts_node_end_byte(string_frag), mod_name_start = ts_node_start_byte(string_frag);

    sprintf(buf, "__tmpModImp%u", tmpn);

    out = appendf(NULL, NULL, "var %s=require(\"%.*s\");", buf, (mod_name_end - mod_name_start), src + mod_name_start);

    while (!ts_node_is_null(n))
    {
        size_t s = ts_node_start_byte(n);
        size_t e = ts_node_end_byte(n);
        out = appendf(out, NULL, "var %.*s=%s.%.*s;", e - s, src + s, buf, e - s, src + s);
        pos++;
        n = find_child_type(named_imports, "import_specifier", &pos);
    }

    add_edit_take_ownership(edits, start, end, out);

    tmpn++;
    return 1;
}

static int do_namespace_import(EditList *edits, const char *src, TSNode snode, TSNode namespace_import,
                               TSNode string_frag, size_t start, size_t end, uint8_t *polysneeded)
{
    char *out = NULL;
    TSNode id = find_child_type(namespace_import, "identifier", NULL);

    if (ts_node_is_null(id))
        return 0;

    size_t mod_name_end = ts_node_end_byte(string_frag), mod_name_start = ts_node_start_byte(string_frag),
           id_end = ts_node_end_byte(id), id_start = ts_node_start_byte(id);

    // var math = _interopRequireWildcard(require("math"));
    out = appendf(NULL, NULL, "var %.*s=_TrN_Sp._interopRequireWildcard(require(\"%.*s\"));", (id_end - id_start),
                  src + id_start, (mod_name_end - mod_name_start), src + mod_name_start);

    add_edit_take_ownership(edits, start, end, out);
    *polysneeded |= IMPORT_PF;
    return 1;
}

static int rewrite_import_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed, uint8_t *polysneeded)
{
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);
    if (rl_overlaps(claimed, ns, ne))
        return 0;

    TSNode string = find_child_type(snode, "string", NULL);
    if (ts_node_is_null(string))
        return 0;

    TSNode string_frag = find_child_type(string, "string_fragment", NULL);
    if (ts_node_is_null(string_frag))
        string_frag = find_child_type(string, "string_fragment_raw", NULL);
    if (ts_node_is_null(string_frag))
        return 0;
    // look for template string here:

    TSNode child = find_child_type(snode, "import_clause", NULL);
    if (!ts_node_is_null(child))
    {
        TSNode named = find_child_type(child, "named_imports", NULL);
        if (!ts_node_is_null(named))
            return do_named_imports(edits, src, snode, named, string_frag, ns, ne);
        named = find_child_type(child, "namespace_import", NULL);
        if (!ts_node_is_null(named))
            return do_namespace_import(edits, src, snode, named, string_frag, ns, ne, polysneeded);
    }

    size_t sstart = ts_node_start_byte(string_frag), send = ts_node_end_byte(string_frag), slen = send - sstart;

    // require(""); == 12
    char *out = NULL;
    REMALLOC(out, 13 + slen);
    snprintf(out, slen + 13, "require(\"%.*s\");", (int)slen, src + sstart);

    add_edit_take_ownership(edits, ns, ne, out);
    return 1;
}

// Templates (tagged + untagged)
static int rewrite_template_node(EditList *edits, const char *src, TSNode tpl_node, RangeList *claimed)
{
    size_t ns = ts_node_start_byte(tpl_node), ne = ts_node_end_byte(tpl_node);
    if (rl_overlaps(claimed, ns, ne))
        return 0;

    TSNode tag = prev_named_sibling(tpl_node);
    int is_tagged = (!ts_node_is_null(tag) && ts_node_end_byte(tag) == ns);

    Piece *lits = NULL, *exprs = NULL;
    size_t nl = 0, neP = 0;
    collect_template_by_offsets(tpl_node, &lits, &nl, &exprs, &neP);

    if (is_tagged)
    {
        size_t ts = ts_node_start_byte(tag), te = ts_node_end_byte(tag);
        size_t cap = 128 + (te - ts) + 32 * nl;
        size_t j = 0;
        char *out = NULL;

        for (size_t i = 0; i < nl; i++)
            cap += 2 * (lits[i].end - lits[i].start) + 4;
        for (size_t i = 0; i < neP; i++)
            cap += (exprs[i].end - exprs[i].start) + 8;

        REMALLOC(out, cap);

#define adds(S)                                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const char *_s = (S);                                                                                          \
        size_t _L = strlen(_s);                                                                                        \
        memcpy(out + j, _s, _L);                                                                                       \
        j += _L;                                                                                                       \
    } while (0)

        memcpy(out + j, src + ts, te - ts);
        j += (te - ts);
        adds("(");
        adds("[");
        for (size_t i = 0; i < nl; i++)
        {
            int nnl = 0;
            if (i)
                adds(",");
            char *q = js_quote_literal(src, lits[i].start, lits[i].end, &nnl);
            adds(q);
            for (int k = 0; k < nnl; k++)
                adds("\n");
            free(q);
        }
        adds("]");
        for (size_t i = 0; i < neP; i++)
        {
            adds(",(");
            size_t L = exprs[i].end - exprs[i].start;
            memcpy(out + j, src + exprs[i].start, L);
            j += L;
            adds(")");
        }
        adds(")");
        out[j] = '\0';

        add_edit_take_ownership(edits, ts, ne, out);
        rl_add(claimed, ts, ne);
        free(lits);
        free(exprs);
        return 1;
    }

    // Untagged → concatenation
    Piece *pieces = NULL;
    size_t np = 0, capP = 0;
    size_t li = 0, ei = 0;
    while (li < nl || ei < neP)
    {
        // if it begins with expression
        if( nl && neP && !li && !ei && exprs[0].start < lits[0].start)
        {
            Piece E = exprs[ei++];
            if (np == capP)
            {
                capP = capP ? capP * 2 : 8;
                REMALLOC(pieces, capP * sizeof(Piece));
            }
            pieces[np++] = (Piece){1, E.start, E.end};
        }
        if (li < nl)
        {
            Piece L = lits[li++];
            if (L.end > L.start)
            {
                if (np == capP)
                {
                    capP = capP ? capP * 2 : 8;
                    REMALLOC(pieces, capP * sizeof(Piece));
                }
                pieces[np++] = (Piece){0, L.start, L.end};
            }
        }
        if (ei < neP)
        {
            Piece E = exprs[ei++];
            if (np == capP)
            {
                capP = capP ? capP * 2 : 8;
                REMALLOC(pieces, capP * sizeof(Piece));
            }
            pieces[np++] = (Piece){1, E.start, E.end};
        }
    }

    if (np == 0)
    {
        if (np == capP)
        {
            capP = capP ? capP * 2 : 8;
            REMALLOC(pieces, capP * sizeof(Piece));
        }
        pieces[np++] = (Piece){0, ns, ns};
    }

    int need_leading_empty = (np > 0 && pieces[0].is_expr);
    size_t cap = 16 + (need_leading_empty ? 4 : 0);
    for (size_t i = 0; i < np; i++)
    {
        if (pieces[i].is_expr)
            cap += 4 + (pieces[i].end - pieces[i].start);
        else
            cap += 2 * (pieces[i].end - pieces[i].start) + 8;
        if (i || need_leading_empty)
            cap += 3;
    }

    char *out = NULL;
    size_t j = 0;

    REMALLOC(out, cap);

    if (need_leading_empty)
        adds("\"\"");
    for (size_t i = 0; i < np; i++)
    {
        if (i || need_leading_empty)
            adds(" + ");
        if (pieces[i].is_expr)
        {
            size_t L = pieces[i].end - pieces[i].start;
            // printf("piece='%.*s'\n", (int)L, src+pieces[i].start);
            // check for ':' and '%'
            const char *p = src + pieces[i].start;
            const char *e = src + pieces[i].end + 1;
            const char *fmtstart = NULL;
            const char *expstart = p;
            int fmtlen = 0;

            do
            {
                while (p < e && isspace(*p))
                    p++;
                if (*p == '%')
                {
                    fmtstart = p;
                    while (p < e && !isspace(*p) && *p != ':')
                        p++, fmtlen++;
                    while (p < e && isspace(*p))
                        p++;
                    if (*p != ':')
                    {
                        fmtstart = NULL;
                        break;
                    }
                }
                else if (*p == '"')
                {
                    p++;
                    fmtstart = p;
                    while (p < e && *p != '"')
                        p++, fmtlen++;
                    if (*p != '"')
                    {
                        fmtstart = NULL;
                        break;
                    }
                    p++;
                    while (p < e && isspace(*p))
                        p++;
                    if (*p != ':')
                    {
                        fmtstart = NULL;
                        break;
                    }
                }
                else
                    break;
                p++;
                L -= (p - expstart);
                expstart = p;
                // rampart.utils.sprintf("",
                cap += 25;
                out = realloc(out, cap);

            } while (0);
            if (fmtstart)
            {
                adds("rampart.utils.sprintf(\"");
                memcpy(out + j, fmtstart, fmtlen);
                j += fmtlen;
                adds("\",");
            }
            else
                adds("(");

            memcpy(out + j, expstart, L);
            j += L;
            adds(")");
        }
        else
        {
            int nnl;
            char *q = js_quote_literal(src, pieces[i].start, pieces[i].end, &nnl);

            adds(q);
            for (int k = 0; k < nnl; k++)
                adds("\n");
            free(q);
        }
    }
    out[j] = '\0';
#undef adds
    // printf("strlen=%d, cap=%d\n", strlen(out), (int)cap);
    add_edit_take_ownership(edits, ns, ne, out);
    rl_add(claimed, ns, ne);
    free(lits);
    free(exprs);
    free(pieces);
    return 1;
}

// Arrow functions (concise + block, with flat destructuring lowering)
static int rewrite_arrow_function_node(EditList *edits, const char *src, TSNode arrow_node, RangeList *claimed)
{
    size_t ns = ts_node_start_byte(arrow_node), ne = ts_node_end_byte(arrow_node);
    if (rl_overlaps(claimed, ns, ne))
        return 0;

    TSNode params = ts_node_child_by_field_name(arrow_node, "parameters", 9);
    TSNode body = ts_node_child_by_field_name(arrow_node, "body", 4);
    uint32_t n = ts_node_child_count(arrow_node);
    int arrow_idx = -1;
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode kid = ts_node_child(arrow_node, i);
        if (!ts_node_is_named(kid) && strcmp(ts_node_type(kid), "=>") == 0)
        {
            arrow_idx = (int)i;
            break;
        }
    }
    if (ts_node_is_null(params) && arrow_idx >= 0)
    {
        for (int i = arrow_idx - 1; i >= 0; i--)
        {
            TSNode kid = ts_node_child(arrow_node, (uint32_t)i);
            if (ts_node_is_named(kid))
            {
                params = kid;
                break;
            }
        }
    }
    if (ts_node_is_null(body) && arrow_idx >= 0)
    {
        for (uint32_t i = (uint32_t)arrow_idx + 1; i < n; i++)
        {
            TSNode kid = ts_node_child(arrow_node, i);
            if (ts_node_is_named(kid))
            {
                body = kid;
                break;
            }
        }
    }
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;

    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);
    int bind_this = contains_lexical_this(body);
    int is_block = (strcmp(ts_node_type(body), "statement_block") == 0); // Single-parameter pattern detection
    TSNode pattern = (TSNode){0};
    int single = 0;
    if (strcmp(ts_node_type(params), "formal_parameters") == 0)
    {
        if (ts_node_named_child_count(params) == 1)
        {
            pattern = ts_node_named_child(params, 0);
            single = 1;
        }
    }
    else
    {
        pattern = params;
        single = 1;
    }

    // Destructuring bindings (flat)
    int did = 0;
    Bindings binds;
    binds_init(&binds);
    const char *temp = NULL;
    if (single)
    {
        const char *pt = ts_node_type(pattern);
        if (strcmp(pt, "array_pattern") == 0)
        {
            temp = "_arr";
            did = collect_flat_destructure_bindings(pattern, src, temp, &binds);
        }
        else if (strcmp(pt, "object_pattern") == 0)
        {
            temp = "_obj";
            did = collect_flat_destructure_bindings(pattern, src, temp, &binds);
        }
        else if (strcmp(pt, "assignment_pattern") == 0)
        {
            TSNode left = ts_node_child_by_field_name(pattern, "left", 4);
            if (!ts_node_is_null(left))
            {
                const char *lt = ts_node_type(left);
                if (strcmp(lt, "array_pattern") == 0)
                {
                    temp = "_arr";
                    did = collect_flat_destructure_bindings(left, src, temp, &binds);
                }
                else if (strcmp(lt, "object_pattern") == 0)
                {
                    temp = "_obj";
                    did = collect_flat_destructure_bindings(left, src, temp, &binds);
                }
            }
        }
    }

    char *rep = NULL;
    if (did && !is_block)
    {
        char *rew = rewrite_concise_body_with_bindings(src, body, &binds);
        size_t cap = 64 + strlen(temp) + strlen(rew) + 1;

        REMALLOC(rep, cap);
        snprintf(rep, cap, "function (%s) { return %s; }", temp, rew);
        free(rew);
    }
    else if (did && is_block)
    {
        size_t body_len = be - bs;
        const char *bsrc = src + bs;
        size_t brace = 0;
        while (brace < body_len && bsrc[brace] != '{')
            brace++;
        if (brace < body_len)
            brace++;
        size_t decl_cap = 16;
        char *decl = NULL;
        int w;
        size_t k;

        for (size_t i = 0; i < binds.len; i++)
            decl_cap += strlen(binds.a[i].name) + strlen(binds.a[i].repl) + 4;

        REMALLOC(decl, decl_cap);

        size_t dj = 0;
        memcpy(decl + dj, "var ", 4);
        dj += 4;
        for (size_t i = 0; i < binds.len; i++)
        {
            size_t nlen = strlen(binds.a[i].name), rlen = strlen(binds.a[i].repl);
            memcpy(decl + dj, binds.a[i].name, nlen);
            dj += nlen;
            decl[dj++] = '=';
            memcpy(decl + dj, binds.a[i].repl, rlen);
            dj += rlen;
            if (i + 1 < binds.len)
            {
                decl[dj++] = ',';
                decl[dj++] = ' ';
            }
        }
        decl[dj++] = ';';
        decl[dj++] = ' ';
        decl[dj] = 0;

        size_t cap = 64 + (size_t)strlen(temp) + body_len + strlen(decl) + 1;

        REMALLOC(rep, cap);

        w = snprintf(rep, cap, "function (%s) ", temp);
        k = (size_t)w;
        memcpy(rep + k, bsrc, brace);
        k += brace;
        memcpy(rep + k, decl, strlen(decl));
        k += strlen(decl);
        memcpy(rep + k, bsrc + brace, body_len - brace);
        k += (body_len - brace);
        rep[k] = '\0';
        free(decl);
    }
    else
    {
        int needs_paren = !slice_starts_with_paren(src, ps, pe);
        size_t cap = 96 + (pe - ps) + (be - bs) + 1;

        REMALLOC(rep, cap);

        if (is_block)
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) %.*s", (int)(pe - ps), src + ps, (int)(be - bs), src + bs);
            else
                snprintf(rep, cap, "function %.*s %.*s", (int)(pe - ps), src + ps, (int)(be - bs), src + bs);
        }
        else
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) { return %.*s; }", (int)(pe - ps), src + ps, (int)(be - bs),
                         src + bs);
            else
                snprintf(rep, cap, "function %.*s { return %.*s; }", (int)(pe - ps), src + ps, (int)(be - bs),
                         src + bs);
        }
    }

    
    // Inject default initializers for arrows if any assignment_pattern exists
    {
        int has_defaults = params_has_assignment_pattern(params);
        if (has_defaults && rep) {
            char *default_inits = build_param_default_inits(src, params);
            if (default_inits) {
                const char *fun_kw = "function ";
                char *p = strstr(rep, fun_kw);
                if (p) {
                    char *po = strchr(p + strlen(fun_kw), '(');
                    if (po) {
                        // find closing paren
                        int dp = 0; char *pc = po;
                        while (*pc) { if (*pc=='(') dp++; else if (*pc==')'){ dp--; if (dp==0) break; } pc++; }
                        if (*pc == ')') {
                            char *brace = strchr(rep, '{');
                            if (brace) {
                                size_t head_len = (size_t)(po - rep);
                                size_t pre_block = (size_t)(brace - rep + 1);
                                size_t deflen = strlen(default_inits);
                                size_t new_len = head_len + 3 + (strlen(rep) - (po - rep)) + deflen;
                                char *nr = NULL; REMALLOC(nr, new_len + 1);
                                size_t k = 0;
                                memcpy(nr + k, rep, head_len); k += head_len;
                                memcpy(nr + k, "() ", 3); k += 3;
                                memcpy(nr + k, pc + 1, pre_block - (pc + 1 - rep)); k += pre_block - (pc + 1 - rep);
                                memcpy(nr + k, default_inits, deflen); k += deflen;
                                memcpy(nr + k, rep + pre_block, strlen(rep) - pre_block); k += strlen(rep) - pre_block;
                                nr[k] = 0;
                                free(rep); rep = nr;
                            }
                        }
                    }
                }
                free(default_inits);
            }
        }
    }
if (bind_this)
    {
        size_t cap_bind = strlen(rep) + 20;
        char *wrapped = NULL;

        REMALLOC(wrapped, cap_bind);

        if (!wrapped)
        {
            fprintf(stderr, "oom\n");
            exit(1);
        }
        snprintf(wrapped, cap_bind, "(%s).bind(this)", rep);
        free(rep);
        rep = wrapped;
    }
    add_edit_take_ownership(edits, ns, ne, rep);
    rl_add(claimed, ns, ne);
    binds_free(&binds);
    return 1;
}

// let/const -> var (token edit)
static void rewrite_lexical_declaration(EditList *edits, const char *src, TSNode lexical_decl)
{
    (void)src;
    uint32_t c = ts_node_child_count(lexical_decl);
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(lexical_decl, i);
        if (ts_node_is_named(kid))
            break;
        const char *kw = ts_node_type(kid);
        if (strcmp(kw, "let") == 0 || strcmp(kw, "const") == 0)
        {
            add_edit(edits, ts_node_start_byte(kid), ts_node_end_byte(kid), "var");
            break;
        }
    }
}

static void rewrite_array_spread(EditList *edits, const char *src, TSNode arr, int isObject, uint8_t *polysneeded)
{
    (void)src;
    uint32_t cnt2, cnt1 = ts_node_child_count(arr);
    uint32_t i, j;
    char *out = NULL;
// ._addchain(rampart.__spreadO({},))
#define spreadsize 34
// ._concat({})
#define addcsize 12
#define isplain 0
#define isspread 1
#define newobj "_TrN_Sp._newObject()"
#define newobjsz 20
#define newarr "_TrN_Sp._newArray()"
#define newarrsz 19

    size_t needed = 1 + newobjsz; // for "_TrN_Sp._newObject()" and '\0'

    int nshort = 0;
    int nspread = 0;


    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);
        if (strcmp(ts_node_type(kid), "spread_element") == 0)
        {
            cnt2 = ts_node_child_count(kid);
            // printf("%d child elem of spread\n", cnt2);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                // printf("looking at '%.*s'\n", (ts_node_end_byte(gkid) - ts_node_start_byte(gkid)),
                // src+ts_node_start_byte(gkid));
                if (strcmp(ts_node_type(gkid), "identifier") == 0)
                {
                    needed += ((ts_node_end_byte(gkid) - ts_node_start_byte(gkid)) + spreadsize);
                    nspread++;
                }
            }
        }
        else if (ts_node_is_named(kid))
        {
            if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
            {
                needed += (ts_node_end_byte(kid) - ts_node_start_byte(kid)) * 2 + addcsize + 1; // x -> x:x
                nshort++;
            }
            else
                needed += (ts_node_end_byte(kid) - ts_node_start_byte(kid)) + addcsize;
        }
    }

    if (!nspread)
    {
        if (!nshort)
            return;

        // handle shorthand if there are no ...vars
        int firstdone = 0;
        out = calloc(1, needed);
        *out = '{';

        for (i = 0; i < cnt1; i++)
        {
            TSNode kid = ts_node_child(arr, i);
            if (ts_node_is_named(kid))
            {
                size_t beg = ts_node_start_byte(kid), end = ts_node_end_byte(kid);

                if (firstdone)
                    strcat(out, ",");

                strncat(out, src + beg, end - beg);
                if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
                {
                    strcat(out, ":");
                    strncat(out, src + beg, end - beg);
                }
                firstdone = 1;
            }
        }
        strcat(out, "}");
        add_edit_take_ownership(edits, ts_node_start_byte(arr), ts_node_end_byte(arr), out);
        return;
    }

    /* process ...var */
    const char *fpref = "._addchain(_TrN_Sp.__spreadA([],";
    char open = '[', close = ']';
    int spos = 0;

    out = calloc(1, needed);
    if (isObject)
    {
        fpref = "._addchain(_TrN_Sp.__spreadO({},";
        open = '{';
        close = '}';
    }

#define addchar(c)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        *(out + spos++) = (c);                                                                                         \
    } while (0)
#define addstr(s, l)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        memcpy((out + spos), (s), l);                                                                                  \
        spos += l;                                                                                                     \
    } while (0)

    if(isObject)
        addstr(newobj,newobjsz);
    else
        addstr(newarr,newarrsz); 

    int lasttype = -1;

    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);

        if (strcmp(ts_node_type(kid), "spread_element") == 0)
        {
            cnt2 = ts_node_child_count(kid);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                if (strcmp(ts_node_type(gkid), "identifier") == 0)
                {
                    size_t start = ts_node_start_byte(gkid), end = ts_node_end_byte(gkid);
                    addstr(fpref, 32);
                    addstr(src + start, (end - start));
                    addchar(')');
                    addchar(')');
                    lasttype = isspread;
                }
            }
        }
        else if (ts_node_is_named(kid))
        {
            size_t start = ts_node_start_byte(kid), end = ts_node_end_byte(kid);

            if (lasttype == isplain)
            {
                spos -= 2; // go back to the }
                addchar(',');
            }
            else
            {
                addstr("._concat(", 9);
                addchar(open);
            }

            addstr(src + start, (end - start));
            if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
            {
                addchar(':');
                addstr(src + start, (end - start));
            }
            addchar(close);
            addchar(')');
            lasttype = isplain;
        }
    }
#undef spreadsize
#undef addcsize
#undef isplain
#undef isspread
#undef addchar
#undef addstr
#undef newobj
#undef newobjsz
#undef newarr
#undef newarrsz
    // printf("strlen=%d, alloc'ed=%d + 1\n", strlen(out), (int)needed-1);
    add_edit_take_ownership(edits, ts_node_start_byte(arr), ts_node_end_byte(arr), out);
    *polysneeded |= SPREAD_PF;
}

// ============== unified dispatcher/pass ==============

// ===== ES5 Unicode regex transformer (reintroduced) =====
typedef struct { char *s; size_t len; size_t cap; } SBuf;
static void sb_init(SBuf *b){ b->cap=256; b->len=0; b->s=(char*)malloc(b->cap); b->s[0]='\0'; }
static void sb_grow(SBuf *b, size_t need){ if (b->len + need + 1 > b->cap){ while (b->len + need + 1 > b->cap) b->cap = (b->cap<4096? b->cap*2 : b->cap + need + 1024); b->s=(char*)realloc(b->s,b->cap);} }
static void sb_putc(SBuf *b, char c){ sb_grow(b,1); b->s[b->len++]=c; b->s[b->len]='\0'; }
static void sb_puts(SBuf *b, const char *s){ size_t n=strlen(s); sb_grow(b,n); memcpy(b->s+b->len,s,n); b->len+=n; b->s[b->len]='\0'; }
static void sb_putsn(SBuf *b, const char *s, size_t n){ sb_grow(b,n); memcpy(b->s+b->len,s,n); b->len+=n; b->s[b->len]='\0'; }
static void sb_put_u4(SBuf *b, unsigned u){ char tmp[7]; snprintf(tmp,sizeof(tmp),"\\u%04X",u&0xFFFFu); sb_puts(b,tmp); }

static int hexv(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return 10+c-'a'; if(c>='A'&&c<='F')return 10+c-'A'; return -1; }
static void cp_to_surrogates_ul(unsigned long cp, unsigned *hi, unsigned *lo){ unsigned long v=cp-0x10000UL; *hi=0xD800u + (unsigned)((v>>10)&0x3FFu); *lo=0xDC00u + (unsigned)(v&0x3FFu); }

static const char *BIG_DOT = "(?:[\\0-\\t\\x0B\\f\\x0E-\\u2027\\u202A-\\uD7FF\\uE000-\\uFFFF]|[\\uD800-\\uDBFF][\\uDC00-\\uDFFF]|[\\uD800-\\uDBFF](?![\\uDC00-\\uDFFF])|(?:[^\\uD800-\\uDBFF]|^)[\\uDC00-\\uDFFF])";


static void append_astral_range(SBuf *out, unsigned long a, unsigned long z){
    unsigned ha, la, hz, lz;
    cp_to_surrogates_ul(a, &ha, &la);
    cp_to_surrogates_ul(z, &hz, &lz);

    if (ha == hz) {
        sb_puts(out, "(?:");
        sb_put_u4(out, ha);
        sb_putc(out, '[');
        sb_put_u4(out, la);
        sb_putc(out, '-');
        sb_put_u4(out, lz);
        sb_putc(out, ']');
        sb_putc(out, ')');
        return;
    }

    /* first block: ha [la-DFFF] */
    sb_puts(out, "(?:");
    sb_put_u4(out, ha);
    sb_putc(out, '[');
    sb_put_u4(out, la);
    sb_putc(out, '-');
    sb_put_u4(out, 0xDFFFu);
    sb_putc(out, ']');
    sb_putc(out, ')');

    /* middle blocks: [ha+1 - hz-1][DC00-DFFF] */
    if (ha + 1 <= hz - 1) {
        sb_putc(out, '|');
        sb_puts(out, "(?:");
        sb_putc(out, '[');
        sb_put_u4(out, ha + 1);
        sb_putc(out, '-');
        sb_put_u4(out, hz - 1);
        sb_putc(out, ']');
        sb_putc(out, '[');
        sb_put_u4(out, 0xDC00u);
        sb_putc(out, '-');
        sb_put_u4(out, 0xDFFFu);
        sb_putc(out, ']');
        sb_putc(out, ')');
    }

    /* last block: hz [DC00-lz] */
    sb_putc(out, '|');
    sb_puts(out, "(?:");
    sb_put_u4(out, hz);
    sb_putc(out, '[');
    sb_put_u4(out, 0xDC00u);
    sb_putc(out, '-');
    sb_put_u4(out, lz);
    sb_putc(out, ']');
    sb_putc(out, ')');
}


static char *rewrite_class_es5(const char *s, size_t len, size_t *i){
    size_t p = *i;
    if (p>=len || s[p] != '[') return NULL;
    p++;
    int neg = 0; if (p<len && s[p]=='^'){ neg=1; p++; }

    SBuf bmp; sb_init(&bmp);
    SBuf astral; sb_init(&astral);

    int esc = 0;
    int first = 1;
    int have_dash = 0;

    int pending = 0;
    unsigned long pend_cp = 0;
    int pend_is_cp = 0;

    #define EMIT_PENDING() do{         if (pending){             if (pend_is_cp){                 if (pend_cp <= 0xFFFFUL){ sb_put_u4(&bmp, (unsigned)pend_cp); }                 else { if (astral.len) sb_putc(&astral,'|'); append_astral_range(&astral, pend_cp, pend_cp); }             }             pending = 0; pend_is_cp = 0; pend_cp = 0;         }     } while(0)

    while (p < len){
        char c = s[p];
        if (!esc) {
            if (c == '\\'){ esc = 1; p++; continue; }
            if (c == ']' && !first){ if (have_dash){ sb_putc(&bmp, '-'); have_dash = 0; } p++; break; }
            if (c == '-' && !first && !have_dash){ have_dash = 1; p++; continue; }

            first = 0;
            if (have_dash && pending && pend_is_cp){
                unsigned long a = pend_cp, b = (unsigned char)c; if (a>b){ unsigned long t=a;a=b;b=t; }
                if (a<=0xFFFFUL && b<=0xFFFFUL){
                    sb_put_u4(&bmp, (unsigned)a); sb_putc(&bmp,'-'); sb_put_u4(&bmp, (unsigned)b);
                } else {
                    if (a<0x10000UL) a=0x10000UL;
                    if (astral.len) sb_putc(&astral,'|');
                    append_astral_range(&astral, a, b);
                }
                pending = 0; pend_is_cp = 0; pend_cp = 0; have_dash = 0;
                p++; continue;
            }
            EMIT_PENDING();
            pending = 1; pend_is_cp = 1; pend_cp = (unsigned char)c;
            p++; continue;
        } else {
            first = 0;
            size_t savep = p;
            unsigned long cp = 0;
            int r = 0;
            if (p < len && s[p]=='u'){
                size_t q = p;
                if (q+1 < len && s[q+1]=='{'){
                    q+=2; unsigned long v=0; int hv;
                    while (q<len && s[q] != '}'){ hv=hexv((unsigned char)s[q++]); if (hv<0){ v=0; q=0; break; } v=(v<<4)+hv; if (v>0x10FFFFUL){ v=0; q=0; break; } }
                    if (q && q<len && s[q]=='}'){ r=1; cp=v; p = q+1; }
                } else if (q+4 < len){
                    int v1=hexv((unsigned char)s[q+1]), v2=hexv((unsigned char)s[q+2]), v3=hexv((unsigned char)s[q+3]), v4=hexv((unsigned char)s[q+4]);
                    if (v1>=0&&v2>=0&&v3>=0&&v4>=0){ r=1; cp=(unsigned)((v1<<12)|(v2<<8)|(v3<<4)|v4); p = q+5; }
                }
            } else if (p < len && s[p]=='x'){
                if (p+2 < len){
                    int v1=hexv((unsigned char)s[p+1]), v2=hexv((unsigned char)s[p+2]);
                    if (v1>=0 && v2>=0){ r=1; cp=(unsigned)(v1<<4|v2); p += 3; }
                }
            }
            if (r==1){
                if (!have_dash){
                    EMIT_PENDING();
                    pending = 1; pend_is_cp = 1; pend_cp = cp;
                } else {
                    if (!pending || !pend_is_cp){
                        have_dash = 0;
                        sb_putc(&bmp, '-');
                        EMIT_PENDING();
                        pending = 1; pend_is_cp = 1; pend_cp = cp;
                    } else {
                        unsigned long a = pend_cp, b = cp; if (a>b){ unsigned long t=a;a=b;b=t; }
                        if (a<=0xFFFFUL && b<=0xFFFFUL){
                            sb_put_u4(&bmp, (unsigned)a); sb_putc(&bmp,'-'); sb_put_u4(&bmp, (unsigned)b);
                        } else {
                            if (a<0x10000UL) a=0x10000UL;
                            if (astral.len) sb_putc(&astral,'|');
                            append_astral_range(&astral, a, b);
                        }
                        pending = 0; pend_is_cp = 0; pend_cp = 0;
                        have_dash = 0;
                    }
                }
                esc = 0;
                continue;
            } else {
                EMIT_PENDING();
                sb_putc(&bmp, '\\'); sb_putc(&bmp, s[savep]);
                p = savep + 1;
                esc = 0;
                continue;
            }
        }
    }

    EMIT_PENDING();
    if (have_dash){ sb_putc(&bmp, '-'); have_dash = 0; }

    SBuf out; sb_init(&out);
    if (!neg){
        if (astral.len == 0){
            sb_putc(&out,'['); sb_putsn(&out, bmp.s, bmp.len); sb_putc(&out,']');
        } else {
            sb_puts(&out,"(?:");
            if (bmp.len){ sb_putc(&out,'['); sb_putsn(&out,bmp.s,bmp.len); sb_putc(&out,']'); sb_putc(&out,'|'); }
            sb_putsn(&out, astral.s, astral.len);
            sb_putc(&out,')');
        }
    } else {
        sb_puts(&out,"(?:");
        sb_puts(&out,"[^\\uD800-\\uDFFF"); if (bmp.len) sb_putsn(&out,bmp.s,bmp.len); sb_putc(&out,']'); sb_putc(&out,'|');
        if (astral.len){ sb_puts(&out,"(?!"); sb_putsn(&out,astral.s,astral.len); sb_putc(&out,')'); }
        sb_puts(&out,"(?:[\\uD800-\\uDBFF][\\uDC00-\\uDFFF])");
        sb_putc(&out,')');
    }
    free(bmp.s); free(astral.s);
    *i = p;
    return out.s;
}
static char *regex_u_to_es5_pattern(const char *in, size_t len){
    SBuf out; sb_init(&out);
    int esc=0;
    for (size_t i=0;i<len;){
        char c=in[i];
        if (!esc){
            if (c=='\\'){ esc=1; sb_putc(&out,'\\'); i++; continue; }
            if (c=='['){ char *cls=rewrite_class_es5(in,len,&i); if(!cls){ free(out.s); return NULL;} sb_puts(&out,cls); free(cls); continue; }
            if (c=='.'){ sb_puts(&out, BIG_DOT); i++; continue; }
            sb_putc(&out,c); i++; continue;
        } else {
            size_t j=i+1;
            if (in[i]=='u' && j<len && in[j]=='{'){
                out.len--; out.s[out.len]='\0';
                unsigned long cp=0; int hv; j++;
                while (j<len && in[j] != '}'){ hv=hexv((unsigned char)in[j++]); if (hv<0){ free(out.s); return NULL; } cp=(cp<<4)+hv; if (cp>0x10FFFFUL){ free(out.s); return NULL; } }
                if (j>=len || in[j] != '}'){ free(out.s); return NULL; }
                j++;
                if (cp<=0xFFFFUL){ sb_put_u4(&out,(unsigned)cp); }
                else { unsigned hi,lo; cp_to_surrogates_ul(cp,&hi,&lo); sb_put_u4(&out,hi); sb_put_u4(&out,lo); }
                i=j; esc=0; continue;
            } else {
                sb_putc(&out,in[i]); i++; esc=0; continue;
            }
        }
    }
    return out.s;
}

static int rewrite_regex_u_to_es5(EditList *edits, const char *src, TSNode regex_node, RangeList *claimed){
    if (strcmp(ts_node_type(regex_node), "regex") != 0) return 0;
    size_t rs=ts_node_start_byte(regex_node), re=ts_node_end_byte(regex_node);
    if (rl_overlaps(claimed, rs, re)) return 0;
    TSNode pattern = find_child_type(regex_node, "regex_pattern", NULL);
    TSNode flags   = find_child_type(regex_node, "regex_flags", NULL);
    if (ts_node_is_null(pattern) || ts_node_is_null(flags)) return 0;
    size_t ps=ts_node_start_byte(pattern), pe=ts_node_end_byte(pattern);
    size_t fs=ts_node_start_byte(flags), fe=ts_node_end_byte(flags);
    int has_u=0; for (size_t i=fs;i<fe;i++) if (src[i]=='u'){ has_u=1; break; }
    if (!has_u) return 0;
    char *newpat = regex_u_to_es5_pattern(src+ps, pe-ps);
    if (!newpat) return 0;
    size_t nflen=0; char *newflags=NULL;
    if (fe>fs){ newflags=(char*)malloc((fe-fs)+1); for(size_t i=fs;i<fe;i++) if (src[i] != 'u') newflags[nflen++]=src[i]; newflags[nflen]='\0'; }
    size_t outlen = 1 + strlen(newpat) + 1 + nflen;
    char *rep = (char*)malloc(outlen+1); size_t k=0;
    rep[k++]='/'; memcpy(rep+k,newpat,strlen(newpat)); k+=strlen(newpat); rep[k++]='/'; if(nflen){ memcpy(rep+k,newflags,nflen); k+=nflen; } rep[k]='\0';
    free(newpat); if (newflags) free(newflags);
    add_edit_take_ownership(edits, rs, re, rep); rl_add(claimed, rs, re);
    return 1;
}

static void tp_linecol_from_src_offset_utf8(const char *src,
                                            size_t src_len,
                                            uint32_t byte_off,
                                            int *out_line,
                                            int *out_col)
{
    if (!src) { if (out_line) *out_line = 1; if (out_col) *out_col = 1; return; }

    if (src_len == 0) {
        src_len = strlen(src); // safe if NUL-terminated; otherwise pass length explicitly
    }

    if (byte_off > src_len) {
        byte_off = (uint32_t)src_len; // clamp
    }

    // Count lines up to byte_off, tracking start index of current line.
    uint32_t line = 1;            // 1-based
    size_t   line_start = 0;      // byte index of current line start

    for (size_t i = 0; i < byte_off; ) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\n') {
            line++;
            line_start = i + 1;
            i++;
            continue;
        }
        if (c == '\r') {
            // Treat CRLF as one newline; lone CR as newline too.
            if (i + 1 < byte_off && (unsigned char)src[i + 1] == '\n') {
                i += 2;
            } else {
                i += 1;
            }
            line++;
            line_start = i;
            continue;
        }
        // Not a newline; advance by one byte
        i++;
    }

    // Column in UTF-8 code points from line_start to byte_off (exclusive).
    // Count only leading bytes of code points (bytes where (b & 0xC0) != 0x80).
    int col = 1; // 1-based
    for (size_t i = line_start; i < byte_off; i++) {
        unsigned char b = (unsigned char)src[i];
        if ((b & 0xC0) != 0x80) {
            col++;
        }
    }
    col -= 1; // we started at 1 then increment before first char; normalize

    if (out_line) *out_line = (int)line;
    if (out_col)  *out_col  = (int)col;
}

RP_ParseRes transpiler_rewrite(
    EditList *edits,
    const char *src,
    size_t src_len,
    TSNode root,
    uint8_t *polysneeded)
{
    RP_ParseRes ret;
    ret.err=0;
    ret.line_num=0;
    ret.col_num=0;
    ret.altered=0;
    ret.pos=0;

    // it will never find the position of the error (actually it will be at end of file, which is not helpful).
    //  Just return err=2
/*
    if (ts_node_has_error(root)) {
        ret.err = 2;
        return ret;
    }
*/
    RangeList claimed;
    rl_init(&claimed);
    TSTreeCursor cur = ts_tree_cursor_new(root);

    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *nt = ts_node_type(n);
        size_t ns = ts_node_start_byte(n), ne = ts_node_end_byte(n);

        // Other errors
        if ( !ret.err && (nt && strcmp(nt, "ERROR") == 0) ) {
            ret.err=1;
            ret.pos = ts_node_start_byte(n);
            tp_linecol_from_src_offset_utf8(src, src_len, ret.pos, &ret.line_num, &ret.col_num);
        }

        if (!rl_overlaps(&claimed, ns, ne))
        {
            int handled = 0;

            if (strcmp(nt, "regex") == 0) {
                handled = rewrite_regex_u_to_es5(edits, src, n, &claimed);
            }

            if (!handled && (strcmp(nt, "import_statement") == 0))
            {
                handled = rewrite_import_node(edits, src, n, &claimed, polysneeded);
            }
            if (!handled && (strcmp(nt, "template_string") == 0 || strcmp(nt, "template_literal") == 0))
            {
                handled = rewrite_template_node(edits, src, n, &claimed);
            }
            if (!handled && (strcmp(nt, "string") == 0 || strcmp(nt, "template_literal") == 0))
            {
                handled = rewrite_raw_node(edits, src, n, &claimed);
            }
            if (!handled && strcmp(nt, "arrow_function") == 0)
            {
                handled = rewrite_arrow_function_node(edits, src, n, &claimed);
            }
            if (!handled && strcmp(nt, "variable_declaration") == 0)
            {
                handled = rewrite_var_function_expression_defaults(edits, src, n, &claimed);
            }
            if (!handled &&
                (strcmp(nt, "function_declaration") == 0 || strcmp(nt, "function") == 0 || strcmp(nt, "function_expression") == 0 ||
                 strcmp(nt, "generator_function_declaration") == 0 || strcmp(nt, "generator_function") == 0 || strcmp(nt, "generator_function_expression") == 0))
            {
                handled = rewrite_function_like_default_params(edits, src, n, &claimed);
            if (!handled)
                handled = rewrite_function_rest(edits, src, n, &claimed);
            }
            
            if (!handled && strcmp(nt, "expression_statement") == 0)
            {
                TSNode expr = ts_node_named_child(n, 0);
                if (!ts_node_is_null(expr)) {
                    const char *et = ts_node_type(expr);
                    if (strcmp(et, "function_expression") == 0 || strcmp(et, "function") == 0 ||
                        strcmp(et, "generator_function_expression") == 0 || strcmp(et, "generator_function") == 0) {
                        TSNode ep = ts_node_child_by_field_name(expr, "parameters", 10);
                        if (!ts_node_is_null(ep) && params_has_assignment_pattern(ep)) {
                            handled = rewrite_function_like_default_params(edits, src, expr, &claimed);
                        }
                    }
                }
            }

            if (!handled && strcmp(nt, "lexical_declaration") == 0)
            {
                rewrite_lexical_declaration(edits, src, n);
            }
            if (!handled && strcmp(nt, "array") == 0)
            {
                rewrite_array_spread(edits, src, n, 0, polysneeded);
            }
            if (!handled && strcmp(nt, "object") == 0)
            {
                rewrite_array_spread(edits, src, n, 1, polysneeded);
            }
        }

        if (ts_tree_cursor_goto_first_child(&cur))
            continue;
        while (!ts_tree_cursor_goto_next_sibling(&cur))
        {
            if (!ts_tree_cursor_goto_parent(&cur))
            {
                ts_tree_cursor_delete(&cur);
                free(claimed.a);
                return ret;
            }
        }
    }
    return ret;
}

RP_ParseRes transpile(const char *src, size_t src_len, int printTree)
{
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_javascript());
    TSTree *tree;
    TSNode root;
    uint8_t polysneeded = 0;
    FILE *f = stdout;
    
    //pass a -1 or a 0 to get length, but use TRANSPILE_CALC_SIZE (0)
    if(!src_len || (ssize_t)src_len == -1)
        src_len=strlen(src);

    // if we mistakenly gave len to include ending '\0'
    if( *(src+src_len) == '\0')
        src_len--;

    tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)src_len);
    root = ts_tree_root_node(tree);


    if (printTree == 2)
        f = stderr;

    if (printTree)
    {
        fputs(
            "=== Outline ===\n  node_type(node_field_name) [start, end]\n     or if field_name is NULL\n  node_type [start, end]\n",
            f);
        print_outline(src, root, 0, f);
        fputs("---------------------------------------------\n", f);
    }

    EditList edits;
    init_edits(&edits);

    // Single traversal with handler dispatch
    RP_ParseRes res = transpiler_rewrite(&edits, src, src_len, root, &polysneeded);

    if(edits.len)
    {
        res.transpiled = apply_edits(src, src_len, &edits, polysneeded);
        res.altered=1;
    }
    else
        res.transpiled = NULL;

    free_edits(&edits);
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return res;
}

#ifdef TEST
// ============== small IO utils ==============
static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "rb");
    long n;
    size_t r;
    char *buf = NULL;

    if (!f)
    {
        perror("open");
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    n = ftell(f);

    if (n < 0)
    {
        perror("ftell");
        exit(1);
    }

    fseek(f, 0, SEEK_SET);

    REMALLOC(buf, (size_t)n + 1);

    r = fread(buf, 1, (size_t)n, f);
    if (strcmp(path, "-") != 0)
        fclose(f);

    buf[r] = '\0';

    if (out_len)
        *out_len = r;

    return buf;
}
// ============== main ==============
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <path-to-js> [--print-ast]\n       Use '-' to read from stdin.\n", argv[0]);
        return 2;
    }
    int printTree = (argc >= 3 && strcmp(argv[2], "--print-tree") == 0) ? 2 : 0;

    printTree = (argc >= 3 && strcmp(argv[2], "--printTree") == 0) ? 2 : 0;

    size_t src_len = 0;
    char *src = read_entire_file(argv[1], &src_len);

    RP_ParseRes res = transpile(src, src_len, printTree);
    if(res.transpiled)
        fwrite(res.transpiled, 1, strlen(res.transpiled), stdout);

    if(res.err) {
        if (res.err && res.transpiled)
        {
            char *p = src + res.pos;
            char *s=p, *e=p, *fe=src+src_len;

            fprintf(stderr, "Transpiler Parse Error (line %d)\n", res.line_num);
            while(s>=src && *s !='\n') s--;
            s++;
            while(e <= fe && *e != '\n') e++;
            fprintf(stderr, "%.*s\n", (int)(e-s), s);
            while(s<p) {fputc(' ', stderr);s++;}
            fputc('^', stderr);
            fputc('\n', stderr);
            free(res.transpiled);
            free(src);

            return(1);
        }
    }
    // Cleanup
    if(res.transpiled)
        free(res.transpiled);
    free(src);
    return 0;
}
#endif
