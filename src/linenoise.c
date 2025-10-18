/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

/*
   Lotsa crazy extras, added with "-ajf date" for the most part.
   Additions copyright 2025 Aaron Flin aaron at flin dot org
   and under same license.

   * ctrl-x: enter forced multiline edit mode.
   * ctrl-z: suspend and drop to shell
   * multiline paste using timing
   * changes to completion that made sense for rampart
   * linenoiseState l.buf is now allocated and grows as necessary
   * changes not tested with hints, as rampart doesn't use them
*/

#include <signal.h> //-ajf 2025-10-11
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>
#include "linenoise.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_ADD_OVERHEAD 256  // -ajf - wiggle room for additional edited text
#define LINENOISE_MAX_LINE 4096
#define LINENOISE_INITIAL_LINE 512  // -ajf - initial size of allocated buf
static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

static struct termios orig_termios; /* In order to restore at exit.*/
static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

static int in_ml_paste_or_edit = 0;    // -- ajf - 2025-10-10 - whether in the middle of a multi-lined paste/edit
static int force_ml_edit = 0;          // -- ajf - 2025-10-11 - whether to force multi-line mode
#define REFRESH_REPOSITION  0  // just move cursor based on bufpos_r & bufpos_c and update l->pos
#define REFRESH_FROM_POS    1  // refresh from currrent l->pos to end 
#define REFRESH_LINE        2  // refresh the current line
#define REFRESH_FULL        3  // redraw the entire l->buf within confines of screen

// -- ajf - 2025-10-10 position helper for multiline paste/edit
typedef struct rowcol {
    int promptrow;    // which line of the screen is our prompt on

    // For the buffer we are about to write to screen or update
    int bufpos_r;    // which row our cursor is at relative to prompt line
    int bufpos_c;    // which col our cursor is at relative to prompt line
    int bufdim_r;    // max number of rows current buf
    int bufdim_c;    // max number of cols current buf

    // for our current term
    int screenpos_r; // current location of cursor on screen
    int screenpos_c; // current location of cursor on screen
    int screendim_r; // current screen dimensions
    int screendim_c; // current screen dimensions

    //for the current line at our editing position
    int linestart;    // start of l->pos line in buf
    int eol;          // end of line starting at linestart

    int delta_r;      // for arrow navigating
    int delta_c;      // for arrow navigating
    int savecol;      // col position when going up or down, keep cursor there
    int hshift;       // shift to the left for horizontal overflow

    int refresh_type; // see above REFRESH_*
} rowcol;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t promptlen;
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
    rowcol rc;          // info for multiline editing
};

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	CTRL_X = 24,        /* Ctrl+x  --ajf 2025-10-11 */
	CTRL_Z = 26,        /* Ctrl+z  --ajf 2025-10-11 */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("/tmp/lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->maxrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void) {
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void) {
    maskmode = 0;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    //raw.c_oflag &= ~(OPOST); // -ajf 2024/12/29 -- see https://github.com/antirez/linenoise/issues/128
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };
    int nread, nwritten;
    char c = 0;

    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;

        // ajf - 2025-10-09 - if there is only one, just use it:
        if(lc.len==1)
        {
            char cret = lc.cvec[0][strlen(lc.cvec[0])-1];
            nwritten = snprintf(ls->buf,ls->buflen,"%.*s", (int)strlen(lc.cvec[0])-1, lc.cvec[0]);
            ls->len = ls->pos = nwritten;
            freeCompletions(&lc);
            return cret; /* Return last read character */
        }

        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct linenoiseState saved = *ls;

                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                // ajf - 2025-10-09 - we are inserting the original first in the queue elsewhere
                i=0;
                continue;
                //refreshLine(ls);
            }

            nread = read(ls->ifd,&c,1);
            if (nread <= 0) {
                freeCompletions(&lc);
                return -1;
            }

            switch(c) {
                case 9: /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) linenoiseBeep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break;
                default:
                    /* Update buffer and return */
                    if (i < lc.len) {
                        nwritten = snprintf(ls->buf,ls->buflen,"%s",lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) {
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

// -ajf 2025-10-09
void linenoiseAddCompletionUnshift(linenoiseCompletions *lc, const char *str)
{
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    memmove(&cvec[1], &cvec[0], sizeof(char*) * lc->len);
    lc->cvec = cvec;
    lc->cvec[0] = copy;
    lc->len++;
}

// -ajf 2025-10-09
void linenoiseReplaceCompletion(linenoiseCompletions *lc, const char *str, size_t pos)
{
    size_t len = strlen(str);
    char *copy;

    if(pos >= lc->len)
        pos = lc->len-1;

    copy = malloc(len+1);
    if (copy == NULL) return;

    memcpy(copy,str,len+1);

    free(lc->cvec[pos]);
    lc->cvec[pos] = copy;

}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. */
void refreshShowHints(struct abuf *ab, struct linenoiseState *l, int plen) {
    char seq[64];
    if (hintsCallback && plen+l->len < l->cols) {
        int color = -1, bold = 0;
        char *hint = hintsCallback(l->buf,&color,&bold);
        if (hint) {
            int hintlen = strlen(hint);
            int hintmaxlen = l->cols-(plen+l->len);
            if (hintlen > hintmaxlen) hintlen = hintmaxlen;
            if (bold == 1 && color == -1) color = 37;
            if (color != -1 || bold != 0)
                snprintf(seq,64,"\033[%d;%d;49m",bold,color);
            else
                seq[0] = '\0';
            abAppend(ab,seq,strlen(seq));
            abAppend(ab,hint,hintlen);
            if (color != -1 || bold != 0)
                abAppend(ab,"\033[0m",4);
            /* Call the function to free the hint returned. */
            if (freeHintsCallback) freeHintsCallback(hint);
        }
    }
}

/* Was - Multi line low level line refresh.
 * Now - refreshSingleLine - and old refreshSingleLine removed.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    char seq[64];
    int plen = l->promptlen;
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->maxrows;
    int fd = l->ofd, j;
    struct abuf ab;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    if (old_rows-rpos > 0) {
        lndebug("go down %d", old_rows-rpos);
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
        abAppend(&ab,seq,strlen(seq));
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
        lndebug("clear+up");
        snprintf(seq,64,"\r\x1b[0K\x1b[1A");
        abAppend(&ab,seq,strlen(seq));
    }

    /* Clean the top line. */
    lndebug("clear");
    snprintf(seq,64,"\r\x1b[0K");
    abAppend(&ab,seq,strlen(seq));

    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt, l->promptlen);
    if (maskmode == 1) {
        unsigned int i;
        for (i = 0; i < l->len; i++) abAppend(&ab,"*",1);
    } else {
        abAppend(&ab,l->buf,l->len);
    }

    /* Show hits if any. */
    refreshShowHints(&ab,l,plen);

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->len &&
        (l->pos+plen) % l->cols == 0)
    {
        lndebug("<newline>");
        abAppend(&ab,"\n",1);
        snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (plen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
    lndebug("rpos2 %d", rpos2);

    /* Go up till we reach the expected positon. */
    if (rows-rpos2 > 0) {
        lndebug("go-up %d", rows-rpos2);
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);
        abAppend(&ab,seq,strlen(seq));
    }

    /* Set column. */
    col = (plen+(int)l->pos) % (int)l->cols;
    lndebug("set col %d", 1+col);
    if (col)
        snprintf(seq,64,"\r\x1b[%dC", col);
    else
        snprintf(seq,64,"\r");
    abAppend(&ab,seq,strlen(seq));

    lndebug("\n");
    l->oldpos = l->pos;

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

// MULTILINE: -ajf
#define WCODE(s,len) (void)write(l->ofd, "\033[" s, len+2)
#define WCODEF(maxlen,fmt, ...) do{\
    char b[maxlen];\
    snprintf(b, maxlen, "\033[" fmt, __VA_ARGS__);\
    (void)write(l->ofd, b, strlen(b));\
}while(0)

