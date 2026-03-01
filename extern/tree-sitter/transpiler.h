// transpiler.h
#ifndef TRANSPILER_H
#define TRANSPILER_H
#include <stddef.h>
#include "lib/include/tree_sitter/api.h"

typedef struct {
    char *transpiled;
    char *errmsg;
    size_t pos;
    int err;
    int altered;
    int line_num;
    int col_num;
} RP_ParseRes;

typedef struct {
    size_t start;      // byte offset (inclusive)
    size_t end;        // byte offset (exclusive)
    char  *text;       // replacement (owned if own_text=true)
    int    own_text;   // whether to free text on cleanup
} Edit;

typedef struct {
    Edit *items;
    size_t len;
    size_t cap;
} EditList;

//void transpiler_rewrite(EditList *edits, const char *src, TSNode root, uint8_t *polysneeded);

/*
    src -     - javascript in
    src_len   - lenght of javascript in string
    printTree -
                0: don't print tree
                2: print tree to stderr
                Any other value other than 0 or 2: print tree to stdout

    Returns a malloc'ed.
*/

#define TRANSPILE_CALC_SIZE 0

/* tracks which polyfills have already been added, if called more than once */
RP_ParseRes transpile(const char *src, size_t src_len, int printTree);

/* will add polyfills for every call */
RP_ParseRes transpile_standalone(const char *src, size_t src_len, int printTree);

/* like transpile() but skips program-level IIFE wrapping (for eval'd code) */
RP_ParseRes transpile_eval(const char *src, size_t src_len, int printTree);

void freeParseRes(RP_ParseRes *res);
char *stealParseRes(RP_ParseRes *res);


#endif
