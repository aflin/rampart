// jcolor.c
// Build: cc -DTEST -O2 -Wall -o jcolor jcolor.c
// Demo:  ./json_color_str '{"a":1,"b":[true,null,"x"]}'                # pretty + colors
//        ./json_color_str '{"a":1,"b":[true,null,"x"]}' 0 nocolor      # compact + colors
//        ./json_color_str '{"a":1,"b":[true,null,"x"]}' 2 nocolor      # pretty, no colors

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "jcolor.h"

/* ANSI colors */

typedef enum { CTX_OBJECT, CTX_ARRAY } CtxType;

/* Simple stack */
typedef struct {
    CtxType *data;
    size_t size, cap;
} Stack;

static void stack_init(Stack *s){ s->data=NULL; s->size=0; s->cap=0; }
static void stack_free(Stack *s){ free(s->data); }
static void stack_push(Stack *s, CtxType t){
    if(s->size==s->cap){
        s->cap = s->cap? s->cap*2 : 8;
        CtxType *tmp = (CtxType*)realloc(s->data, s->cap*sizeof(*s->data));
        if(!tmp){ perror("realloc"); exit(1); }
        s->data = tmp;
    }
    s->data[s->size++] = t;
}
static bool stack_empty(const Stack *s){ return s->size==0; }
static CtxType stack_top(const Stack *s){ return s->data[s->size-1]; }
static void stack_pop(Stack *s){ if(s->size) s->size--; }

/* Dynamic buffer */
typedef struct {
    char *buf;
    size_t len, cap;
} DynBuf;

static void dbuf_init(DynBuf *db){ db->buf=NULL; db->len=0; db->cap=0; }
static void dbuf_append(DynBuf *db, const char *s, size_t n){
    if(db->len+n+1 > db->cap){
        size_t newcap = db->cap? db->cap*2 : 256;
        while(newcap < db->len+n+1) newcap *= 2;
        char *tmp = (char*)realloc(db->buf, newcap);
        if(!tmp){ perror("realloc"); exit(1); }
        db->buf = tmp; db->cap = newcap;
    }
    memcpy(db->buf + db->len, s, n);
    db->len += n;
    db->buf[db->len] = '\0';
}
static void dbuf_putc(DynBuf *db, char c){ dbuf_append(db, &c, 1); }
static void dbuf_puts(DynBuf *db, const char *s){ dbuf_append(db, s, strlen(s)); }
static void dbuf_indent(DynBuf *db, int spaces, int level){
    for(int i=0;i<spaces*level;i++) dbuf_putc(db,' ');
}


static Jcolors choose_colors(int colors_flag){
    if(colors_flag == NO_COLORS){
        Jcolors c = { "", "", "", "", "", "" };
        return c;
    }else{
        Jcolors c = { C_RESET_DEF, C_KEY_DEF, C_STRING_DEF, C_NUMBER_DEF, C_BOOL_DEF, C_NULL_DEF };
        return c;
    }
}

/* Read/print a JSON string token starting at json[i] == '"' */
static size_t print_string(const char *json, size_t i, DynBuf *out, const char *color, const Jcolors *C){
    dbuf_puts(out, color);
    dbuf_putc(out, '"');
    i++; /* past opening quote */
    bool esc = false;
    for(; json[i] != '\0'; ++i){
        char c = json[i];
        dbuf_putc(out, c);
        if(esc){
            esc = false;
        }else if(c == '\\'){
            esc = true;
        }else if(c == '"'){
            i++; /* move past closing quote */
            break;
        }
    }
    dbuf_puts(out, C->RESET);
    return i;
}

/* Read/print a number token; i is at first char of the number */
static size_t print_number(const char *json, size_t i, DynBuf *out, const Jcolors *C){
    dbuf_puts(out, C->NUMBER);
    /* minimal scan for JSON number charset */
    while(json[i]){
        char c = json[i];
        if(isdigit((unsigned char)c) || c=='+' || c=='-' || c=='.' || c=='e' || c=='E'){
            dbuf_putc(out, c);
            i++;
        }else break;
    }
    dbuf_puts(out, C->RESET);
    return i;
}

/* Read/print true|false|null starting at t/f/n */
static size_t print_literal(const char *json, size_t i, DynBuf *out, const Jcolors *C){
    if(strncmp(&json[i], "true", 4) == 0){
        dbuf_puts(out, C->BOOL); dbuf_append(out, "true", 4); dbuf_puts(out, C->RESET);
        return i+4;
    }else if(strncmp(&json[i], "false", 5) == 0){
        dbuf_puts(out, C->BOOL); dbuf_append(out, "false", 5); dbuf_puts(out, C->RESET);
        return i+5;
    }else if(strncmp(&json[i], "null", 4) == 0){
        dbuf_puts(out, C->NULLC); dbuf_append(out, "null", 4); dbuf_puts(out, C->RESET);
        return i+4;
    }else{
        /* Shouldn't happen for valid JSON; copy char verbatim */
        dbuf_putc(out, json[i]);
        return i+1;
    }
}