//#define DEBUGMSGS 1
#ifdef DEBUGMSGS
static int dbcnt=0;
#define DEBUGF(r,c,...) do {\
    write(l->ofd, "\0337", 2); \
    printf("\033[%d;%dH\033[0K\033[31m#%d ", (r), (c), dbcnt++ );\
    printf(__VA_ARGS__);\
    printf("\033[0m"); \
    fflush(stdout);\
    write(l->ofd, "\0338", 2);\
} while(0)
#else
#define DEBUGF(r,c,...) /* nada */
#endif

// set position in l->buf based on bufpos_r and bufpos_c
static void set_pos(struct linenoiseState *l)
{
    int row = l->rc.bufpos_r, col= l->rc.bufpos_c;
    int currow=0, curcol=0, i=0;
    for(; i<l->len; i++)
    {
        if(currow==row && curcol==col)
        {
            l->pos = i;
            return;
        }
        if(l->buf[i]=='\n')
        {
            if(currow==row)
            {
                l->pos = i;
                l->rc.bufpos_c=curcol;
                return;
            }
            curcol=0;
            currow++;
        }
        else
            curcol++;
    }
    if (currow == row)
    {
        l->pos = i;
        l->rc.bufpos_c=curcol;
    }
}

// counting from the top left, set col/row from l->pos
static void get_bufpos(struct linenoiseState *l)
{
    size_t i=0;
    rowcol *rcp = &(l->rc);
    int lastn=-1, linelen=0;
    rcp->linestart=-1;
    rcp->eol=0;
    rcp->bufdim_c = 0;
    rcp->bufdim_r = 0;
    rcp->bufpos_c = 0;
    rcp->bufpos_r = 0;

    if(!l->buf[0])
        return;

    rcp->eol=-1;
    for(; i<l->len; i++)
    {
        if(l->buf[i]=='\n')
        {
            // if we are past curpos, this is our line
            if( rcp->linestart==-1 && i >= l->pos)
            {
                rcp->eol=(int)i;
                if(!rcp->bufdim_r)
                    rcp->linestart=0;
                else
                    rcp->linestart=lastn+1;
                rcp->bufpos_r=rcp->bufdim_r;
            }
            linelen = i-lastn-1;
            if(linelen > rcp->bufdim_c)
                rcp->bufdim_c = linelen;  

            lastn=i;
            rcp->bufdim_r++;
        }
    }

    // either no '\n' in l->buf (lastn==0),
    // or no '\n' past l->pos (lastn == linestart-1)
    if(rcp->eol==-1)
    {
        rcp->eol=(int)l->len;
        rcp->linestart = lastn  + 1;
        rcp->bufpos_r=rcp->bufdim_r;  //at the last line
    }
    rcp->bufpos_c = l->pos - rcp->linestart;
    //DEBUGF(25,15,"eol=%d, start=%d, pos=%d, lastn=%d %dx%d\n", rcp->eol, rcp->linestart, l->pos, lastn, rcp->bufpos_r, rcp->bufpos_c);
}

// Helper to parse response from ESC [ row ; col R
static int read_cpr(struct linenoiseState *l, int *row, int *col) {
    char buf[64];
    size_t len = 0;

    while (len < sizeof(buf) - 1) {
        ssize_t n = read(l->ifd, buf + len, 1);
        if (n <= 0) break;
        if (buf[len] == 'R') { len++; break; }
        len += (size_t)n;
    }
    buf[len] = '\0';

    // Expected format: ESC [ rows ; cols R
    char *esc = memchr(buf, '\x1b', len);
    if (!esc || esc[1] != '[') return -1;

    int r = 0, c = 0;
    char *p = esc + 2;
    while (*p && isdigit((unsigned char)*p)) { r = r * 10 + (*p++ - '0'); }
    if (*p++ != ';') return -1;
    while (*p && isdigit((unsigned char)*p)) { c = c * 10 + (*p++ - '0'); }
    if (*p != 'R') return -1;

    if (row) *row = r;
    if (col) *col = c;
    return 0;
}

