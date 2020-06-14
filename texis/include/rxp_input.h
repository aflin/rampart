#ifndef INPUT_H
#define INPUT_H

#ifdef NEVER_MAW
#include <stdio.h>
#include "rxp_charset.h"
#include "rxp_stdio16.h"
#include "rxp_dtd.h"
#endif

/* Typedefs */

typedef struct input_source *InputSource;

/* Input sources */

XML_API InputSource SourceFromFILE16 ARGS((CONST char8 *description, FILE16 *file16));
XML_API InputSource SourceFromStream ARGS((CONST char8 *description, FILE *file));
XML_API InputSource EntityOpen ARGS((Entity e));
XML_API InputSource NewInputSource ARGS((Entity e, FILE16 *f16));
XML_API void SourceClose ARGS((InputSource source));
XML_API int SourceTell ARGS((InputSource s));
XML_API int SourceSeek ARGS((InputSource s, int byte_offset));
XML_API int SourceLineAndChar ARGS((InputSource s, int *linenum, int *charnum));
XML_API void SourcePosition ARGS((InputSource s, Entity *entity, int *byte_offset));
XML_API int get_with_fill ARGS((InputSource s));
XML_API void determine_character_encoding ARGS((InputSource s));

struct input_source {
    Entity entity;		/* The entity from which the source reads */

    FILE16 *file16;

    Char *line;
    int line_alloc, line_length;
    int next;

    int seen_eoe;
    int complicated_utf8_line;
    int bytes_consumed;
    int bytes_before_current_line;
    int line_end_was_cr;

    int line_number;
    int not_read_yet;

    struct input_source *parent;

    int nextin;
    int insize;
    unsigned char inbuf[4096];
};

/* EOE used to be -2, but that doesn't work if Char is signed char */
#define XEOE (-999)

#define at_eol(s) ((s)->next == (s)->line_length)
#define get(s)    (at_eol(s) ? get_with_fill(s) : (s)->line[(s)->next++])
#define unget(s)  ((s)->seen_eoe ? (s)->seen_eoe= 0 : (s)->next--)

#endif /* INPUT_H */
