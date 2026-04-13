// minify.h
#ifndef MINIFY_H
#define MINIFY_H
#include <stddef.h>

typedef struct {
    char   *code;       /* minified output (malloc'd) */
    size_t  code_len;   /* length of output */
    char   *error;      /* error message if any (malloc'd), else NULL */
} MinifyResult;

MinifyResult minify(const char *src, size_t src_len);
void minify_result_free(MinifyResult *r);

#endif