static void get_screenpos(struct linenoiseState *l)
{
    int r = 0, c = 0;
    int tr = 0, tc = 0;
    rowcol *rcp = &(l->rc);

    rcp->screenpos_r = 1;
    rcp->screenpos_c = 1;
    rcp->screendim_r = 24;
    rcp->screendim_c = 80;

    // Query current position
    WCODE("6n", 2);
    if (read_cpr(l, &r, &c) == 0)
    {
        rcp->screenpos_r = r;
        rcp->screenpos_c = c;
    }

    // Move to bottom-right, ask again
    WCODE("9999;9999H", 10);
    WCODE("6n", 2);
    if (read_cpr(l, &tr, &tc) == 0)
    {
        rcp->screendim_r = tr;
        rcp->screendim_c = tc;
    }

    // Restore original cursor position
    if (r && c)
        WCODEF(16, "%d;%dH", r, c);
}

static inline void writeprompt(struct linenoiseState *l)
{
    if(force_ml_edit)
        WCODE("35m", 3);

    (void)write(l->ofd, l->prompt, l->promptlen);

    if(force_ml_edit)
        WCODE("0m", 2);
}

static void update_promptrow(struct linenoiseState *l, int rows_added)
{
    rowcol *rcp = &(l->rc);
    int rows_below = rcp->screendim_r - rcp->promptrow;
    int pushed_up = rows_added - rows_below;

    if(pushed_up > 0)
        rcp->promptrow -= pushed_up;
}

// write line within constraints of screen, return beginning of next line, or NULL
static char *write_line(struct linenoiseState *l, char *startpos, size_t width, size_t col, int writenl)
{
    char *e=strchr(startpos, '\n');

    startpos += l->rc.hshift;

    //safety check:
    if(startpos < l->buf){printf("\0331;1H\033[0JStartpos (%p) before buffer (%p)\n",startpos,l->buf);abort();}

    if(!e)
    {
        size_t len = strlen(startpos);
        if(len > width-col)
            len = width-col;
        if(len)
            (void)write( l->ofd, startpos, len);
        return NULL;
    }
    else
    {
        int len = e-startpos;
        if(len>0)
        {
            if(len > width-col)
                len = width-col;
            (void)write( l->ofd, startpos, (size_t)len);
        }
        if(writenl)
            (void)write( l->ofd, "\n", 1);    
    }   
    return e+1;
}
// scroll one line up or down, if necessary
static void doscroll(struct linenoiseState *l, int up)
{
    rowcol *rcp = &(l->rc);

    if(up && rcp->screenpos_r==1)
    {
        get_bufpos(l);
        WCODE("H",1);
        WCODE("L",1);
        if(!l->rc.bufpos_r)
        {
            writeprompt(l);
            rcp->screenpos_c += l->promptlen;
        }
        write(l->ofd, l->buf + rcp->linestart, rcp->eol - rcp->linestart);
        WCODEF(16, "%d;%dH", rcp->screenpos_r, rcp->screenpos_c);
        rcp->promptrow++;
    }
    else if(!up && rcp->screenpos_r == rcp->screendim_r)
    {
        get_bufpos(l);
        WCODE("9999;9999H\n", 11);  //newline at bottom
        write_line(l, l->buf + rcp->linestart, l->rc.screendim_c, 0, 0);
        WCODEF(16, "%d;%dH", rcp->screenpos_r, rcp->screenpos_c);
        rcp->promptrow--;
    }
}

#define bufcolpos (rcp->bufpos_c - rcp->hshift)

static void place_cursor(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int row = rcp->bufpos_r, col = bufcolpos;

    if(!row)
    {
        int onscreen_promptlen = l->promptlen - rcp->hshift;
        if(onscreen_promptlen > 0)
            col += onscreen_promptlen;
        else
            col += l->promptlen;
    }
    row += l->rc.promptrow;

    col++; //0 to 1 based indexing

    if(row < 1) // 1 is top of screen
    {
        while(row++<1)
            doscroll(l,1);
        l->rc.promptrow=1;
    }

    WCODEF(32, "%d;%dH", row, col);
}

static void write_to_end(struct linenoiseState *l, int full)
{
    rowcol *rcp = &(l->rc);
    int nlines = full ? -1                                  // write to end of buf
                : 1 + rcp->screendim_r - rcp->screenpos_r;  // write to bottom of screen
    char *b = l->buf;
    // starting position
    size_t curcol= full? l->promptlen : (size_t)rcp->screenpos_c-1, 
           screenwidth=(size_t)rcp->screendim_c;

    if(full)     // do the whole buf, don't stop at end of screen
    {   // this mess is to account for prompt length when horizontally wrapping
        int visible = l->promptlen - rcp->hshift;
        // write visible portion of prompt
        if(visible > 0)
        {
            WCODE("1G",2);
            if(force_ml_edit)
                WCODE("35m", 3);
            (void)write(l->ofd, l->prompt + rcp->hshift, visible);
            if(force_ml_edit)
                WCODE("0m", 2);
            b -= rcp->hshift; // see note ↓
            curcol=visible;
        }
        else
        {
            curcol=0;
            b-=l->promptlen;  // Not my finest. This only looks dangerous.  There's a check in write_line to prove the math is good.
        }
    }
    else         // start a pos
        b+=l->pos;

    while(nlines) {
        nlines--;
        b = write_line(l, b, screenwidth, curcol, nlines);
        curcol=0; 
        if(!b)
            break;
    }
}