/* newline + indent if indent_spaces>0; else single space separator */
static void newline_and_indent(DynBuf *out, int indent_spaces, int level){
    if(indent_spaces > 0){
        dbuf_putc(out, '\n');
        dbuf_indent(out, indent_spaces, level);
    }else{
        dbuf_putc(out, ' ');
    }
}

/* Public API:
   - json_text: input JSON string (assumed valid)
   - indent_spaces: 0 => compact (no newlines); N>0 => N spaces per indent level
   - colors_flag: COLORS (0) => use ANSI; NO_COLORS (1) => plain text
   Returns malloc'ed NUL-terminated string that caller must free().
*/
char *print_json_colored(const char *json_text, int indent_spaces, int colors_flag, Jcolors *Cp)
{
    if(!json_text){
        char *empty = (char*)malloc(1);
        if(!empty){ return NULL; }
        empty[0] = '\0';
        return empty;
    }

    Jcolors C;
    if (!Cp) {
         C = choose_colors(colors_flag);
         Cp=&C;
     }

    Stack ctx; stack_init(&ctx);
    DynBuf out; dbuf_init(&out);

    int level = 0;
    bool at_line_start = true;
    bool expecting_key = false;

    for(size_t i=0; json_text[i] != '\0'; ){
        char c = json_text[i];

        if(c == '"'){
            const char *col = (!stack_empty(&ctx) && stack_top(&ctx)==CTX_OBJECT && expecting_key)
                                ? Cp->KEY : Cp->STRING;
            i = print_string(json_text, i, &out, col, Cp);
            at_line_start = false;
            continue;
        }

        if(c == '{'){
            dbuf_putc(&out, c);
            stack_push(&ctx, CTX_OBJECT);
            expecting_key = true;
            level++;
            newline_and_indent(&out, indent_spaces, level);
            at_line_start = true;
            i++;
            continue;
        }

        if(c == '['){
            dbuf_putc(&out, c);
            stack_push(&ctx, CTX_ARRAY);
            level++;
            newline_and_indent(&out, indent_spaces, level);
            at_line_start = true;
            i++;
            continue;
        }

        if(c == '}'){
            if(level>0) level--;
            if(!at_line_start) newline_and_indent(&out, indent_spaces, level);
            dbuf_putc(&out, c);
            if(!stack_empty(&ctx)) stack_pop(&ctx);
            expecting_key = (!stack_empty(&ctx) && stack_top(&ctx)==CTX_OBJECT) ? false : expecting_key;
            at_line_start = false;
            i++;
            continue;
        }

        if(c == ']'){
            if(level>0) level--;
            if(!at_line_start) newline_and_indent(&out, indent_spaces, level);
            dbuf_putc(&out, c);
            if(!stack_empty(&ctx)) stack_pop(&ctx);
            at_line_start = false;
            i++;
            continue;
        }

        if(c == ','){
            dbuf_putc(&out, c);
            if(!stack_empty(&ctx) && stack_top(&ctx)==CTX_OBJECT){
                expecting_key = true;
            }
            newline_and_indent(&out, indent_spaces, level);
            at_line_start = true;
            i++;
            continue;
        }

        if(c == ':'){
            dbuf_putc(&out, c);
            if(!stack_empty(&ctx) && stack_top(&ctx)==CTX_OBJECT){
                dbuf_putc(&out, ' ');
                expecting_key = false;
            }
            at_line_start = false;
            i++;
            continue;
        }

        if(isspace((unsigned char)c)){
            /* discard original whitespace to control layout ourselves */
            i++;
            continue;
        }

        if(c=='-' || isdigit((unsigned char)c)){
            i = print_number(json_text, i, &out, Cp);
            at_line_start = false;
            continue;
        }

        if(c=='t' || c=='f' || c=='n'){
            i = print_literal(json_text, i, &out, Cp);
            at_line_start = false;
            continue;
        }

        /* Any other punctuation/char (shouldn't appear in valid JSON) */
        dbuf_putc(&out, c);
        at_line_start = false;
        i++;
    }

    dbuf_puts(&out, Cp->RESET);
    stack_free(&ctx);

    return out.buf;
}

/* ---------------------- Demo main (optional) ---------------------- */
/* Accepts: argv[1]=json string, argv[2]=indent (int), argv[3]=colors flag (0/1)
   Example:
     ./json_color_str '{"k": [1,true,null, "s"]}' 2 0
*/
#ifdef TEST
int main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr, "Usage: %s '<json>' [indent] [noColor]\n", argv[0]);
        return 1;
    }
    const char *json = argv[1];
    int indent = (argc>2)? atoi(argv[2]) : 2;
    int colors = COLORS;
    if(argc>3){
        if(strcasecmp("nocolor", argv[3])==0)
            colors = NO_COLORS;
    }

    char *out = print_json_colored(json, indent, colors, NULL);
    if(!out){ perror("print_json_colored"); return 1; }
    puts(out);
    free(out);
    return 0;
}
#endif