static void refresh_window(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    size_t oldpos = l->pos;
    get_bufpos(l);
    if(rcp->promptrow > 0)
    {   // just go to row and rewrite from there
        WCODEF(16, "%d;1H", rcp->promptrow);
        WCODE("0J", 2);
        l->pos=0;
        write_to_end(l, 1);
        place_cursor(l);
    }
    else
    {
        int oldrow = rcp->bufpos_r, oldcol = rcp->bufpos_c;
        int oldscreenpos_r = rcp->screenpos_r, oldscreenpos_c = rcp->screenpos_c;
        rcp->bufpos_r = -rcp->promptrow + 1; // bufrow at top of screen - set from one based prompt row 
        rcp->bufpos_c = 0;
        set_pos(l);  //set l->pos from new bufpos

        WCODE("1;1H", 4);
        WCODE("0J", 2);

        rcp->screenpos_r=1;
        rcp->screenpos_c=1;
        write_to_end(l,0);
        rcp->screenpos_r = oldscreenpos_r;
        rcp->screenpos_c = oldscreenpos_c;

        rcp->bufpos_r = oldrow;
        rcp->bufpos_c = oldcol;
        place_cursor(l);
    }
    l->pos = oldpos;        
}

static int check_scroll(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int promptadd = rcp->bufpos_r==0 ? l->promptlen:0;

    if(bufcolpos + promptadd >= rcp->screendim_c)
    {
        DEBUGF(22, 5, "OVER LIMIT, hshift before %d, after %d", rcp->hshift, 8 + (rcp->bufpos_c - rcp->screendim_c));
        rcp->hshift = 8 + ( (rcp->bufpos_c+promptadd) - rcp->screendim_c);
        refresh_window(l);
        place_cursor(l);
        return 1;
    }

    if (bufcolpos < 0)
    {
        DEBUGF(22, 5, "UNDER LIMIT, hshift before %d, after %d", rcp->hshift, rcp->hshift-8>0?rcp->hshift-8:0);
        rcp->hshift = -8 + (rcp->bufpos_c - rcp->screendim_c);
        if(rcp->hshift < 0)
            rcp->hshift = 0;
        refresh_window(l);
        place_cursor(l);
        return 1;
    }
    return 0;
}

static void refresh_from_pos(struct linenoiseState *l)
{
    get_bufpos(l);
    place_cursor(l);
    get_screenpos(l);
    WCODE("0J",2); //erase from cursor to end of screen
    write_to_end(l,0);
    place_cursor(l);
}

static void refresh_whole_line(struct linenoiseState *l)
{
    //rowcol *rcp = &(l->rc);
    get_bufpos(l);
    WCODE("1G", 2); // 0 col
    WCODE("0K", 2); // clear

    if(!l->rc.bufpos_r)
        writeprompt(l);

    write_line(l, l->buf + l->rc.linestart, l->rc.screendim_c, 0, 0);

    place_cursor(l);
}

static void refresh_full(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);


    if( rcp->promptrow>0)
        WCODEF(16, "%d;1H", rcp->promptrow); // go to prompt line
    else
        WCODE("H", 1); //go to top of screen

    WCODE("0J", 2); // clear to end of screen

    get_bufpos(l);

    // did we scroll horiz?
    if(!check_scroll(l)) //check scroll will refresh, so don't do it again
    {
        // write prompt and buf
        writeprompt(l);
        //(void)write(l->ofd, l->buf, l->len);
        write_to_end(l,1);
    }

    // did we scroll down?
    if(rcp->bufdim_r + rcp->promptrow > rcp->screendim_r)
        rcp->promptrow = rcp->screendim_r - rcp->bufdim_r;
    place_cursor(l);
}

static int getlinelen(struct linenoiseState *l, int next)
{
    int len = 0;
    char *b = l->buf, *s = &b[l->pos], *e=&b[l->len];
    if(*s == '\n')
    {
        if(next>0) // if at \n and next, read next line
        {
            s++;
            while(s<e && *s !='\n')
                s++,len++;
            return len;
        }
        // else read previous line        
        s--;
        while(s>=b && *s !='\n')
            s--,len++;
        return len;
    }

    if(next<0) // length of previous line when not at \n -- not used right now
    {
        while(s>=b && *s !='\n')
            s--;
        s--;
        while(s>=b && *s !='\n')
            s--,len++;
        return len;
    }

    // lenth of current line if not at \n
    while(s<e && *s !='\n') // chars ahead
        s++,len++;

    s=&b[l->pos]; //reset
    len--;

    while(s>=b && *s !='\n') //chars behind
        s--,len++;

    return len;
}

static void refresh_reposition(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int dc = rcp->delta_c, dr = rcp->delta_r;
    int linelen=-1;

    char c=l->buf[l->pos];
    char prevc = l->pos ? l->buf[l->pos+-1] : '\0';

    //quick paths:
    if(!dr) //note that pos has already been updated in EditMoveLeft/Right below
    {
        int basepos = rcp->bufpos_r?0:l->promptlen;
        // left
        if(dc == -1)
        {
            if(rcp->savecol)
            {
                linelen = getlinelen(l,1);//get next line if '\n'

                if(rcp->savecol>linelen)
                {
                    DEBUGF(17, 5, "DO JUMP BACK savecol = %d >? linelen = %d", rcp->savecol, linelen);
                    // don't actually change pos, just go to end of line
                    l->pos++; //undo the -- in EditMoveLeft
                    rcp->bufpos_c = linelen;
                    WCODEF(16, "%dG", basepos + linelen+1);//go to col
                    rcp->savecol=0;
                    goto hshift;
                    //return;
                }
            }

            linelen = getlinelen(l,0);

            if(c=='\n')
            {
                rcp->bufpos_c=linelen+1;
                rcp->bufpos_r--;
                basepos = rcp->bufpos_r?0:l->promptlen;
                WCODEF(16, "%dG", basepos + linelen+1);//go to col
                WCODE("1A",2);//one up
                doscroll(l,1);
            }
            else
            {
                rcp->bufpos_c--;
                //linelen = getlinelen(l,0);
                WCODE("1D",2); // one left
            }
        }
        // right
        else if(dc == 1)
        {
            if(prevc == '\n')
            {
                if(rcp->hshift)
                {
                    rcp->hshift = 0;
                    //refresh_window(l);
                }
                else
                {
                    WCODE("1B",2);
                    rcp->bufpos_r++;
                    rcp->bufpos_c=0;
                    WCODE("1G",2);
                }
                doscroll(l,0);
            }
            else
            {
                WCODE("1C",2);
                rcp->bufpos_c++;
            }
        }
        rcp->savecol=0;
        hshift:
        return;
    }

    if(!dc && (dr==-1||dr==1) )
    {
        // do not allow scrolling up into the prompt
        if(rcp->bufpos_r==1 && dr==-1 && rcp->screenpos_c + rcp->hshift < l->promptlen+1)
            return;

        if(rcp->savecol) //keep in same col until something other than up/down is pressed
            rcp->bufpos_c=rcp->savecol;

        // adjust for length of prompt when moving off prompt line
        if(dr==1 && rcp->bufpos_r==0)
            rcp->bufpos_c+= l->promptlen;

        rcp->bufpos_r += dr;

        // adjust for length of prompt when moving onto the prompt line
        if(!rcp->bufpos_r)
            rcp->bufpos_c -= l->promptlen;

        if(rcp->bufpos_c < 0)
        {
            if(rcp->bufpos_r)
            {
                rcp->bufpos_c=999; //will be set to end of line in set_pos()
                rcp->bufpos_r--;
            }
            else
                rcp->bufpos_c=0;

        }
        else if (dc>0 && rcp->bufpos_c > linelen+1)
        {
            rcp->bufpos_c=0;
            rcp->bufpos_r++;
        }

        rcp->savecol=rcp->bufpos_c;

        set_pos(l);

        if(dr==-1) // up
        {
            doscroll(l,1);
            WCODE("1A",2);
        }
        else       // down
        {
            doscroll(l,0);
            WCODE("1B",2);
        }
    }
    // if we ever need something other than moving one col or row - do that here:
}

static void refreshMultiLine(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    get_screenpos(l);

    DEBUGF(10, 15, "BEFORE - type: %d pos: %lu prow: %d scrdim: %dx%d, scrpos: %dx%d  bufpos: %dx%d bufdim: %dx%d linelen=%d '%c' ",
         rcp->refresh_type, l->pos, rcp->promptrow,
         rcp->screendim_r, rcp->screendim_c, rcp->screenpos_r, rcp->screenpos_c,
         rcp->bufpos_r,    rcp->bufpos_c,    rcp->bufdim_r,    rcp->bufdim_c,    
         rcp->eol-rcp->linestart,      l->buf[l->pos] =='\n'?'*':l->buf[l->pos]   );

    switch(rcp->refresh_type)
    {
        case REFRESH_REPOSITION:
            refresh_reposition(l);
            break;
        case REFRESH_FROM_POS:
            refresh_from_pos(l);
            break;
        case REFRESH_LINE:
            refresh_whole_line(l);
            break;
        case REFRESH_FULL:
            refresh_full(l);
            break;
    }

    if(rcp->refresh_type != REFRESH_FULL) //just rewrote screen, no need to check
        check_scroll(l);

    // not used here: l->oldpos = l->pos;
    rcp->delta_c=0;
    rcp->delta_r=0;

    DEBUGF(15, 15, "AFTER - type: %d pos: %lu prow: %d scrdim: %dx%d, scrpos: %dx%d  bufpos: %dx%d bufdim: %dx%d linelen=%d '%c' ",
         rcp->refresh_type, l->pos, rcp->promptrow,
         rcp->screendim_r, rcp->screendim_c, rcp->screenpos_r, rcp->screenpos_c,
         rcp->bufpos_r,    rcp->bufpos_c,    rcp->bufdim_r,    rcp->bufdim_c,    
         rcp->eol-rcp->linestart,      l->buf[l->pos] =='\n'?'*':l->buf[l->pos]   );
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the current mode. */
static void refreshLine(struct linenoiseState *l) {
    if(in_ml_paste_or_edit || force_ml_edit)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c) {
    if(l->len >= l->buflen) //-ajf
    {
        size_t len = l->buflen + LINENOISE_ADD_OVERHEAD;
        char *nb=realloc(l->buf, len);
        if(!nb)
            return 0;

        l->buflen=len-1;
        l->buf=nb;
        l->buf[l->buflen]='\0';
    }
    if (l->len < l->buflen) {

        l->rc.refresh_type = REFRESH_LINE;
        if(c=='\n')
        {
            rowcol *rcp = &(l->rc);        
            update_promptrow(l, 1);
            WCODE("0K", 2);       // clear line
            l->rc.bufpos_r++;     // update row number
            l->rc.bufpos_c=0;
            rcp->refresh_type = REFRESH_FULL;
            rcp->hshift=0;
        }

        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            /* Not using in rampart, figure this out later
            if ((!mlmode && l->plen+l->len < l->cols && !hintsCallback)) {
                // Avoid a full update of the line in the * trivial case.
                char d = (maskmode==1) ? '*' : c;
                if (write(l->ofd,&d,1) == -1) return -1;
            } else {
            */
                refreshLine(l);
            //}
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        l->rc.delta_c=-1;
        l->rc.refresh_type = REFRESH_REPOSITION;
        refreshLine(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        l->rc.delta_c=1;
        l->rc.refresh_type = REFRESH_REPOSITION;
        refreshLine(l);
    }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        l->rc.bufpos_r = 0;
        l->rc.bufpos_c = 0;
        if(l->rc.promptrow<1)
        {
            l->rc.promptrow=1;
            WCODE("1;1H", 4);
            writeprompt(l);
        }
        l->rc.refresh_type = REFRESH_FROM_POS;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        l->rc.refresh_type = REFRESH_FULL;
        refreshLine(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return;
        }
        char *h = history[history_len - 1 - l->history_index];
        size_t hlen = strlen(h);
        if(hlen>= l->buflen) // -ajf
        {
            size_t llen= hlen + LINENOISE_ADD_OVERHEAD;
            char *nb = realloc(l->buf, llen);
            if(nb)
            {
                l->buf=nb;
                l->buflen = llen-1;
                l->buf[l->buflen]='\0';
            }
        }
        strncpy(l->buf, h, l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);

        char *s = l->buf;
        char *e = l->buf + l->pos;
        int nl=0;
        while (s < e) //check for newlines, if so, enter multiline
        {
            if(*s=='\n')
            {
                nl++;
                break;
            }
            s++;
        }
        if(nl || in_ml_paste_or_edit)
        {
            l->rc.refresh_type = REFRESH_FULL;
            in_ml_paste_or_edit=1;
        }
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        l->rc.refresh_type = REFRESH_LINE;
        if( l->buf[l->pos] == '\n' )
        {
            l->rc.bufdim_r -= 1;
            l->rc.refresh_type = REFRESH_FROM_POS;
        }
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        rowcol *rcp = &(l->rc);
        char c = l->buf[l->pos-1]; //the char being deleted
        rcp->refresh_type = REFRESH_LINE;
        memmove(l->buf+l->pos-1, l->buf+l->pos, l->len-l->pos);
        l->pos--;
        l->len--;
        rcp->bufpos_c--;
        l->buf[l->len] = '\0';
        if( c == '\n')
        {
            get_bufpos(l);
            l->rc.refresh_type = REFRESH_FROM_POS;
        }
/*
        if(bufcolpos > rcp->screendim_c || bufcolpos < 0)
        {
            rcp->delta_c=-1;
            l->rc.refresh_type = REFRESH_REPOSITION; //same path as EditMoveLeft, force refresh_window to scroll horiz
        }
*/
        refreshLine(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    l->rc.refresh_type = REFRESH_FROM_POS;
    refreshLine(l);
}

//added so we can manually restore state -- ajf
struct linenoiseState *linenoise_lnstate;

void linenoise_refresh() {
    if(linenoise_lnstate)
        refreshLine(linenoise_lnstate);
}

/* suspend on ctrl-z - ajf 2025-10-11 */
static struct termios saved_tio;
static int have_saved = 0;

static void on_sigcont(int sig) {
    (void)sig;
    if (have_saved) {
        struct termios raw = saved_tio;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    // optionally: redraw UI here
}

static void suspend_self(void) {
    tcdrain(STDOUT_FILENO);

    // ensure default action for SIGTSTP and it isn't blocked
    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);

    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &m, NULL);

    raise(SIGTSTP);   // stops here

    struct sigaction sc = {0};
    sc.sa_handler = on_sigcont;
    sigemptyset(&sc.sa_mask);
    sigaction(SIGCONT, &sc, NULL);

    on_sigcont(SIGCONT);
}

// -ajf
static int polpaste(struct linenoiseState *l, char pc)
{
    unsigned char nextc;
    ssize_t nread = read(l->ifd, &nextc, 1);
    if (nread != 1)
        return 0;

    // if filled, loop will resume and write more
    unsigned char buf[16384];
    int lines_written=0;

    buf[0]=pc;
    buf[1]=nextc;
    size_t len = 2;

    /* Paste-drain: read all remaining bytes arriving within ~5ms gaps */

    for (;;) {
        struct pollfd p = { .fd = l->ifd, .events = POLLIN, .revents = 0 };
        int r = poll(&p, 1, 5);                 /* wait <= 5ms for next chunk */
        if (r <= 0 || !(p.revents & POLLIN))    /* nothing new -> done */
            break;

        ssize_t n = read(l->ifd, buf + len, sizeof(buf) - len);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len == sizeof(buf))
            break;          /* cap safety */
    }

    /* Normalize CRLF → LF */
    size_t w = 0;
    for (size_t i = 0; i < len; )
    {
        if (buf[i] == '\r')
        {
            if (i + 1 < len && buf[i + 1] == '\n')
            {
                buf[w++] = '\n';
                i += 2;
            }
            else // dangerous if on a boundry.  might get two \n
            {
                buf[w++] = '\n';
                i++;
            }
        } 
        else 
            buf[w++] = buf[i++];
        if( buf[w-1]=='\n')
            lines_written++;
    }
    len = w;

    /* --- Insert into current buffer --- */
    if (len > 0)
    {
        if ((int)(l->len + len + 1) >= l->buflen)
        {
            int newlen = l->len + (int)len + 1 + LINENOISE_ADD_OVERHEAD;
            char *newbuf = realloc(l->buf, newlen);
            if (!newbuf) {
                /* not gonna happen, but if allocation failed — fall back to truncating */
                len = (size_t)(l->buflen - l->len - 1);
            } else {
                l->buf = newbuf;
                l->buflen = newlen;
            }
        }

        memmove(l->buf + l->pos + len,
                l->buf + l->pos,
                (size_t)(l->len - l->pos));
        memcpy(l->buf + l->pos, buf, len);
        l->pos += (int)len;
        l->len += (int)len;
        l->buf[l->len] = '\0';

        if(write(l->ofd, buf, len)==-1)
            return 0;
        update_promptrow(l, lines_written);
        get_bufpos(l);
        l->rc.refresh_type = REFRESH_FULL;
        refreshLine(l);
    }
    // in_ml_paste_or_edit is set until we get a real ENTER
    return 1;
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static char * linenoiseEdit(int stdin_fd, int stdout_fd, size_t buflen, const char *prompt)
{
    struct linenoiseState l;
    rowcol *rcp;
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buflen = buflen;
    l.prompt = prompt;
    l.promptlen = strlen(prompt);
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.maxrows = 0;
    l.history_index = 0;
    memset(&(l.rc), 0, sizeof(rowcol));  
    rcp = &(l.rc);
    l.buf = malloc(buflen);
    if(!l.buf)
        return NULL;

    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */
    get_screenpos(&l);  //initial screen size and cursor pos
    l.cols = l.rc.screendim_c;
    rcp->promptrow = rcp->screenpos_r;  // our cursor is on the prompt row at this point

    linenoise_lnstate=&l; //-- ajf

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

    if (write(l.ofd,prompt,l.plen) == -1) goto end_fail;
    if (write(l.ofd, "\033[0J", 5) == -1) goto end_fail;
    while(1) {
        char c;
        int nread;
        char seq[3];

        nread = read(l.ifd,&c,1);
        if (nread <= 0) return l.buf;

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        // -- ajf - 2025-10-10 don't try completion if in a multiline paste
        if (!in_ml_paste_or_edit && c == TAB && completionCallback != NULL) {
            c = completeLine(&l);
            /* Return on errors */
            if (c < 0) return l.buf;
            /* Read next character when 0 */
            if (c == 0) continue;
        }
        switch(c) {
        // -- ajf - 2025-10-10 rewrite to handle multiline paste/edit
        case ENTER:    /* enter  or \n pasted */
        {
            if(!force_ml_edit)
            {
                /* Look ahead briefly: if another byte arrives within 5ms, we assume paste */
                struct pollfd p = { .fd = l.ifd, .events = POLLIN, .revents = 0 };
                int ready = poll(&p, 1, 5);

                if (ready == 1 && (p.revents & POLLIN))
                {
                    if(polpaste(&l, '\n'))
                    {
                        in_ml_paste_or_edit=1;
                        break;
                    }
                    
                }
            }
            // --ajf 2025-10-11 allow newlines if not positioned at the end, or if forced
            if(force_ml_edit || (in_ml_paste_or_edit && l.len != l.pos))
            {
                linenoiseEditInsert(&l,'\n');
                break;
            }
            // poll reports no data waiting
            in_ml_paste_or_edit=0;

            history_len--;
            free(history[history_len]);
            linenoiseEditMoveEnd(&l);
            if (hintsCallback) {
                /* Force a refresh without hints to leave the previous
                 * line as the user typed it after a newline. */
                linenoiseHintsCallback *hc = hintsCallback;
                hintsCallback = NULL;
                refreshLine(&l);
                hintsCallback = hc;
            }

            return l.buf;
        }
        case CTRL_C:     /* ctrl-c */
            /* AJF 2023-07-08 -- added printf and ENODATA if line is empty and ctrl-c pressed */
//freebsd
#ifndef ENODATA
#define ENODATA 8088
#endif
            in_ml_paste_or_edit=0;
            force_ml_edit = 0;
            // -ajf 2025-10-10 - changed to write
            (void)write(l.ofd, "^C", 2);
            if(l.len)
                errno = EAGAIN;
            else
                errno = ENODATA;
            goto end_fail;
        case BACKSPACE:   /* backspace */
        case 8:     /* ctrl-h */
            linenoiseEditBackspace(&l);
            break;
        case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                            line is empty, act as end-of-file. */
            if(in_ml_paste_or_edit|force_ml_edit) {
                history_len--;
                free(history[history_len]);
                linenoiseEditMoveEnd(&l);
                force_ml_edit = 0;
                if (hintsCallback) {
                    /* Force a refresh without hints to leave the previous
                     * line as the user typed it after a newline. */
                    linenoiseHintsCallback *hc = hintsCallback;
                    hintsCallback = NULL;
                    refreshLine(&l);
                    hintsCallback = hc;
                }
                in_ml_paste_or_edit=0;

                return l.buf;

            } else if (l.len > 0) {
                linenoiseEditDelete(&l);
            } else {
                history_len--;
                free(history[history_len]);
                goto end_fail;
            }
            break;
        case CTRL_T:    /* ctrl-t, swaps current character with previous. */
            if (l.pos > 0 && l.pos < l.len) {
                int aux = l.buf[l.pos-1];
                l.buf[l.pos-1] = l.buf[l.pos];
                l.buf[l.pos] = aux;
                if (l.pos != l.len-1) l.pos++;
                refreshLine(&l);
            }
            break;
        case CTRL_B:     /* ctrl-b */
            linenoiseEditMoveLeft(&l);
            break;
        case CTRL_F:     /* ctrl-f */
            linenoiseEditMoveRight(&l);
            break;
        case CTRL_P:    /* ctrl-p */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        // -ajf 2025-10-11
        case CTRL_X:
        {
            //toggle
            l.rc.refresh_type=REFRESH_FULL;
            if(force_ml_edit)
            {
                force_ml_edit = 0;
                // keep refresh using multiline version
                int old = in_ml_paste_or_edit;
                in_ml_paste_or_edit=1;
                refreshLine(&l);
                in_ml_paste_or_edit=old;
            }
            else
            {
                force_ml_edit=1;
                refreshLine(&l);
            }
            break;
        }
        // -ajf 2025-10-11
        case CTRL_Z:
            suspend_self();   // returns after `fg`
            enableRawMode(l.ofd);
            l.rc.refresh_type=REFRESH_FULL;
            int old = in_ml_paste_or_edit;
            in_ml_paste_or_edit=1;
            refreshLine(&l);
            in_ml_paste_or_edit=old;
            break;
        case CTRL_N:    /* ctrl-n */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
        case ESC:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence.
             * Use two calls to handle slow terminals returning the two
             * chars at different times. */
            if (read(l.ifd,seq,1) == -1) break;
            if (read(l.ifd,seq+1,1) == -1) break;

            /* ESC [ sequences. */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte. */
                    if (read(l.ifd,seq+2,1) == -1) break;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        case '3': /* Delete key. */
                            linenoiseEditDelete(&l);
                            break;
                        }
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': /* Up */
                        if( force_ml_edit || (in_ml_paste_or_edit && l.len != l.pos) )
                        {
                            if(l.rc.bufpos_r)
                            {
                                l.rc.refresh_type = REFRESH_REPOSITION;
                                l.rc.delta_r=-1;
                                refreshLine(&l);
                            }
                            break;
                        }
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                        break;
                    case 'B': /* Down */
                        if( force_ml_edit || (in_ml_paste_or_edit && l.len != l.pos) )
                        {
                            if(l.rc.bufpos_r < l.rc.bufdim_r)
                            {
                                l.rc.refresh_type = REFRESH_REPOSITION;
                                l.rc.delta_r=1;
                                refreshLine(&l);
                            }
                            break;
                        }
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                        break;
                    case 'C': /* Right */
                        linenoiseEditMoveRight(&l);
                        break;
                    case 'D': /* Left */
                        linenoiseEditMoveLeft(&l);
                        break;
                    case 'H': /* Home */
                        linenoiseEditMoveHome(&l);
                        break;
                    case 'F': /* End*/
                        linenoiseEditMoveEnd(&l);
                        break;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': /* Home */
                    linenoiseEditMoveHome(&l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(&l);
                    break;
                }
            }
            break;
        default:
            if(force_ml_edit){
                struct pollfd p = { .fd = l.ifd, .events = POLLIN, .revents = 0 };
                int ready = poll(&p, 1, 5);

                if (ready == 1 && (p.revents & POLLIN))
                {
                    if(polpaste(&l, c))
                    {
                        break;
                    }
                }
            }
            if (linenoiseEditInsert(&l,c)) goto end_fail;
            break;
        case CTRL_U: /* Ctrl+u, delete the whole line. */
            l.buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;
        case CTRL_K: /* Ctrl+k, delete from current to end of line. */
            l.buf[l.pos] = '\0';
            l.len = l.pos;
            refreshLine(&l);
            break;
        case CTRL_A: /* Ctrl+a, go to the start of the line */
            linenoiseEditMoveHome(&l);
            break;
        case CTRL_E: /* ctrl+e, go to the end of the line */
            linenoiseEditMoveEnd(&l);
            break;
        case CTRL_L: /* ctrl+l, clear screen */
            if(in_ml_paste_or_edit)
            {
                refresh_window(&l);
                break;
            }
            linenoiseClearScreen();
            l.rc.promptrow=1;
            refreshLine(&l);
            break;
        case CTRL_W: /* ctrl+w, delete previous word */
            linenoiseEditDeletePrevWord(&l);
            break;
        }
    }

    return l.buf;

    end_fail:

    if(l.buf)
        free(l.buf);
    return NULL;
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static char * linenoiseRaw(size_t buflen, const char *prompt) {
    char * ret = NULL; // -ajf

    if (buflen == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (enableRawMode(STDIN_FILENO) == -1) return NULL;
    ret = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buflen, prompt);
    linenoise_lnstate=NULL; //-ajf
    disableRawMode(STDIN_FILENO);
    printf("\n");
    return ret;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoiseNoTTY(void) {
    char *line = NULL;
    size_t len = 0, maxlen = 0;

    while(1) {
        if (len == maxlen) {
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL) {
                if (oldval) free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) {
                free(line);
                return NULL;
            } else {
                line[len] = '\0';
                return line;
            }
        } else {
            line[len] = c;
            len++;
        }
    }
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt) {

    if (!isatty(STDIN_FILENO)) {
        /* Not a tty: read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoiseNoTTY();
    } else if (isUnsupportedTerm()) {
        char buf[LINENOISE_MAX_LINE];
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else {
        // - ajf - now returns malloc'd buffer or NULL
        return linenoiseRaw(LINENOISE_INITIAL_LINE, prompt);
    }
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoiseFree(void *ptr) {
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(void) {
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
    disableRawMode(STDIN_FILENO);
    freeHistory();
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. 

   MODIFIED to take ownership of an already malloc'd string.
   The API call linenoiseHistoryAdd() is now below   -ajf            */

static int lhAdd_to(char *line, int take_ownership) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    /* Initialization on first call. */
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len-1], line))
    {
        if(take_ownership)
            free(line);
        return 0;
    }

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    if(take_ownership) //--ajf
        linecopy = line;
    else
        linecopy = strdup(line);

    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

// -ajf - the public function does strdup
int linenoiseHistoryAdd(const char *line)
{
    return lhAdd_to((char *)line, 0);
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
    char **new;

    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char*)*len);
        if (new == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char*)*tocopy);
        free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

static void strchr_rep(char *p, char s, char r)
{
    while(*p){
        if(*p==s)
            *p=r;
        p++;
    }
}

#define PLACEHOLDER_CHAR 0x01

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename) {
    mode_t old_umask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
    FILE *fp;
    int j;

    fp = fopen(filename,"w");
    umask(old_umask);
    if (fp == NULL) return -1;
    chmod(filename,S_IRUSR|S_IWUSR);
    for (j = 0; j < history_len; j++)
    {
        strchr_rep(history[j], '\n', PLACEHOLDER_CHAR);
        fprintf(fp,"%s\n",history[j]);
        strchr_rep(history[j], PLACEHOLDER_CHAR, '\n');
    }
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename,"r");
    char *buf=NULL;
    size_t buflen=0;
    ssize_t nread;

    if (fp == NULL) return -1;

    while ((nread = getline(&buf, &buflen, fp)) != -1) { // --ajf
        char *p;

        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        strchr_rep(buf, PLACEHOLDER_CHAR, '\n');  // -ajf
        lhAdd_to(buf,1);
        buf=NULL;
        buflen=0;
    }
    if(buf)
        free(buf);
    fclose(fp);
    return 0;
}
