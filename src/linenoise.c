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
   Additions copyright 2026 Aaron Flin aaron at flin dot org
   and under same license.

   * ctrl-x: enter forced multiline edit mode.
   * ctrl-z: suspend and drop to shell
   * multiline paste using timing
   * changes to completion that made sense for rampart
   * linenoiseState l.buf is now allocated and grows as necessary
   * changes not tested with hints, as rampart doesn't use them
   * UTF-8 aware: positions are bytes on character boundaries, the screen is
     in display columns (wide chars, combining marks, tabs, ANSI in prompts)
   * one input queue; full CSI parsing; bracketed paste; ctrl/alt word moves
   * terminal resize (SIGWINCH) handled while editing
*/

#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h> //-ajf 2025-10-11
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include "linenoise.h"
#include "linenoise_wtab.h"

/* Coordinate fd-0 ownership with nodeshim's process.stdin event-driven
   data pump.  See include/rampart.h for the constants and contract.
   We touch this from enableRawMode (acquire) and disableRawMode
   (release).  Declared extern here so linenoise.c stays free of
   rampart.h includes. */
extern int rp_stdin_owner;
#define RP_STDIN_NONE_     0
#define RP_STDIN_REPL_     1
#define RP_STDIN_NODESHIM_ 2

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_ADD_OVERHEAD 256 // -ajf - wiggle room for additional edited text
#define LINENOISE_MAX_LINE 4096
#define LINENOISE_INITIAL_LINE 512 // -ajf - initial size of allocated buf
static char *unsupported_term[] = {"dumb", "cons25", "emacs", NULL};
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

static struct termios orig_termios; /* In order to restore at exit.*/
static int maskmode = 0;            /* Show "***" instead of input. For passwords. */
static int rawmode = 0;             /* For atexit() function to check if restore is needed*/
static int atexit_registered = 0;   /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

// stdout recorder state (defined with the recorder, below)
static int rec_orig_stdout; // the real terminal while fd 1 is the recorder pipe
static int rec_active;      // fd 1 is currently redirected

// fd that reaches the terminal without passing through the recorder
static int tty_out_fd(void)
{
    return rec_active ? rec_orig_stdout : STDOUT_FILENO;
}

static int in_ml_paste_or_edit = 0; // -- ajf - 2025-10-10 - whether in the middle of a multi-lined paste/edit
static int force_ml_edit = 0;       // -- ajf - 2025-10-11 - whether to force multi-line mode

/* Interrupt pipe: caller writes to the writable end to wake an in-flight
 * linenoise read; the edit loop polls the readable end alongside stdin
 * and returns with errno=ECANCELED when a byte arrives. Lazy-created on
 * first linenoiseInterrupt() call. Bytes written (if any) are stashed
 * in linenoise_intr_last for the caller to surface to JS. */
static int linenoise_intr_pipe[2] = {-1, -1};
static char *linenoise_intr_last = NULL;
static size_t linenoise_intr_last_len = 0;
#define REFRESH_REPOSITION 0        // just move cursor based on bufpos_r & bufpos_c and update l->pos
#define REFRESH_FROM_POS 1          // refresh from currrent l->pos to end
#define REFRESH_LINE 2              // refresh the current line
#define REFRESH_FULL 3              // redraw the entire l->buf within confines of screen

// -- ajf - 2025-10-10 position helper for multiline paste/edit
typedef struct rowcol
{
    int promptrow; // which line of the screen is our prompt on

    // For the buffer we are about to write to screen or update
    int bufpos_r; // which row our cursor is at relative to prompt line
    int bufpos_c; // which col our cursor is at relative to prompt line
    int bufdim_r; // max number of rows current buf
    int bufdim_c; // max number of cols current buf

    // for our current term
    int screenpos_r; // current location of cursor on screen
    int screenpos_c; // current location of cursor on screen
    int screendim_r; // current screen dimensions
    int screendim_c; // current screen dimensions

    // for the current line at our editing position
    int linestart; // start of l->pos line in buf
    int eol;       // end of line starting at linestart

    int delta_r; // for arrow navigating
    int delta_c; // for arrow navigating
    int savecol; // col position when going up or down, keep cursor there
    int hshift;  // shift to the left for horizontal overflow

    int refresh_type; // see above REFRESH_*
} rowcol;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState
{
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t promptlen;   /* prompt length in BYTES: only for writing it */
    size_t promptwidth; /* prompt width in COLUMNS: for all screen arithmetic */
    int oldrpos;        /* single-line refresh: cursor row (1-based) after the last refresh */
    size_t plen;       /* Prompt length. */
    size_t pos;        /* Current cursor position. */
    size_t oldpos;     /* Previous refresh cursor position. */
    size_t len;        /* Current edited line length. */
    size_t cols;       /* Number of columns in terminal. */
    size_t maxrows;    /* Maximum num of rows used so far (multiline mode) */
    int history_index; /* The history index we are currently editing. */
    rowcol rc;         // info for multiline editing
};

enum KEY_ACTION
{
    KEY_NULL = 0,   /* NULL */
    CTRL_A = 1,     /* Ctrl+a */
    CTRL_B = 2,     /* Ctrl-b */
    CTRL_C = 3,     /* Ctrl-c */
    CTRL_D = 4,     /* Ctrl-d */
    CTRL_E = 5,     /* Ctrl-e */
    CTRL_F = 6,     /* Ctrl-f */
    CTRL_H = 8,     /* Ctrl-h */
    TAB = 9,        /* Tab */
    CTRL_K = 11,    /* Ctrl+k */
    CTRL_L = 12,    /* Ctrl+l */
    ENTER = 13,     /* Enter */
    CTRL_N = 14,    /* Ctrl-n */
    CTRL_P = 16,    /* Ctrl-p */
    CTRL_T = 20,    /* Ctrl-t */
    CTRL_U = 21,    /* Ctrl+u */
    CTRL_W = 23,    /* Ctrl+w */
    CTRL_X = 24,    /* Ctrl+x  --ajf 2025-10-11 */
    CTRL_Z = 26,    /* Ctrl+z  --ajf 2025-10-11 */
    ESC = 27,       /* Escape */
    BACKSPACE = 127 /* Backspace */
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);

/* Drop the scratch entry linenoiseEdit() keeps at the end of the history
 * while a line is being edited.  Called on every way out of the editor. */
static void pop_scratch_history(void)
{
    if (history_len > 0)
    {
        history_len--;
        free(history[history_len]);
        history[history_len] = NULL;
    }
}

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (lndebug_fp == NULL)                                                                                        \
        {                                                                                                              \
            lndebug_fp = fopen("/tmp/lndebug.txt", "a");                                                               \
            fprintf(lndebug_fp, "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", (int)l->len,            \
                    (int)l->pos, (int)l->oldpos, plen, rows, rpos, (int)l->maxrows, old_rows);                         \
        }                                                                                                              \
        fprintf(lndebug_fp, ", " __VA_ARGS__);                                                                         \
        fflush(lndebug_fp);                                                                                            \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void)
{
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void)
{
    maskmode = 0;
}

/* Lazy self-pipe init. Non-fatal if pipe(2) fails — interrupt support
 * just stays disabled and linenoise reads block on stdin only. */
static void linenoise_intr_pipe_ensure(void)
{
    if (linenoise_intr_pipe[0] >= 0)
        return;
    if (pipe(linenoise_intr_pipe) < 0)
    {
        linenoise_intr_pipe[0] = -1;
        linenoise_intr_pipe[1] = -1;
    }
}

static void linenoise_intr_set_last(const char *buf, size_t len)
{
    free(linenoise_intr_last);
    linenoise_intr_last = NULL;
    linenoise_intr_last_len = 0;
    char *copy = malloc(len + 1);
    if (!copy)
        return;
    if (len)
        memcpy(copy, buf, len);
    copy[len] = '\0';
    linenoise_intr_last = copy;
    linenoise_intr_last_len = len;
}

/* Wake any in-flight linenoise read. `payload`/`len` are written to the
 * pipe; on the read side they get surfaced via linenoiseLastIntrSignal().
 * No-payload (len==0) writes a single null byte; the drain side maps a
 * lone null back to an empty signal string. Safe to call from any
 * thread — write(2) on a pipe fd is atomic for PIPE_BUF-sized writes. */
void linenoiseInterrupt(const char *payload, size_t len)
{
    linenoise_intr_pipe_ensure();
    if (linenoise_intr_pipe[1] < 0)
        return;
    if (!payload || len == 0)
    {
        char z = 0;
        (void)write(linenoise_intr_pipe[1], &z, 1);
        return;
    }
    (void)write(linenoise_intr_pipe[1], payload, len);
}

/* Returns the bytes drained from the pipe at the last wake (or "" if
 * none / lone-null sentinel). Owned by linenoise; valid until next wake. */
const char *linenoiseLastIntrSignal(size_t *len_out)
{
    if (len_out)
        *len_out = linenoise_intr_last_len;
    return linenoise_intr_last ? linenoise_intr_last : "";
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void)
{
    char *term = getenv("TERM");
    int j;

    if (term == NULL)
        return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term, unsupported_term[j]))
            return 1;
    return 0;
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO))
        goto fatal;
    if (!atexit_registered)
    {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd, &orig_termios) == -1)
        goto fatal;

    raw = orig_termios; /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    // raw.c_oflag &= ~(OPOST); // -ajf 2024/12/29 -- see https://github.com/antirez/linenoise/issues/128
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* Refuse to enter raw mode if nodeshim's process.stdin pump
       currently owns fd 0.  The REPL entry points in cmdline.c
       (repl_thr) and rampart-utils.c (rp_repl) check this up front
       and throw a clearer JS-level error before we get here, so
       reaching this branch implies the user attached a stdin pump
       AFTER the REPL was already running.  We can't throw from
       linenoise so just fail like ENOTTY. */
    if (rp_stdin_owner == RP_STDIN_NODESHIM_)
        goto fatal;

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
        goto fatal;
    rawmode = 1;
    rp_stdin_owner = RP_STDIN_REPL_;
    // bracketed paste on: pastes arrive wrapped in ESC[200~ ... ESC[201~
    (void)write(tty_out_fd(), "\033[?2004h", 8);
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd)
{
    /* Don't even check the return value as it's too late. */
    if (rawmode)
        (void)write(tty_out_fd(), "\033[?2004l", 8); // bracketed paste off
    if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
        rawmode = 0;
    /* Release fd 0 if we were the owner.  Don't stomp NODESHIM
       ownership if somehow it's set (shouldn't be reachable). */
    if (rp_stdin_owner == RP_STDIN_REPL_)
        rp_stdin_owner = RP_STDIN_NONE_;
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void)
{
    if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0)
    {
        /* nothing to do, just to avoid warning. */
    }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void)
{
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* Make room for `need` bytes plus the NUL.  l->buflen is always the
 * allocation size minus one.  Returns 0 if out of memory. */
static int ensure_buf(struct linenoiseState *l, size_t need)
{
    if (need <= l->buflen)
        return 1;
    size_t alloc = need + 1 + LINENOISE_ADD_OVERHEAD;
    char *nb = realloc(l->buf, alloc);
    if (!nb)
        return 0;
    l->buf = nb;
    l->buflen = alloc - 1;
    return 1;
}

/* Replace the whole edit buffer with s[0..n). */
static void set_buf(struct linenoiseState *l, const char *s, size_t n)
{
    if (!ensure_buf(l, n))
        n = l->buflen;
    memcpy(l->buf, s, n);
    l->buf[n] = '\0';
    l->len = l->pos = n;
}

/* ===================== UTF-8 / display width layer -ajf ====================
 *
 * Buffer positions (pos, len) are byte offsets and always sit on a grapheme
 * cluster boundary.  Everything that talks to the screen is in display
 * columns.  Width is the sum of the code point widths (the wcwidth
 * convention used by xterm, tmux and readline); movement and deletion are by
 * whole cluster.  No locale or libc wcwidth() dependency. */

/* Decode one code point from s[0..n), n >= 1.  Returns bytes consumed;
 * malformed input consumes one byte and yields U+FFFD. */
static size_t u8_decode(const char *s, size_t n, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    uint32_t c = u[0], min;
    size_t need, i;

    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF)
        need = 1, c &= 0x1F, min = 0x80;
    else if ((c & 0xF0) == 0xE0)
        need = 2, c &= 0x0F, min = 0x800;
    else if (c >= 0xF0 && c <= 0xF4)
        need = 3, c &= 0x07, min = 0x10000;
    else
        goto bad;
    if (need >= n)
        goto bad;
    for (i = 1; i <= need; i++)
    {
        if ((u[i] & 0xC0) != 0x80)
            goto bad;
        c = (c << 6) | (u[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        goto bad;
    *cp = c;
    return need + 1;
bad:
    *cp = 0xFFFD;
    return 1;
}

static int in_table(uint32_t cp, const struct ln_interval *t, size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        if (cp < t[mid].first)
            hi = mid;
        else if (cp > t[mid].last)
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

/* Columns for one printable code point: 0, 1 or 2. */
static int cp_width(uint32_t cp)
{
    if (cp < 0x300)
        return 1;
    if (in_table(cp, ln_zero_width, sizeof(ln_zero_width) / sizeof(ln_zero_width[0])))
        return 0;
    if (in_table(cp, ln_wide, sizeof(ln_wide) / sizeof(ln_wide[0])))
        return 2;
    return 1;
}

#define IS_CTRL(cp) ((cp) < 0x20 || (cp) == 0x7f)
#define IS_RI(cp) ((cp) >= 0x1F1E6 && (cp) <= 0x1F1FF)   /* regional indicator */
#define IS_EMOD(cp) ((cp) >= 0x1F3FB && (cp) <= 0x1F3FF) /* emoji skin tone */

/* Byte length of the grapheme cluster starting at s[pos], pos < len: a base
 * code point plus what attaches to it (combining marks, variation selectors,
 * skin tones, ZWJ + next, the second half of a flag). */
static size_t gr_next(const char *s, size_t pos, size_t len)
{
    uint32_t cp, nx;
    size_t k, i = pos + u8_decode(s + pos, len - pos, &cp);

    if (IS_CTRL(cp))
        return i - pos;
    if (IS_RI(cp) && i < len)
    {
        k = u8_decode(s + i, len - i, &nx);
        if (IS_RI(nx))
            i += k;
    }
    while (i < len)
    {
        k = u8_decode(s + i, len - i, &nx);
        if (nx == 0x200D)
        {
            i += k;
            if (i < len)
            {
                k = u8_decode(s + i, len - i, &nx);
                if (!IS_CTRL(nx))
                    i += k;
            }
            continue;
        }
        if (nx >= 0x300 && (IS_EMOD(nx) || cp_width(nx) == 0))
        {
            i += k;
            continue;
        }
        break;
    }
    return i - pos;
}

/* Byte length of the cluster that ends at s[pos], pos > 0.  Walks forward
 * from the start of the line so it always agrees with gr_next(). */
static size_t gr_prev(const char *s, size_t pos, size_t len)
{
    size_t i = pos, k = 1;

    while (i > 0 && s[i - 1] != '\n')
        i--;
    if (i == pos)
        return 1; // the newline itself
    while (i < pos)
    {
        k = gr_next(s, i, len);
        if (i + k > pos) // pos was inside a cluster
            return pos - i;
        i += k;
    }
    return k;
}

/* Cluster at s[i], i < n.  Returns its byte length and sets *w to the
 * columns it takes when drawn at absolute column abscol.  tabstops > 0:
 * tabs run to the next multiple of 8.  Otherwise tabs, like other control
 * characters, are drawn as ^X.  tabstops < 0 is for prompt text, which mask
 * mode must not hide. */
static size_t cl_at(const char *s, size_t i, size_t n, int abscol, int tabstops, int *w)
{
    unsigned char c = (unsigned char)s[i];
    size_t g, j;
    uint32_t cp;

    if (maskmode && tabstops >= 0) // every character shows as one '*'
    {
        *w = 1;
        return gr_next(s, i, n);
    }
    if (c == '\t' && tabstops > 0)
    {
        *w = 8 - (abscol % 8);
        return 1;
    }
    if (IS_CTRL(c))
    {
        *w = 2;
        return 1;
    }
    g = gr_next(s, i, n);
    *w = 0;
    for (j = 0; j < g;)
    {
        j += u8_decode(s + i + j, g - j, &cp);
        *w += cp_width(cp);
    }
    return g;
}

/* Columns taken by s[0..n) when it starts at absolute column startcol. */
static int span_width(const char *s, size_t n, int startcol, int tabstops)
{
    size_t i = 0;
    int col = startcol, w;

    while (i < n)
    {
        i += cl_at(s, i, n, col, tabstops, &w);
        col += w;
    }
    return col - startcol;
}

/* Byte offset in s[0..n) of the cluster covering column `col` (relative to
 * the start of s); *actual gets the column of that cluster's left edge.
 * Past the end returns n. */
static size_t col_to_pos(const char *s, size_t n, int col, int startcol, int tabstops, int *actual)
{
    size_t i = 0, k;
    int c = 0, w;

    while (i < n)
    {
        k = cl_at(s, i, n, startcol + c, tabstops, &w);
        if (c + w > col)
            break;
        c += w;
        i += k;
    }
    if (actual)
        *actual = c;
    return i;
}

/* Length of the terminal escape sequence at s[0..n), or 0 if none.  Also
 * treats readline's \001 and \002 prompt markers as zero-width. */
static size_t ansi_skip(const char *s, size_t n)
{
    size_t i;

    if (n && (s[0] == '\001' || s[0] == '\002'))
        return 1;
    if (n < 2 || s[0] != '\x1b')
        return 0;
    if (s[1] == '[') // CSI: parameters, intermediates, final byte
    {
        for (i = 2; i < n && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] <= 0x3f; i++)
            ;
        return i < n ? i + 1 : i;
    }
    if (s[1] == ']') // OSC: ends with BEL or ESC backslash
    {
        for (i = 2; i < n; i++)
        {
            if (s[i] == '\a')
                return i + 1;
            if (s[i] == '\x1b' && i + 1 < n && s[i + 1] == '\\')
                return i + 2;
        }
        return n;
    }
    if (strchr("()*+", s[1])) // charset designation
        return n < 3 ? n : 3;
    return 2;
}

/* Display width of a prompt: escape sequences take no columns. */
static int prompt_width(const char *p, size_t n)
{
    size_t i = 0, k;
    int col = 0, w;

    while (i < n)
    {
        if ((k = ansi_skip(p + i, n - i)))
        {
            i += k;
            continue;
        }
        i += cl_at(p, i, n, col, -1, &w);
        col += w;
    }
    return col;
}

/* ============================== Input layer -ajf ===========================
 *
 * All terminal input goes through one queue so that bytes read while looking
 * for something else (a cursor position report, the end of a paste) are kept
 * and replayed in order. */

#define RK_EOF -1
#define RK_INTR -2    /* woken by linenoiseInterrupt() */
#define RK_TIMEOUT -3
#define RK_RESIZE -4  /* terminal size changed */

static unsigned char *inq = NULL;
static size_t inq_head = 0, inq_len = 0, inq_cap = 0;

static int inq_room(size_t n)
{
    if (inq_head + inq_len + n <= inq_cap)
        return 1;
    if (inq_head)
    {
        memmove(inq, inq + inq_head, inq_len);
        inq_head = 0;
    }
    if (inq_len + n > inq_cap)
    {
        size_t nc = (inq_len + n) * 2 + 64;
        unsigned char *nb = realloc(inq, nc);
        if (!nb)
            return 0;
        inq = nb;
        inq_cap = nc;
    }
    return 1;
}

static void inq_append(const unsigned char *s, size_t n)
{
    if (!n || !inq_room(n))
        return;
    memcpy(inq + inq_head + inq_len, s, n);
    inq_len += n;
}

/* put bytes back at the FRONT of the queue */
static void inq_unread(const unsigned char *s, size_t n)
{
    if (!n || !inq_room(n))
        return;
    if (inq_head >= n)
        inq_head -= n;
    else
    {
        memmove(inq + n, inq + inq_head, inq_len);
        inq_head = 0;
    }
    memcpy(inq + inq_head, s, n);
    inq_len += n;
}

static void inq_clear(void)
{
    inq_head = inq_len = 0;
}

/* SIGWINCH self-pipe: the handler may run on any thread, so it wakes the
 * editor's poll() through a pipe rather than relying on EINTR. */
static int winch_pipe[2] = {-1, -1};
static struct sigaction winch_old;
static int winch_installed = 0;

static void on_winch(int sig)
{
    int e = errno;
    if (winch_pipe[1] >= 0)
        (void)write(winch_pipe[1], "w", 1);
    if (!(winch_old.sa_flags & SA_SIGINFO) && winch_old.sa_handler != SIG_DFL && winch_old.sa_handler != SIG_IGN
        && winch_old.sa_handler != on_winch)
        winch_old.sa_handler(sig);
    errno = e;
}

static void winch_install(void)
{
    struct sigaction sa;
    int i;

    if (winch_pipe[0] < 0 && pipe(winch_pipe) == 0)
        for (i = 0; i < 2; i++)
        {
            fcntl(winch_pipe[i], F_SETFL, fcntl(winch_pipe[i], F_GETFL) | O_NONBLOCK);
            fcntl(winch_pipe[i], F_SETFD, FD_CLOEXEC);
        }
    if (winch_pipe[0] < 0 || winch_installed)
        return;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_winch;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, &winch_old) == 0)
        winch_installed = 1;
}

static void winch_restore(void)
{
    if (winch_installed)
    {
        sigaction(SIGWINCH, &winch_old, NULL);
        winch_installed = 0;
    }
}

/* Wait for input.  timeout_ms < 0 blocks, and only then do interrupts and
 * resizes wake us.  Returns 1 when bytes are ready, 0 on timeout, else RK_*. */
static int in_wait(struct linenoiseState *l, int timeout_ms)
{
    struct pollfd p[3];
    int n = 1, pr, ii = -1, wi = -1;

    if (inq_len)
        return 1;
    p[0].fd = l->ifd;
    p[0].events = POLLIN;
    if (timeout_ms < 0)
    {
        if (linenoise_intr_pipe[0] >= 0)
            p[ii = n++].fd = linenoise_intr_pipe[0];
        if (winch_pipe[0] >= 0)
            p[wi = n++].fd = winch_pipe[0];
    }
    for (pr = 1; pr < n; pr++)
        p[pr].events = POLLIN;
    for (pr = 0; pr < n; pr++)
        p[pr].revents = 0;
    do
    {
        pr = poll(p, n, timeout_ms);
    } while (pr < 0 && errno == EINTR);
    if (pr < 0)
        return RK_EOF;
    if (pr == 0)
        return 0;
    if (ii > 0 && (p[ii].revents & POLLIN))
    {
        char drainbuf[256];
        ssize_t dn = read(linenoise_intr_pipe[0], drainbuf, sizeof(drainbuf));
        if (dn < 0)
            dn = 0;
        if (dn == 1 && drainbuf[0] == 0)
            linenoise_intr_set_last("", 0);
        else
            linenoise_intr_set_last(drainbuf, (size_t)dn);
        return RK_INTR;
    }
    if (wi > 0 && (p[wi].revents & POLLIN))
    {
        char drainbuf[64];
        while (read(winch_pipe[0], drainbuf, sizeof(drainbuf)) > 0)
            ;
        return RK_RESIZE;
    }
    return 1;
}

/* pull whatever the terminal has into the queue; 0 on EOF/error */
static int in_fill(struct linenoiseState *l)
{
    unsigned char tmp[4096];
    ssize_t n;

    do
    {
        n = read(l->ifd, tmp, sizeof(tmp));
    } while (n < 0 && errno == EINTR);
    if (n <= 0)
        return 0;
    inq_append(tmp, (size_t)n);
    return 1;
}

/* Next input byte (0..255), or RK_*. */
static int read_key(struct linenoiseState *l, int timeout_ms)
{
    if (!inq_len)
    {
        int w = in_wait(l, timeout_ms);
        if (w == 0)
            return RK_TIMEOUT;
        if (w < 0)
            return w;
        if (!in_fill(l) || !inq_len)
            return RK_EOF;
    }
    int c = inq[inq_head++];
    if (!--inq_len)
        inq_head = 0;
    return c;
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc)
{
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
/* Returns the key (0..255) the caller should handle next, 0 for "read
 * another key", or RK_EOF / RK_INTR. */
static int completeLine(struct linenoiseState *ls)
{
    linenoiseCompletions lc = {0, NULL};
    int c = 0;

    completionCallback(ls->buf, &lc);
    if (lc.len == 0)
    {
        linenoiseBeep();
    }
    else
    {
        size_t stop = 0, i = 0;

        // ajf - 2025-10-09 - if there is only one, just use it:
        if (lc.len == 1)
        {
            // Use all but the last character, and queue that one as if it
            // had been typed so it goes through the normal insert path.
            size_t clen = strlen(lc.cvec[0]);
            if (clen)
            {
                size_t last = gr_prev(lc.cvec[0], clen, clen);
                set_buf(ls, lc.cvec[0], clen - last);
                inq_unread((unsigned char *)lc.cvec[0] + clen - last, last);
            }
            freeCompletions(&lc);
            return 0;
        }

        while (!stop)
        {
            /* Show completion or original buffer */
            if (i < lc.len)
            {
                struct linenoiseState saved = *ls;

                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                ls->rc.refresh_type = REFRESH_FULL;
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            }
            else
            {
                // ajf - 2025-10-09 - we are inserting the original first in the queue elsewhere
                i = 0;
                continue;
                // refreshLine(ls);
            }

            c = read_key(ls, -1);
            if (c == RK_RESIZE)
                continue;
            if (c < 0)
            {
                freeCompletions(&lc);
                return c;
            }

            switch (c)
            {
            case 9: /* tab */
                i = (i + 1) % (lc.len + 1);
                if (i == lc.len)
                    linenoiseBeep();
                break;
            case 27: /* escape */
                /* Re-show original buffer */
                if (i < lc.len)
                {
                    ls->rc.refresh_type = REFRESH_FULL;
                    refreshLine(ls);
                }
                stop = 1;
                break;
            default:
                /* Update buffer and return */
                if (i < lc.len)
                    set_buf(ls, lc.cvec[i], strlen(lc.cvec[i]));
                stop = 1;
                break;
            }
        }
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn)
{
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn)
{
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn)
{
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str)
{
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len + 1);
    if (copy == NULL)
        return;
    memcpy(copy, str, len + 1);
    cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    if (cvec == NULL)
    {
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

    copy = malloc(len + 1);
    if (copy == NULL)
        return;
    memcpy(copy, str, len + 1);
    cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
    if (cvec == NULL)
    {
        free(copy);
        return;
    }
    memmove(&cvec[1], &cvec[0], sizeof(char *) * lc->len);
    lc->cvec = cvec;
    lc->cvec[0] = copy;
    lc->len++;
}

// -ajf 2025-10-09
void linenoiseReplaceCompletion(linenoiseCompletions *lc, const char *str, size_t pos)
{
    size_t len = strlen(str);
    char *copy;

    // bug fix: added early return if lc->len == 0 - 2026-02-27
    // won't happen
    if (lc->len == 0)
        return;

    if (pos >= lc->len)
        pos = lc->len - 1;

    copy = malloc(len + 1);
    if (copy == NULL)
        return;

    memcpy(copy, str, len + 1);

    free(lc->cvec[pos]);
    lc->cvec[pos] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf
{
    char *b;
    int len;
};

static void abInit(struct abuf *ab)
{
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab)
{
    free(ab->b);
}

static void ab_cluster(struct abuf *ab, const char *s, size_t k, int w, int tabs);

/* Helper of refreshSingleLine() to show hints to the right of the input.
 * `used` is the number of columns the prompt and input already take. */
void refreshShowHints(struct abuf *ab, struct linenoiseState *l, int used)
{
    char seq[64];
    if (hintsCallback && used < (int)l->cols)
    {
        int color = -1, bold = 0;
        char *hint = hintsCallback(l->buf, &color, &bold);
        if (hint)
        {
            size_t hintlen = strlen(hint);
            int actual;
            // as much of the hint as fits, cut between characters
            hintlen = col_to_pos(hint, hintlen, (int)l->cols - used, used, -1, &actual);
            if (bold == 1 && color == -1)
                color = 37;
            if (color != -1 || bold != 0)
                snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
            else
                seq[0] = '\0';
            abAppend(ab, seq, strlen(seq));
            abAppend(ab, hint, (int)hintlen);
            if (color != -1 || bold != 0)
                abAppend(ab, "\033[0m", 4);
            /* Call the function to free the hint returned. */
            if (freeHintsCallback)
                freeHintsCallback(hint);
        }
    }
}

/* Where the terminal cursor ends up after the prompt and buf[0..upto) have
 * been written on a terminal l->cols wide: *row is 0-based from the prompt
 * row.  *col == cols means the last cell of the row was just filled.  A wide
 * character that does not fit in the last cell moves whole to the next row,
 * which is what terminals do. */
static void wrap_walk(struct linenoiseState *l, size_t upto, int *row, int *col)
{
    int cols = (int)l->cols, r = 0, c = (int)l->promptwidth, w;
    size_t i = 0;

    while (c > cols)
        c -= cols, r++;
    while (i < upto)
    {
        i += cl_at(l->buf, i, l->len, c, 0, &w);
        if (c + w > cols)
            r++, c = 0;
        c += w;
    }
    *row = r;
    *col = c;
}

/* Was - Multi line low level line refresh.
 * Now - refreshSingleLine - and old refreshSingleLine removed.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.  All arithmetic
 * is in display columns. */
static void refreshSingleLine(struct linenoiseState *l)
{
    char seq[64];
    int cols = (int)l->cols;
    int rows;               /* rows used by current buf. */
    int rpos = l->oldrpos;  /* cursor relative row before this refresh. */
    int rpos2;              /* rpos after refresh. */
    int col;                /* colum position, zero-based. */
    int old_rows = l->maxrows;
    int fd = l->ofd, j, er, ec, w;
    size_t i, k;
    struct abuf ab;

    wrap_walk(l, l->len, &er, &ec);
    rows = er + 1;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows)
        l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    if (old_rows - rpos > 0)
    {
        lndebug("go down %d", old_rows - rpos);
        snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
        abAppend(&ab, seq, strlen(seq));
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows - 1; j++)
    {
        lndebug("clear+up");
        snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
        abAppend(&ab, seq, strlen(seq));
    }

    /* Clean the top line. */
    lndebug("clear");
    snprintf(seq, 64, "\r\x1b[0K");
    abAppend(&ab, seq, strlen(seq));

    /* Write the prompt and the current buffer content */
    abAppend(&ab, l->prompt, l->promptlen);
    for (i = 0; i < l->len; i += k)
    {
        k = cl_at(l->buf, i, l->len, 0, 0, &w);
        ab_cluster(&ab, l->buf + i, k, w, 0);
    }

    /* Show hits if any. */
    if (er == 0)
        refreshShowHints(&ab, l, ec);

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    wrap_walk(l, l->pos, &rpos2, &col);
    if (l->pos && l->pos == l->len && col == cols)
    {
        lndebug("<newline>");
        abAppend(&ab, "\n", 1);
        snprintf(seq, 64, "\r");
        abAppend(&ab, seq, strlen(seq));
        rows++;
        if (rows > (int)l->maxrows)
            l->maxrows = rows;
        rpos2++, col = 0;
    }
    else if (l->pos < l->len)
    {
        /* the character under the cursor starts the next row */
        cl_at(l->buf, l->pos, l->len, col, 0, &w);
        if (col + w > cols)
            rpos2++, col = 0;
    }

    /* Move cursor to right position. */
    rpos2++; /* current cursor relative row, 1-based. */
    lndebug("rpos2 %d", rpos2);

    /* Go up till we reach the expected positon. */
    if (rows - rpos2 > 0)
    {
        lndebug("go-up %d", rows - rpos2);
        snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
        abAppend(&ab, seq, strlen(seq));
    }

    /* Set column. */
    lndebug("set col %d", 1 + col);
    if (col)
        snprintf(seq, 64, "\r\x1b[%dC", col);
    else
        snprintf(seq, 64, "\r");
    abAppend(&ab, seq, strlen(seq));

    lndebug("\n");
    l->oldpos = l->pos;
    l->oldrpos = rpos2;

    (void)write(fd, ab.b, ab.len);
    abFree(&ab);

    // -ajf - track promptrow through terminal auto-wrap scrolls. When the
    // wrapped rendering would extend past the bottom row, the terminal
    // scrolls; the actual prompt moves up but rcp->promptrow stays stale.
    // Subsequent transitions to multi-line mode (e.g. polpaste) then use
    // a stale promptrow and refresh_full leaves a duplicate of the first
    // line above the fresh render.  `rows` already counts the extra row
    // written when the cursor sits exactly at a row boundary.
    {
        int last_row = l->rc.promptrow + rows - 1;
        if (last_row > l->rc.screendim_r)
            l->rc.promptrow -= (last_row - l->rc.screendim_r);
    }
}

/* ***************************** MULTILINE -ajf ***************************** */
#define WCODE(s, len) (void)write(l->ofd, "\033[" s, len + 2)
#define WCODEF(maxlen, fmt, ...)                                               \
    do                                                                         \
    {                                                                          \
        char b[maxlen];                                                        \
        snprintf(b, maxlen, "\033[" fmt, __VA_ARGS__);                         \
        (void)write(l->ofd, b, strlen(b));                                     \
    } while (0)

//#define DEBUGMSGS 1
#ifdef DEBUGMSGS
static int dbcnt = 0;
#define DEBUGF(r, c, ...)                                                      \
    do                                                                         \
    {                                                                          \
        write(l->ofd, "\0337", 2);                                             \
        printf("\033[%d;%dH\033[0K\033[31m#%d ", (r), (c), dbcnt++);           \
        printf(__VA_ARGS__);                                                   \
        printf("\033[0m");                                                     \
        fflush(stdout);                                                        \
        write(l->ofd, "\0338", 2);                                             \
    } while (0)
#else
#define DEBUGF(r, c, ...) /* nada */
#endif
static size_t line_start(struct linenoiseState *l);

// absolute column at which buffer row `row` starts: the prompt shares row 0
#define ROWSTART(l, row) ((row) ? 0 : (int)(l)->promptwidth)

// set position in l->buf based on bufpos_r and bufpos_c (display columns).
// A column past the end of the row, or inside a wide character, snaps to
// the nearest character boundary on its left and bufpos_c is updated.
static void set_pos(struct linenoiseState *l)
{
    int row = l->rc.bufpos_r, currow = 0, actual;
    size_t ls = 0, le, i;

    for (i = 0; i < l->len && currow < row; i++)
        if (l->buf[i] == '\n')
        {
            currow++;
            ls = i + 1;
        }
    if (currow != row)
        return;
    for (le = ls; le < l->len && l->buf[le] != '\n'; le++)
        ;
    l->pos = ls + col_to_pos(l->buf + ls, le - ls, l->rc.bufpos_c, ROWSTART(l, row), 1, &actual);
    l->rc.bufpos_c = actual;
}

// counting from the top left, set col/row from l->pos.  Rows are buffer
// lines; columns are display columns within the line (prompt not included).
static void get_bufpos(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    size_t ls = 0, i;
    int row = 0, w, found = 0;

    rcp->bufdim_c = 0;
    rcp->bufpos_r = 0;
    rcp->linestart = 0;
    rcp->eol = (int)l->len;
    for (i = 0; i <= l->len; i++)
    {
        if (i < l->len && l->buf[i] != '\n')
            continue;
        w = span_width(l->buf + ls, i - ls, ROWSTART(l, row), 1);
        if (w > rcp->bufdim_c)
            rcp->bufdim_c = w;
        if (!found && i >= l->pos) // first line end at or past pos: our line
        {
            found = 1;
            rcp->linestart = (int)ls;
            rcp->eol = (int)i;
            rcp->bufpos_r = row;
        }
        if (i < l->len)
        {
            row++;
            ls = i + 1;
        }
    }
    rcp->bufdim_r = row;
    rcp->bufpos_c =
        span_width(l->buf + rcp->linestart, l->pos - (size_t)rcp->linestart, ROWSTART(l, rcp->bufpos_r), 1);
}

// Helper to parse response from ESC [ row ; col R
// Reads straight from the terminal (the reply is newer than anything already
// queued).  Bytes that are not part of the reply are user input: they go on
// the END of the input queue.  Gives up after 200ms of silence.
static int read_cpr(struct linenoiseState *l, int *row, int *col)
{
    unsigned char seq[32], ch;
    size_t n = 0;
    int st = 0, r = 0, c = 0, budget = 512, pr;

    while (budget-- > 0)
    {
        struct pollfd p = {.fd = l->ifd, .events = POLLIN, .revents = 0};
        do
        {
            pr = poll(&p, 1, 200);
        } while (pr < 0 && errno == EINTR);
        if (pr <= 0 || read(l->ifd, &ch, 1) != 1)
            break;
    again:
        switch (st)
        {
        case 0: // looking for ESC
            if (ch == 0x1b)
                seq[0] = ch, n = 1, st = 1;
            else
                inq_append(&ch, 1);
            continue;
        case 1:
            if (ch != '[')
                break;
            seq[n++] = ch, st = 2, r = c = 0;
            continue;
        case 2: // row digits
            if (isdigit(ch) && n < sizeof(seq) - 1)
                seq[n++] = ch, r = r * 10 + (ch - '0');
            else if (ch == ';' && n > 2)
                seq[n++] = ch, st = 3;
            else
                break;
            continue;
        case 3: // col digits
            if (isdigit(ch) && n < sizeof(seq) - 1)
                seq[n++] = ch, c = c * 10 + (ch - '0');
            else if (ch == 'R' && seq[n - 1] != ';')
            {
                if (row)
                    *row = r;
                if (col)
                    *col = c;
                return 0;
            }
            else
                break;
            continue;
        }
        // not a position report after all: it was user input
        inq_append(seq, n);
        n = 0, st = 0;
        goto again;
    }
    inq_append(seq, n);
    return -1;
}

static void get_screenpos(struct linenoiseState *l)
{
    int r = 0, c = 0;
    int tr = 0, tc = 0;
    rowcol *rcp = &(l->rc);
    static int cpr_broken = 0; // terminal never answers: stop asking
    static int misses = 0;

    if (cpr_broken)
    {
        struct winsize ws;
        if (ioctl(l->ifd, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row)
        {
            rcp->screendim_r = ws.ws_row;
            rcp->screendim_c = ws.ws_col;
        }
        return;
    }

    // -ajf - Save cursor with DECSC (ESC 7) so we can always restore it,
    // even when the CPR read below fails. Previously, when queued user
    // input bytes (e.g. autorepeating arrow keys) collided with the CPR
    // response, read_cpr would parse them as garbage and return -1; the
    // subsequent dimension probe (\033[9999;9999H) then left the cursor
    // stranded at the bottom-right corner.
    (void)write(l->ofd, "\0337", 2);

    // Query current position. On failure, keep previous screenpos values
    // (do not zero them — stale values are better than (1,1) defaults
    // which would confuse vert_scroll's edge checks).
    WCODE("6n", 2);
    if (read_cpr(l, &r, &c) == 0)
    {
        rcp->screenpos_r = r;
        rcp->screenpos_c = c;
        misses = 0;
    }
    else
    {
        // no answer: size from the tty driver, cursor row stays a guess.
        // Two misses in a row and we stop asking.
        (void)write(l->ofd, "\0338", 2);
        int give_up = ++misses >= 2;
        cpr_broken = 1;
        get_screenpos(l);
        cpr_broken = give_up;
        return;
    }

    // Move to bottom-right, ask again to discover screen dimensions.
    WCODE("9999;9999H", 10);
    WCODE("6n", 2);
    if (read_cpr(l, &tr, &tc) == 0)
    {
        rcp->screendim_r = tr;
        rcp->screendim_c = tc;
    }

    // -ajf - Always restore (DECRC, ESC 8), regardless of read success.
    (void)write(l->ofd, "\0338", 2);
}

static void ab_spaces(struct abuf *ab, int n)
{
    while (n-- > 0)
        abAppend(ab, " ", 1);
}

// Append one cluster as it should look on screen, w columns wide.  `tabs`
// must match the tabstops flag its width was measured with.
static void ab_cluster(struct abuf *ab, const char *s, size_t k, int w, int tabs)
{
    unsigned char c = (unsigned char)*s;
    char ctl[2];

    if (maskmode && tabs >= 0)
        abAppend(ab, "*", 1);
    else if (c == '\t' && tabs > 0)
        ab_spaces(ab, w);
    else if (IS_CTRL(c))
    {
        ctl[0] = '^';
        ctl[1] = c == 0x7f ? '?' : (char)(c + 0x40);
        abAppend(ab, ctl, 2);
    }
    else
        abAppend(ab, s, (int)k);
}

// Append the prompt with its first `skip` columns scrolled off the left
// edge.  Escape sequences in the hidden part are still sent so the colour
// state carries over.  In forced multi-line mode the prompt is magenta.
static void ab_prompt(struct abuf *ab, struct linenoiseState *l, int skip)
{
    const char *p = l->prompt;
    size_t n = l->promptlen, i = 0, k;
    int col = 0, w;

    if (force_ml_edit)
        abAppend(ab, "\033[35m", 5);
    while (i < n)
    {
        if ((k = ansi_skip(p + i, n - i)))
        {
            if (p[i] == '\x1b')
                abAppend(ab, p + i, (int)k);
            if (force_ml_edit) // keep it magenta whatever the prompt sets
                abAppend(ab, "\033[35m", 5);
            i += k;
            continue;
        }
        k = cl_at(p, i, n, col, -1, &w);
        if (col >= skip)
            ab_cluster(ab, p + i, k, w, -1);
        else if (col + w > skip) // wide character cut by the edge
            ab_spaces(ab, col + w - skip);
        col += w;
        i += k;
    }
    if (force_ml_edit)
        abAppend(ab, "\033[0m", 4);
}

static inline void write_prompt(struct linenoiseState *l)
{
    struct abuf ab;

    abInit(&ab);
    ab_prompt(&ab, l, 0);
    (void)write(l->ofd, ab.b, ab.len);
    abFree(&ab);
}

// Draw buffer line s[0..n) from byte `from` on, clipped to the visible
// window of columns [hshift, hshift + screen width).  The cursor must already
// be where s[from] belongs (column 1 of the row when from == 0).  A wide
// character cut by either edge is drawn as spaces.
static void draw_line(struct linenoiseState *l, struct abuf *ab, int row0, const char *s, size_t n, size_t from)
{
    rowcol *rcp = &(l->rc);
    int pw = (int)l->promptwidth, v = row0 ? pw : 0, w;
    int lo = rcp->hshift, hi = rcp->hshift + rcp->screendim_c;
    size_t i = 0, k;

    if (row0 && !from && rcp->hshift < pw)
        ab_prompt(ab, l, rcp->hshift);
    while (i < n && v < hi)
    {
        k = cl_at(s, i, n, v, 1, &w);
        if (i >= from)
        {
            if (v >= lo && v + w <= hi)
                ab_cluster(ab, s + i, k, w, 1);
            else if (v + w > lo)
                ab_spaces(ab, (v + w < hi ? v + w : hi) - (v > lo ? v : lo));
        }
        v += w;
        i += k;
    }
}

static void update_promptrow(struct linenoiseState *l, int rows_added)
{
    rowcol *rcp = &(l->rc);
    // -ajf - the relevant "space below" is below the CURSOR (= promptrow +
    // bufpos_r), not below the prompt row itself. When the cursor is on a
    // non-prompt row at paste/Enter time, the prompt-relative measurement
    // over-counts available space and promptrow doesn't get decremented
    // enough. Subsequent refresh_full then clears below where the prompt
    // actually moved to, leaving a stale copy of the first row above the
    // fresh render.
    int cursor_row = rcp->promptrow + rcp->bufpos_r;
    int rows_below = rcp->screendim_r - cursor_row;
    int pushed_up = rows_added - rows_below;

    if (pushed_up > 0)
        rcp->promptrow -= pushed_up;
}

static void place_cursor(struct linenoiseState *l);

// scroll one line up or down, if necessary
static void vert_scroll(struct linenoiseState *l, int up)
{
    rowcol *rcp = &(l->rc);
    struct abuf ab;

    // -ajf - only scroll if the buf row we just stepped to is actually
    // off-screen. The screen-edge check (screenpos_r at 1 or screendim_r) is
    // necessary but not sufficient: if the buffer fits on screen and the
    // target row is already visible, scrolling here causes a spurious
    // terminal scroll that leaves the cursor one row away from the row's
    // display position.
    if (up && rcp->screenpos_r == 1 && rcp->promptrow + rcp->bufpos_r < 1)
    {
        get_bufpos(l);
        abInit(&ab);
        abAppend(&ab, "\033[H\033[L", 6); // home, insert a blank row
        draw_line(l, &ab, !rcp->bufpos_r, l->buf + rcp->linestart, (size_t)(rcp->eol - rcp->linestart), 0);
        (void)write(l->ofd, ab.b, ab.len);
        abFree(&ab);
        rcp->promptrow++;
        place_cursor(l);
    }
    else if (!up && rcp->screenpos_r == rcp->screendim_r && rcp->promptrow + rcp->bufpos_r > rcp->screendim_r)
    {
        get_bufpos(l);
        abInit(&ab);
        abAppend(&ab, "\033[9999;9999H\n\r", 14); // newline at bottom
        draw_line(l, &ab, !rcp->bufpos_r, l->buf + rcp->linestart, (size_t)(rcp->eol - rcp->linestart), 0);
        (void)write(l->ofd, ab.b, ab.len);
        abFree(&ab);
        rcp->promptrow--;
        place_cursor(l);
    }
}

// cursor column on screen, 0-based and before the prompt offset of row 0
#define bufcolpos (rcp->bufpos_c - rcp->hshift)

static void place_cursor(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int row = rcp->bufpos_r, col = bufcolpos + ROWSTART(l, rcp->bufpos_r);

    row += l->rc.promptrow;

    col++; // 0 to 1 based indexing

    if (row < 1) // 1 is top of screen
    {
        while (row++ < 1)
            vert_scroll(l, 1);
        l->rc.promptrow = 1;
    }

    WCODEF(32, "%d;%dH", row, col);
    // -ajf - track where the cursor now is, so refresh_* paths don't have
    // to query the terminal (which is racy under autorepeating input).
    rcp->screenpos_r = row;
    rcp->screenpos_c = col;
}

// Write the buffer to the screen: everything (full), or from the cursor to
// the bottom of the screen.  The terminal cursor must already be on the
// right row; for a partial write, also at the column of l->pos.
static void write_to_end(struct linenoiseState *l, int full)
{
    rowcol *rcp = &(l->rc);
    int nlines = full ? -1                                       // write to end of buf
                      : 1 + rcp->screendim_r - rcp->screenpos_r; // write to bottom of screen
    char *bufend = l->buf + l->len, *line = l->buf, *e;
    size_t from = 0;
    int row0 = 1;
    struct abuf ab;

    abInit(&ab);
    if (full)
        abAppend(&ab, "\r", 1);
    else
    {
        size_t ls = line_start(l);
        line = l->buf + ls;
        from = l->pos - ls;
        row0 = (ls == 0);
    }

    while (nlines)
    {
        nlines--;
        e = memchr(line, '\n', (size_t)(bufend - line));
        draw_line(l, &ab, row0, line, (size_t)((e ? e : bufend) - line), from);
        if (!e)
            break;
        if (nlines)
            abAppend(&ab, "\r\n", 2);
        line = e + 1;
        from = 0;
        row0 = 0;
    }
    (void)write(l->ofd, ab.b, ab.len);
    abFree(&ab);
}

static void refresh_window(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    size_t oldpos = l->pos;
    get_bufpos(l);
    if (rcp->promptrow > 0)
    { // just go to row and rewrite from there
        WCODEF(16, "%d;1H", rcp->promptrow);
        WCODE("0J", 2);
        l->pos = 0;
        write_to_end(l, 1);
        place_cursor(l);
    }
    else
    {
        int oldrow = rcp->bufpos_r, oldcol = rcp->bufpos_c;
        int oldscreenpos_r = rcp->screenpos_r, oldscreenpos_c = rcp->screenpos_c;
        rcp->bufpos_r = -rcp->promptrow + 1; // bufrow at top of screen - set from one based prompt row
        rcp->bufpos_c = 0;
        set_pos(l); // set l->pos from new bufpos

        WCODE("1;1H", 4);
        WCODE("0J", 2);

        rcp->screenpos_r = 1;
        rcp->screenpos_c = 1;
        write_to_end(l, 0);
        rcp->screenpos_r = oldscreenpos_r;
        rcp->screenpos_c = oldscreenpos_c;

        rcp->bufpos_r = oldrow;
        rcp->bufpos_c = oldcol;
        place_cursor(l);
    }
    l->pos = oldpos;
}

static int horiz_scroll(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int promptadd = ROWSTART(l, rcp->bufpos_r);

    if (bufcolpos + promptadd >= rcp->screendim_c)
    {
        DEBUGF(22, 5, "OVER LIMIT, hshift before %d, after %d", rcp->hshift, 8 + (rcp->bufpos_c - rcp->screendim_c));
        rcp->hshift = 8 + ((rcp->bufpos_c + promptadd) - rcp->screendim_c);
        refresh_window(l);
        place_cursor(l);
        return 1;
    }

    if (bufcolpos + promptadd < 0)
    {
        // -ajf - mirror the right-edge formula: target cursor at display col 8
        // (i.e. 8 cols from the left edge of the screen, accounting for prompt).
        DEBUGF(22, 5, "UNDER LIMIT, hshift before %d, after %d", rcp->hshift,
               rcp->bufpos_c + promptadd - 8 > 0 ? rcp->bufpos_c + promptadd - 8 : 0);
        rcp->hshift = rcp->bufpos_c + promptadd - 8;
        if (rcp->hshift < 0)
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
    place_cursor(l); // also updates screenpos_r/c
    WCODE("0J", 2); // erase from cursor to end of screen
    write_to_end(l, 0);
    place_cursor(l);
}

static void refresh_whole_line(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    struct abuf ab;

    get_bufpos(l);
    abInit(&ab);
    abAppend(&ab, "\r\033[0K", 5); // col 1, clear to end of line
    draw_line(l, &ab, !rcp->bufpos_r, l->buf + rcp->linestart, (size_t)(rcp->eol - rcp->linestart), 0);
    (void)write(l->ofd, ab.b, ab.len);
    abFree(&ab);

    place_cursor(l);
}

static void refresh_full(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);

    if (rcp->promptrow > 0)
        WCODEF(16, "%d;1H", rcp->promptrow); // go to prompt line
    else
        WCODE("H", 1); // go to top of screen

    WCODE("0J", 2); // clear to end of screen

    get_bufpos(l);

    // did we scroll horiz?
    if (!horiz_scroll(l)) // check scroll will refresh, so don't do it again
        write_to_end(l, 1); // prompt and buf

    // did we scroll down?
    if (rcp->bufdim_r + rcp->promptrow > rcp->screendim_r)
        rcp->promptrow = rcp->screendim_r - rcp->bufdim_r;
    place_cursor(l);
}

// Cursor movement only.  For left/right l->pos has already moved by one
// character; for up/down we pick the new position here.
static void refresh_reposition(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    int dc = rcp->delta_c, dr = rcp->delta_r;

    if (!dr)
    {
        int oldrow = rcp->bufpos_r;

        get_bufpos(l);
        if (rcp->bufpos_r != oldrow) // crossed a newline
        {
            if (dc > 0 && rcp->hshift) // start of the next line: unscroll
            {
                rcp->hshift = 0;
                refresh_window(l);
            }
            vert_scroll(l, rcp->bufpos_r < oldrow);
        }
        place_cursor(l);
        return;
    }

    if (!dc && (dr == -1 || dr == 1))
    {
        // Keep the same SCREEN column while moving vertically: savecol holds
        // it (prompt included) until something other than up/down is pressed.
        int v = rcp->savecol >= 0 ? rcp->savecol : rcp->bufpos_c + ROWSTART(l, rcp->bufpos_r);

        rcp->bufpos_r += dr;
        rcp->bufpos_c = v - ROWSTART(l, rcp->bufpos_r);
        if (rcp->bufpos_c < 0) // under the prompt: first character of the row
            rcp->bufpos_c = 0;
        rcp->savecol = v;

        set_pos(l); // snaps bufpos_c to a real character boundary

        vert_scroll(l, dr == -1);
        // -ajf - place_cursor computes the correct screen position from
        // bufpos (including the prompt offset on row 0).
        place_cursor(l);
    }
    // if we ever need something other than moving one col or row - do that here:
}

static void refreshMultiLine(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);
    // -ajf - intentionally no get_screenpos here. screenpos_r/c are
    // maintained by place_cursor and by the inline cursor moves in
    // refresh_reposition. Querying the terminal on every refresh was
    // racy under autorepeating input (queued bytes got eaten by the
    // CPR read) and added a visible cursor flicker plus a synchronous
    // terminal round-trip that halved sustained arrow-key speed.

    DEBUGF(10, 15,
           "BEFORE - type: %d pos: %lu prow: %d scrdim: %dx%d, scrpos: %dx%d  bufpos: %dx%d bufdim: %dx%d linelen=%d "
           "'%c' ",
           rcp->refresh_type, l->pos, rcp->promptrow, rcp->screendim_r, rcp->screendim_c, rcp->screenpos_r,
           rcp->screenpos_c, rcp->bufpos_r, rcp->bufpos_c, rcp->bufdim_r, rcp->bufdim_c, rcp->eol - rcp->linestart,
           l->buf[l->pos] == '\n' ? '*' : l->buf[l->pos]);

    // the remembered column only survives consecutive up/down moves
    if (!(rcp->refresh_type == REFRESH_REPOSITION && rcp->delta_r))
        rcp->savecol = -1;

    switch (rcp->refresh_type)
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

    if (rcp->refresh_type != REFRESH_FULL) // just rewrote screen, no need to check
        horiz_scroll(l);

    // not used here: l->oldpos = l->pos;
    rcp->delta_c = 0;
    rcp->delta_r = 0;
    // a caller that forgets to pick a type redraws everything, not nothing
    rcp->refresh_type = REFRESH_FULL;

    DEBUGF(
        15, 15,
        "AFTER - type: %d pos: %lu prow: %d scrdim: %dx%d, scrpos: %dx%d  bufpos: %dx%d bufdim: %dx%d linelen=%d '%c' ",
        rcp->refresh_type, l->pos, rcp->promptrow, rcp->screendim_r, rcp->screendim_c, rcp->screenpos_r,
        rcp->screenpos_c, rcp->bufpos_r, rcp->bufpos_c, rcp->bufdim_r, rcp->bufdim_c, rcp->eol - rcp->linestart,
        l->buf[l->pos] == '\n' ? '*' : l->buf[l->pos]);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the current mode. */
static void refreshLine(struct linenoiseState *l)
{
    if (in_ml_paste_or_edit || force_ml_edit)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}

// -ajf - find start of the current line: position right after preceding '\n', or 0.
static size_t line_start(struct linenoiseState *l)
{
    size_t p = l->pos;
    while (p > 0 && l->buf[p - 1] != '\n')
        p--;
    return p;
}

// -ajf - find end of the current line: position of next '\n', or l->len.
static size_t line_end(struct linenoiseState *l)
{
    size_t p = l->pos;
    while (p < l->len && l->buf[p] != '\n')
        p++;
    return p;
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
static int linenoiseEditInsertN(struct linenoiseState *l, const char *s, size_t n);

int linenoiseEditInsert(struct linenoiseState *l, char c)
{
    return linenoiseEditInsertN(l, &c, 1);
}

/* Insert s[0..n) -- one whole character, or a newline -- at the cursor. */
static int linenoiseEditInsertN(struct linenoiseState *l, const char *s, size_t n)
{
    if (!n || !ensure_buf(l, l->len + n)) //-ajf
        return 0;
    {
        l->rc.refresh_type = REFRESH_LINE;
        if (n == 1 && s[0] == '\n')
        {
            rowcol *rcp = &(l->rc);
            update_promptrow(l, 1);
            WCODE("0K", 2);   // clear line
            l->rc.bufpos_r++; // update row number
            l->rc.bufpos_c = 0;
            rcp->refresh_type = REFRESH_FULL;
            rcp->hshift = 0;
        }

        memmove(l->buf + l->pos + n, l->buf + l->pos, l->len - l->pos);
        memcpy(l->buf + l->pos, s, n);
        l->len += n;
        l->pos += n;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
    return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l)
{
    if (l->pos > 0)
    {
        l->pos -= gr_prev(l->buf, l->pos, l->len); // one whole character
        l->rc.delta_c = -1;
        l->rc.refresh_type = REFRESH_REPOSITION;
        refreshLine(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l)
{
    if (l->pos != l->len)
    {
        l->pos += gr_next(l->buf, l->pos, l->len); // one whole character
        l->rc.delta_c = 1;
        l->rc.refresh_type = REFRESH_REPOSITION;
        refreshLine(l);
    }
}

/* Move cursor to the start of the line.
 * In multiline mode this is the start of the current line within the buffer;
 * otherwise it is the start of the buffer (legacy single-line behavior). */
void linenoiseEditMoveHome(struct linenoiseState *l)
{
    int ml = in_ml_paste_or_edit || force_ml_edit;
    size_t target = ml ? line_start(l) : 0;
    if (l->pos == target)
        return;
    l->pos = target;
    l->rc.hshift = 0;
    if (ml)
    {
        l->rc.refresh_type = REFRESH_LINE;
    }
    else
    {
        l->rc.bufpos_r = 0;
        l->rc.bufpos_c = 0;
        if (l->rc.promptrow < 1)
        {
            l->rc.promptrow = 1;
            WCODE("1;1H", 4);
            write_prompt(l);
        }
        l->rc.refresh_type = REFRESH_FROM_POS;
    }
    refreshLine(l);
}

/* Move cursor to the end of the line.
 * In multiline mode this is the end of the current line (just before '\n');
 * otherwise it is the end of the buffer (legacy single-line behavior). */
void linenoiseEditMoveEnd(struct linenoiseState *l)
{
    int ml = in_ml_paste_or_edit || force_ml_edit;
    size_t target = ml ? line_end(l) : l->len;
    if (l->pos == target)
        return;
    l->pos = target;
    l->rc.refresh_type = ml ? REFRESH_LINE : REFRESH_FULL;
    refreshLine(l);
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir)
{
    if (history_len > 1)
    {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0)
        {
            l->history_index = 0;
            return;
        }
        else if (l->history_index >= history_len)
        {
            l->history_index = history_len - 1;
            return;
        }
        char *h = history[history_len - 1 - l->history_index];
        set_buf(l, h, strlen(h)); // -ajf

        char *s = l->buf;
        char *e = l->buf + l->pos;
        int nl = 0;
        while (s < e) // check for newlines, if so, enter multiline
        {
            if (*s == '\n')
            {
                nl++;
                break;
            }
            s++;
        }
        l->rc.refresh_type = REFRESH_FULL;
        l->rc.hshift = 0;
        if (nl || in_ml_paste_or_edit)
            in_ml_paste_or_edit = 1;
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l)
{
    if (l->len > 0 && l->pos < l->len)
    {
        size_t k = gr_next(l->buf, l->pos, l->len); // one whole character
        l->rc.refresh_type = REFRESH_LINE;
        if (l->buf[l->pos] == '\n')
        {
            l->rc.bufdim_r -= 1;
            l->rc.refresh_type = REFRESH_FROM_POS;
        }
        memmove(l->buf + l->pos, l->buf + l->pos + k, l->len - l->pos - k);
        l->len -= k;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l)
{
    if (l->pos > 0 && l->len > 0)
    {
        rowcol *rcp = &(l->rc);
        char c = l->buf[l->pos - 1]; // the char being deleted
        size_t k = gr_prev(l->buf, l->pos, l->len); // all of it
        rcp->refresh_type = REFRESH_LINE;
        memmove(l->buf + l->pos - k, l->buf + l->pos, l->len - l->pos);
        l->pos -= k;
        l->len -= k;
        l->buf[l->len] = '\0';
        if (c == '\n')
        {
            // -ajf - deleting a '\n' joins this row with the previous one.
            // bufpos_c is not "one less" — it's the previous row's length
            // at the join point. Let get_bufpos recompute everything from
            // l->pos rather than poisoning bufpos_c with a stale --.
            get_bufpos(l);
            l->rc.refresh_type = REFRESH_FROM_POS;
        }
        else
        {
            rcp->bufpos_c--;
        }
        refreshLine(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l)
{
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos - 1] != ' ' && l->buf[l->pos - 1] != '\n')
        l->pos--;
    // at the start of a line: join it to the previous one
    if (l->pos == old_pos && l->pos > 0)
        l->pos--;
    diff = old_pos - l->pos;
    if (!diff)
        return;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    l->rc.refresh_type = REFRESH_FROM_POS;
    refreshLine(l);
}

// added so we can manually restore state -- ajf
struct linenoiseState *linenoise_lnstate;

void linenoise_refresh()
{
    if (linenoise_lnstate)
        refreshLine(linenoise_lnstate);
}

// -ajf - true when the editor is in multi-line mode (either user-forced via
// Ctrl-X, or auto-entered because the buffer contains '\n' from a paste).
// Callers (e.g. cmdline.c's completion()) use this to skip side-output that
// would collide with the multi-line render.
int linenoiseIsMultiLine(void)
{
    return in_ml_paste_or_edit || force_ml_edit;
}

/* suspend on ctrl-z - ajf 2025-10-11
 * The caller leaves raw mode first and re-enters it after we return. */
static void suspend_self(void)
{

    // ensure default action for SIGTSTP and it isn't blocked
    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);

    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &m, NULL);

    raise(SIGTSTP); // stops here; returns after `fg`
}

/* ============================ Output recording -ajf ============================
 *
 * Every byte written to STDOUT_FILENO (by linenoise, by JS printf/console.log,
 * by subprocesses inheriting our fd 1) is mirrored to the original terminal
 * AND captured in a fixed-size ring buffer sized to 2 * cols * rows. On Ctrl-Z
 * resume we clear the screen + scrollback and replay the ring, so the user
 * sees the screen as it looked before they suspended (modulo anything the
 * shell wrote during the detour, which the alt-screen approach can't avoid
 * either unless we go full-screen).
 *
 * Lazy init on first linenoiseEdit call. Can be forced earlier via
 * linenoiseEnableRecording() so output emitted before the first prompt
 * (e.g. a program banner) is also captured.
 */
#include <pthread.h>

static int    rec_inited       = 0;
static int    rec_active       = 0;
static int    rec_orig_stdout  = -1;
static int    rec_pipefd[2]    = { -1, -1 };
static int    rec_wake[2]      = { -1, -1 }; /* tells the thread to drain and stop */
static pthread_t rec_thread;
static char  *rec_buf          = NULL;
static size_t rec_cap          = 0;
static size_t rec_head         = 0;   /* next write index */
static size_t rec_len          = 0;   /* bytes currently stored */
static int    rec_redraw_on_resume = 1; /* toggled via linenoiseSetRedrawOnResume */
static pthread_mutex_t rec_lock;

static void rec_append(const char *src, size_t n)
{
    pthread_mutex_lock(&rec_lock);
    for (size_t i = 0; i < n; i++)
    {
        rec_buf[rec_head] = src[i];
        rec_head = (rec_head + 1) % rec_cap;
        if (rec_len < rec_cap)
            rec_len++;
    }
    /* If the ring just filled (and thereby overwrote bytes from the start),
     * trim leading bytes up to and including the next '\n' so a future replay
     * never starts mid-escape-sequence. */
    if (rec_len == rec_cap)
    {
        size_t tail = rec_head;            /* tail == head when full */
        size_t scanned = 0;
        while (scanned < rec_len && rec_buf[tail] != '\n')
        {
            tail = (tail + 1) % rec_cap;
            scanned++;
        }
        if (scanned < rec_len)
        {
            scanned++;                     /* drop the '\n' itself too */
            rec_len -= scanned;
        }
    }
    pthread_mutex_unlock(&rec_lock);
}

static void *recorder_thread_fn(void *unused)
{
    (void)unused;
    char buf[4096];
    int stopping = 0, budget = 1024;
    for (;;)
    {
        struct pollfd p[2] = {{.fd = rec_pipefd[0], .events = POLLIN}, {.fd = rec_wake[0], .events = POLLIN}};
        // once told to stop, keep going only while data is still waiting
        int pr = poll(p, 2, stopping ? 0 : -1);
        if (pr < 0 && errno == EINTR) // signals (SIGCONT after Ctrl-Z) must not end the thread
            continue;
        if (pr < 0 || (stopping && (pr == 0 || --budget <= 0)))
            break;
        if (p[1].revents & POLLIN)
            stopping = 1;
        if (p[0].revents & POLLIN)
        {
            ssize_t n = read(rec_pipefd[0], buf, sizeof(buf));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            (void)write(rec_orig_stdout, buf, (size_t)n);
            rec_append(buf, (size_t)n);
        }
        else if (p[0].revents & (POLLHUP | POLLERR))
            break;
        else if (stopping)
            break;
    }
    return NULL;
}

/* A forked child has no recorder thread: give it the real terminal back. */
static void rec_atfork_child(void)
{
    if (!rec_active)
        return;
    dup2(rec_orig_stdout, STDOUT_FILENO);
    close(rec_orig_stdout);
    close(rec_pipefd[0]);
    close(rec_wake[0]);
    close(rec_wake[1]);
    rec_orig_stdout = rec_pipefd[0] = rec_wake[0] = rec_wake[1] = -1;
    rec_active = 0;
}

/* Put the terminal back on fd 1 and let the recorder forward what is still
 * in the pipe.  Runs at exit; harmless to call more than once. */
void linenoiseShutdown(void)
{
    if (!rec_active)
        return;
    rec_active = 0;
    fflush(stdout);
    dup2(rec_orig_stdout, STDOUT_FILENO);
    (void)write(rec_wake[1], "q", 1);
    pthread_join(rec_thread, NULL);
}

/* The terminal's fd while stdout is redirected through the recorder, else -1.
 * For isatty()/window-size checks that mean "the user's stdout". */
int linenoiseRealStdoutFd(void)
{
    return rec_active ? rec_orig_stdout : -1;
}

/* isatty(STDOUT_FILENO) that sees through the recorder. */
int linenoiseStdoutIsTTY(void)
{
    return isatty(tty_out_fd());
}

static void ensure_recorder(int cols, int rows)
{
    if (rec_inited)
        return;
    rec_inited = 1;                        /* set early so failures don't retry */

    rec_cap = 2 * (size_t)cols * (size_t)rows;
    if (rec_cap < 4096)
        rec_cap = 4096;
    rec_buf = malloc(rec_cap);
    if (!rec_buf) { rec_inited = 0; return; }

    if (pthread_mutex_init(&rec_lock, NULL) != 0)
    {
        free(rec_buf); rec_buf = NULL; rec_inited = 0; return;
    }

    rec_orig_stdout = dup(STDOUT_FILENO);
    if (rec_orig_stdout < 0)
    {
        pthread_mutex_destroy(&rec_lock);
        free(rec_buf); rec_buf = NULL; rec_inited = 0; return;
    }

    if (pipe(rec_pipefd) != 0)
    {
        close(rec_orig_stdout); rec_orig_stdout = -1;
        pthread_mutex_destroy(&rec_lock);
        free(rec_buf); rec_buf = NULL; rec_inited = 0; return;
    }

    if (pipe(rec_wake) != 0 || dup2(rec_pipefd[1], STDOUT_FILENO) < 0)
    {
        close(rec_pipefd[0]); close(rec_pipefd[1]);
        rec_pipefd[0] = rec_pipefd[1] = -1;
        if (rec_wake[0] >= 0) { close(rec_wake[0]); close(rec_wake[1]); }
        rec_wake[0] = rec_wake[1] = -1;
        close(rec_orig_stdout); rec_orig_stdout = -1;
        pthread_mutex_destroy(&rec_lock);
        free(rec_buf); rec_buf = NULL; rec_inited = 0; return;
    }
    close(rec_pipefd[1]);
    rec_pipefd[1] = -1;

    /* only fd 1 itself should reach exec'd children */
    fcntl(rec_orig_stdout, F_SETFD, FD_CLOEXEC);
    fcntl(rec_pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(rec_wake[0], F_SETFD, FD_CLOEXEC);
    fcntl(rec_wake[1], F_SETFD, FD_CLOEXEC);

    /* stdio's line/block buffering would hide writes from us — make printf
     * etc. unbuffered so every byte hits the pipe immediately. */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (pthread_create(&rec_thread, NULL, recorder_thread_fn, NULL) != 0)
    {
        /* couldn't spawn the recorder — restore stdout and bail */
        dup2(rec_orig_stdout, STDOUT_FILENO);
        close(rec_orig_stdout); rec_orig_stdout = -1;
        close(rec_pipefd[0]); rec_pipefd[0] = -1;
        close(rec_wake[0]); close(rec_wake[1]);
        rec_wake[0] = rec_wake[1] = -1;
        pthread_mutex_destroy(&rec_lock);
        free(rec_buf); rec_buf = NULL; rec_inited = 0; return;
    }
    rec_active = 1;

    static int atfork_done = 0;
    if (!atfork_done)
    {
        pthread_atfork(NULL, NULL, rec_atfork_child);
        atfork_done = 1;
    }
}

static void replay_recording(void)
{
    if (!rec_active)
        return;
    /* clear scrollback + screen + home — bypass the pipe so we don't pollute
     * the recording with our own clears. */
    static const char clear[] = "\033[3J\033[2J\033[H";
    (void)write(rec_orig_stdout, clear, sizeof(clear) - 1);

    pthread_mutex_lock(&rec_lock);

    /* -ajf - linearize the ring and strip out any "\033[6n" (Device Status
     * Report — Cursor Position Query) sequences. Without this the terminal
     * would respond to each re-emitted query via stdin, and linenoise's main
     * loop would see the CPR responses ("\033[r;cR") as a flood of keystrokes
     * once we return. The cursor-set "\033[9999;9999H" that linenoise pairs
     * with the query is left intact — it doesn't generate a response and
     * the surrounding DECSC/DECRC bracketing restores the cursor anyway. */
    size_t tail = (rec_head + rec_cap - rec_len) % rec_cap;
    char *flat = malloc(rec_len);
    size_t flat_len = 0;
    if (flat)
    {
        int state = 0;
        for (size_t i = 0; i < rec_len; i++)
        {
            char c = rec_buf[(tail + i) % rec_cap];
            switch (state)
            {
            case 0:
                if (c == '\033') state = 1;
                else flat[flat_len++] = c;
                break;
            case 1:
                if (c == '[') state = 2;
                else { flat[flat_len++] = '\033'; flat[flat_len++] = c; state = 0; }
                break;
            case 2:
                if (c == '6') state = 3;
                else { flat[flat_len++] = '\033'; flat[flat_len++] = '['; flat[flat_len++] = c; state = 0; }
                break;
            case 3:
                if (c == 'n')      { /* drop the whole ESC[6n */ state = 0; }
                else { flat[flat_len++] = '\033'; flat[flat_len++] = '['; flat[flat_len++] = '6'; flat[flat_len++] = c; state = 0; }
                break;
            }
        }
        /* If the recording ended mid-sequence, flush the partial bytes so we
         * don't silently drop them. */
        if (state >= 1) flat[flat_len++] = '\033';
        if (state >= 2) flat[flat_len++] = '[';
        if (state >= 3) flat[flat_len++] = '6';
        (void)write(rec_orig_stdout, flat, flat_len);
        free(flat);
    }
    else
    {
        /* malloc failed — fall back to raw replay (CPR responses will leak) */
        if (tail + rec_len <= rec_cap)
            (void)write(rec_orig_stdout, rec_buf + tail, rec_len);
        else
        {
            size_t first = rec_cap - tail;
            (void)write(rec_orig_stdout, rec_buf + tail, first);
            (void)write(rec_orig_stdout, rec_buf, rec_len - first);
        }
    }

    pthread_mutex_unlock(&rec_lock);
}

/* Public: caller can force recording on before the first linenoise() call so
 * pre-prompt output is also captured. Uses a default 80x24 cap if invoked
 * before we know the real terminal size. */
void linenoiseEnableRecording(void)
{
    ensure_recorder(80, 24);
}

/* Public: toggle whether Ctrl-Z resume replays the screen recording. When
 * disabled, the Ctrl-Z handler falls back to redrawing just the prompt + buf
 * at the cursor's current row (the previous, pre-recording behavior). The
 * recording itself is still maintained — only the replay step is skipped. */
void linenoiseSetRedrawOnResume(int enable)
{
    rec_redraw_on_resume = enable ? 1 : 0;
}

/* ================================ Pasting -ajf ============================= */

/* Clean pasted bytes: CR LF and lone CR become LF, NULs are dropped, invalid
 * UTF-8 becomes U+FFFD.  Returns a malloc'd buffer of *outlen bytes. */
static char *paste_clean(const unsigned char *in, size_t n, size_t *outlen)
{
    char *out = malloc(n * 3 + 1);
    size_t i = 0, w = 0, k;
    uint32_t cp;

    *outlen = 0;
    if (!out)
        return NULL;
    while (i < n)
    {
        unsigned char c = in[i];
        if (c == '\r')
        {
            out[w++] = '\n';
            i += (i + 1 < n && in[i + 1] == '\n') ? 2 : 1;
        }
        else if (c == 0)
            i++;
        else if (c < 0x80)
            out[w++] = (char)c, i++;
        else if ((k = u8_decode((const char *)in + i, n - i, &cp)) == 1)
            memcpy(out + w, "\xEF\xBF\xBD", 3), w += 3, i++;
        else
            memcpy(out + w, in + i, k), w += k, i += k;
    }
    *outlen = w;
    return out;
}

/* Insert pasted text at the cursor.  Newlines or tabs switch the editor to
 * multi-line mode, which is the renderer that can draw them. */
static void insert_paste(struct linenoiseState *l, const char *raw, size_t rawlen)
{
    size_t n;
    char *s = paste_clean((const unsigned char *)raw, rawlen, &n);

    if (!s || !n)
    {
        free(s);
        return;
    }
    if (memchr(s, '\n', n) || memchr(s, '\t', n))
        in_ml_paste_or_edit = 1;
    /* not gonna happen, but if allocation failed — fall back to truncating */
    if (!ensure_buf(l, l->len + n))
        n = l->buflen - l->len;
    memmove(l->buf + l->pos + n, l->buf + l->pos, l->len - l->pos);
    memcpy(l->buf + l->pos, s, n);
    l->pos += n;
    l->len += n;
    l->buf[l->len] = '\0';
    free(s);

    // Redraw everything from promptrow rather than echoing the paste: that
    // stays correct when long pasted lines make the terminal scroll.
    get_bufpos(l);
    l->rc.refresh_type = REFRESH_FULL;
    refreshLine(l);
}

/* Bytes at the end of s[0..n) that start a UTF-8 sequence but do not finish it. */
static size_t u8_incomplete_tail(const unsigned char *s, size_t n)
{
    size_t k;

    for (k = 1; k <= 3 && k <= n; k++)
    {
        unsigned char c = s[n - k];
        if ((c & 0xC0) == 0x80)
            continue;
        if (c < 0xC0)
            return 0;
        return (size_t)(c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2) > k ? k : 0;
    }
    return 0;
}

/* Timing-based paste, for terminals without bracketed paste: `pre` has been
 * read already and more input is waiting.  Take everything that arrives with
 * gaps under 5ms and insert it in one go. */
static int polpaste(struct linenoiseState *l, const char *pre, size_t pren)
{
    struct abuf pb;
    int extra = 3;
    size_t tail;

    abInit(&pb);
    abAppend(&pb, pre, (int)pren);
    for (;;)
    {
        if (!inq_len)
        {
            if (in_wait(l, 5) != 1)
            {
                // a trailing CR may be half of a CR LF split across reads:
                // give the LF a little longer so the pair becomes one newline
                if (pb.len && pb.b[pb.len - 1] == '\r' && extra-- > 0)
                    continue;
                break;
            }
            if (!in_fill(l))
                break;
        }
        abAppend(&pb, (const char *)inq + inq_head, (int)inq_len);
        inq_clear();
    }
    // a character cut in half by the gap goes back to be read normally
    tail = u8_incomplete_tail((const unsigned char *)pb.b, (size_t)pb.len);
    if (tail)
    {
        inq_unread((const unsigned char *)pb.b + pb.len - tail, tail);
        pb.len -= (int)tail;
    }
    insert_paste(l, pb.b, (size_t)pb.len);
    abFree(&pb);
    // in_ml_paste_or_edit is set until we get a real ENTER
    return 1;
}

/* Bracketed paste: ESC[200~ has been read; take everything up to ESC[201~
 * verbatim.  Returns 0, or RK_EOF. */
static int bracketed_paste(struct linenoiseState *l)
{
    static const char endm[] = "\033[201~";
    const int endn = 6;
    struct abuf pb;
    int from, i, hit = -1, ret = 0;

    abInit(&pb);
    while (hit < 0)
    {
        if (!inq_len)
        {
            if (in_wait(l, 2000) != 1) // terminator never came: use what we have
                break;
            if (!in_fill(l))
            {
                ret = RK_EOF;
                break;
            }
        }
        from = pb.len - (endn - 1);
        if (from < 0)
            from = 0;
        abAppend(&pb, (const char *)inq + inq_head, (int)inq_len);
        inq_clear();
        for (i = from; i + endn <= pb.len; i++)
            if (!memcmp(pb.b + i, endm, endn))
            {
                hit = i;
                break;
            }
    }
    if (hit >= 0)
    {
        inq_unread((const unsigned char *)pb.b + hit + endn, (size_t)(pb.len - hit - endn));
        pb.len = hit;
    }
    insert_paste(l, pb.b, (size_t)pb.len);
    abFree(&pb);
    return ret;
}

/* ============================ Key handling -ajf ============================ */

static int is_wordbyte(unsigned char c)
{
    return isalnum(c) || c == '_' || c >= 0x80;
}

/* Move one word left (dir < 0) or right.  In multi-line mode a word move
 * stops at the ends of the current line; from there it steps one character,
 * which takes it across the newline. */
static void edit_move_word(struct linenoiseState *l, int dir)
{
    int ml = in_ml_paste_or_edit || force_ml_edit;
    size_t lo = ml ? line_start(l) : 0, hi = ml ? line_end(l) : l->len, p = l->pos;

    if (dir < 0)
    {
        while (p > lo && !is_wordbyte((unsigned char)l->buf[p - 1]))
            p--;
        while (p > lo && is_wordbyte((unsigned char)l->buf[p - 1]))
            p--;
    }
    else
    {
        while (p < hi && !is_wordbyte((unsigned char)l->buf[p]))
            p++;
        while (p < hi && is_wordbyte((unsigned char)l->buf[p]))
            p++;
    }
    if (p == l->pos)
    {
        if (dir < 0)
            linenoiseEditMoveLeft(l);
        else
            linenoiseEditMoveRight(l);
        return;
    }
    l->pos = p;
    l->rc.refresh_type = REFRESH_LINE;
    refreshLine(l);
}

static void edit_up(struct linenoiseState *l)
{
    if (force_ml_edit || (in_ml_paste_or_edit && l->len != l->pos))
    {
        if (l->rc.bufpos_r)
        {
            l->rc.refresh_type = REFRESH_REPOSITION;
            l->rc.delta_r = -1;
            refreshLine(l);
        }
        return;
    }
    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
}

static void edit_down(struct linenoiseState *l)
{
    if (force_ml_edit || (in_ml_paste_or_edit && l->len != l->pos))
    {
        if (l->rc.bufpos_r < l->rc.bufdim_r)
        {
            l->rc.refresh_type = REFRESH_REPOSITION;
            l->rc.delta_r = 1;
            refreshLine(l);
        }
        return;
    }
    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
}

/* An ESC has been read: parse the rest of the sequence and act on it.
 * Sequences we do not know are consumed and ignored.  A lone ESC (nothing
 * follows within 50ms) does nothing.  Returns 0, or RK_EOF. */
static int handle_escape(struct linenoiseState *l)
{
    char par[32];
    size_t n = 0;
    int k = read_key(l, 50), f, mod = 0, word;
    char *semi;

    if (k == RK_TIMEOUT)
        return 0;
    if (k < 0)
        return k;

    if (k == 'O') /* SS3: application-mode cursor keys, Home, End */
    {
        if ((f = read_key(l, 50)) < 0)
            return f == RK_TIMEOUT ? 0 : f;
    }
    else if (k == '[') /* CSI: parameter/intermediate bytes, then a final byte */
    {
        for (;;)
        {
            if ((f = read_key(l, 50)) < 0)
                return f == RK_TIMEOUT ? 0 : f;
            if (f < 0x20 || f > 0x3f)
                break;
            if (n < sizeof(par) - 1)
                par[n++] = (char)f;
        }
        if (f < 0x40 || f > 0x7e)
            return 0;
    }
    else /* Alt+key */
    {
        if (k == 'b')
            edit_move_word(l, -1);
        else if (k == 'f')
            edit_move_word(l, 1);
        else if (k == BACKSPACE || k == CTRL_H)
            linenoiseEditDeletePrevWord(l);
        return 0;
    }
    par[n] = '\0';
    if ((semi = strchr(par, ';')))
        mod = atoi(semi + 1);
    word = (mod == 3 || mod == 5); /* Alt or Ctrl held */

    switch (f)
    {
    case 'A':
        edit_up(l);
        break;
    case 'B':
        edit_down(l);
        break;
    case 'C':
        if (word)
            edit_move_word(l, 1);
        else
            linenoiseEditMoveRight(l);
        break;
    case 'D':
        if (word)
            edit_move_word(l, -1);
        else
            linenoiseEditMoveLeft(l);
        break;
    case 'H':
        linenoiseEditMoveHome(l);
        break;
    case 'F':
        linenoiseEditMoveEnd(l);
        break;
    case '~':
        switch (atoi(par))
        {
        case 1:
        case 7:
            linenoiseEditMoveHome(l);
            break;
        case 4:
        case 8:
            linenoiseEditMoveEnd(l);
            break;
        case 3:
            linenoiseEditDelete(l);
            break;
        case 200:
            return bracketed_paste(l);
        }
        break;
    }
    return 0;
}

/* `seq[0]` is a byte >= 0x80 just read: collect the rest of the UTF-8
 * sequence.  Anything malformed becomes U+FFFD so the buffer stays valid
 * UTF-8.  Returns the sequence length. */
static size_t read_u8_rest(struct linenoiseState *l, char *seq)
{
    unsigned char lead = (unsigned char)seq[0];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 0, i;
    uint32_t cp;

    for (i = 1; i < need; i++)
    {
        int k = read_key(l, 50);
        if (k < 0)
            break;
        if ((k & 0xC0) != 0x80)
        {
            unsigned char b = (unsigned char)k;
            inq_unread(&b, 1);
            break;
        }
        seq[i] = (char)k;
    }
    if (need && i == need && u8_decode(seq, need, &cp) == need)
        return need;
    memcpy(seq, "\xEF\xBF\xBD", 3);
    return 3;
}

/* The terminal changed size: measure it again and redraw. */
static void handle_resize(struct linenoiseState *l)
{
    rowcol *rcp = &(l->rc);

    get_screenpos(l);
    l->cols = rcp->screendim_c;
    if (in_ml_paste_or_edit || force_ml_edit)
    {
        get_bufpos(l);
        rcp->promptrow = rcp->screenpos_r - rcp->bufpos_r;
        rcp->hshift = 0;
        rcp->refresh_type = REFRESH_FULL;
    }
    refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static char *linenoiseEdit(int stdin_fd, int stdout_fd, size_t buflen, const char *prompt)
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
    l.promptwidth = (size_t)prompt_width(prompt, l.promptlen);
    l.oldrpos = 1;
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.maxrows = 0;
    l.history_index = 0;
    memset(&(l.rc), 0, sizeof(rowcol));
    rcp = &(l.rc);
    rcp->savecol = -1; // no remembered column
    // a previous edit may have ended by interrupt/EOF with these still set
    in_ml_paste_or_edit = 0;
    force_ml_edit = 0;
    // -ajf - sane defaults before first get_screenpos. If the initial probe
    // fails for any reason these keep us from operating on zeroed values.
    rcp->screenpos_r = 1;
    rcp->screenpos_c = 1;
    rcp->screendim_r = 24;
    rcp->screendim_c = 80;
    l.buf = malloc(buflen);
    if (!l.buf)
        return NULL;

    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--;        /* Make sure there is always space for the nulterm */
    inq_clear();       // raw mode was entered with TCSAFLUSH: start clean too
    get_screenpos(&l); // initial screen size and cursor pos
    l.cols = l.rc.screendim_c;
    rcp->promptrow = rcp->screenpos_r; // our cursor is on the prompt row at this point

    // -ajf - lazy init of the stdout recorder so Ctrl-Z resume can replay
    // what was on screen. No-op after first call.  Only when the replay is
    // wanted: with redraw-on-resume off, stdout is left completely alone.
    if (rec_redraw_on_resume)
        ensure_recorder(l.rc.screendim_c, l.rc.screendim_r);

    linenoise_lnstate = &l; //-- ajf

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

    if (write(l.ofd, prompt, l.plen) == -1)
        goto end_fail;
    // bug fix: changed write(..., 5) to write(..., 4) for correct escape sequence length - 2026-02-27
    if (write(l.ofd, "\033[0J", 4) == -1)
        goto end_fail;

    /* Eagerly create the interrupt pipe so the very first poll iteration
     * includes it. If we wait until linenoiseInterrupt() does the lazy
     * init, a wake fired before main hits poll() would only become
     * visible after the next stdin keystroke advanced the loop. The
     * pipe pair is permanent process state; one-time cost. */
    linenoise_intr_pipe_ensure();

    while (1)
    {
        char c;
        int k;

        /* One key from the input queue / terminal.  The same wait also
         * watches the interrupt pipe and the resize pipe. */
        k = read_key(&l, -1);
        if (k == RK_INTR)
        {
            errno = ECANCELED;
            goto end_fail;
        }
        if (k == RK_RESIZE)
        {
            handle_resize(&l);
            continue;
        }
        if (k < 0)
            goto end_eof;

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        // -- ajf - 2025-10-10 don't try completion if in a multiline paste
        if (!in_ml_paste_or_edit && k == TAB && completionCallback != NULL)
        {
            k = completeLine(&l);
            if (k == RK_INTR)
            {
                errno = ECANCELED;
                goto end_fail;
            }
            /* Return on errors */
            if (k < 0)
                goto end_eof;
            /* Read next character when 0 */
            if (k == 0)
                continue;
        }
        c = (char)k;
        switch (c)
        {
        // -- ajf - 2025-10-10 rewrite to handle multiline paste/edit
        case ENTER: /* enter  or \n pasted */
        {
            if (!force_ml_edit)
            {
                /* Look ahead briefly: if another byte arrives within 5ms, we assume paste */
                if (in_wait(&l, 5) == 1)
                {
                    // pass the CR itself so "\r\n" collapses to one newline
                    if (polpaste(&l, "\r", 1))
                    {
                        in_ml_paste_or_edit = 1;
                        break;
                    }
                }
            }
            // --ajf 2025-10-11 always allow newlines if not positioned at the end, or if forced
            if (force_ml_edit || (in_ml_paste_or_edit && l.len != l.pos))
            {
                linenoiseEditInsert(&l, '\n');
                break;
            }
            // poll reports no data waiting
            in_ml_paste_or_edit = 0;

            pop_scratch_history();
            linenoiseEditMoveEnd(&l);
            if (hintsCallback)
            {
                /* Force a refresh without hints to leave the previous
                 * line as the user typed it after a newline. */
                linenoiseHintsCallback *hc = hintsCallback;
                hintsCallback = NULL;
                refreshLine(&l);
                hintsCallback = hc;
            }

            return l.buf;
        }
        case CTRL_C: /* ctrl-c */
                     /* AJF 2023-07-08 -- added printf and ENODATA if line is empty and ctrl-c pressed */
// freebsd
#ifndef ENODATA
#define ENODATA 8088
#endif
            in_ml_paste_or_edit = 0;
            force_ml_edit = 0;
            // -ajf 2025-10-10 - changed to write
            (void)write(l.ofd, "^C", 2);
            if (l.len)
                errno = EAGAIN;
            else
                errno = ENODATA;
            goto end_fail;
        case BACKSPACE: /* backspace */
        case 8:         /* ctrl-h */
            linenoiseEditBackspace(&l);
            break;
        case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the
                        line is empty, act as end-of-file. */
            if (in_ml_paste_or_edit | force_ml_edit)
            {
                pop_scratch_history();
                // end of BUFFER (MoveEnd is per-line here) so the caller's
                // output starts below the last row
                if (l.pos != l.len)
                {
                    l.pos = l.len;
                    l.rc.refresh_type = REFRESH_FULL;
                    refreshLine(&l);
                }
                force_ml_edit = 0;
                if (hintsCallback)
                {
                    /* Force a refresh without hints to leave the previous
                     * line as the user typed it after a newline. */
                    linenoiseHintsCallback *hc = hintsCallback;
                    hintsCallback = NULL;
                    refreshLine(&l);
                    hintsCallback = hc;
                }
                in_ml_paste_or_edit = 0;

                return l.buf;
            }
            else if (l.len > 0)
            {
                linenoiseEditDelete(&l);
            }
            else
            {
                goto end_fail;
            }
            break;
        case CTRL_T: /* ctrl-t, swaps current character with previous. */
            if (l.pos > 0 && l.pos < l.len)
            {
                // swap the whole characters either side of the cursor
                size_t a = gr_prev(l.buf, l.pos, l.len), b = gr_next(l.buf, l.pos, l.len);
                char *tmp = malloc(a);
                if (tmp)
                {
                    memcpy(tmp, l.buf + l.pos - a, a);
                    memmove(l.buf + l.pos - a, l.buf + l.pos, b);
                    memcpy(l.buf + l.pos - a + b, tmp, a);
                    free(tmp);
                    // cursor ends after the pair, except on the last character
                    l.pos = (l.pos + b != l.len) ? l.pos + b : l.pos - a + b;
                }
                l.rc.refresh_type = REFRESH_FULL;
                refreshLine(&l);
            }
            break;
        case CTRL_B: /* ctrl-b */
            linenoiseEditMoveLeft(&l);
            break;
        case CTRL_F: /* ctrl-f */
            linenoiseEditMoveRight(&l);
            break;
        case CTRL_P: /* ctrl-p */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        // -ajf 2025-10-11
        case CTRL_X: {
            // toggle
            l.rc.refresh_type = REFRESH_FULL;
            if (force_ml_edit)
            {
                force_ml_edit = 0;
                // the single-line renderer can't draw newlines; stay multi-line
                if (memchr(l.buf, '\n', l.len))
                    in_ml_paste_or_edit = 1;
                // keep refresh using multiline version
                int old = in_ml_paste_or_edit;
                in_ml_paste_or_edit = 1;
                refreshLine(&l);
                in_ml_paste_or_edit = old;
            }
            else
            {
                force_ml_edit = 1;
                refreshLine(&l);
            }
            break;
        }
        // -ajf 2025-10-11
        case CTRL_Z:
            disableRawMode(l.ifd); // hand the shell a sane terminal
            suspend_self();        // returns after `fg`
            // -ajf - must restore raw mode on STDIN (the input fd). The pre-
            // recording version happened to work because l.ofd was the tty
            // and termios is per-tty; once we redirect stdout to a pipe,
            // tcsetattr(pipe_fd) fails silently with ENOTTY and stdin stays
            // in the shell's cooked mode — arrow keys and Enter then look
            // broken because the tty driver line-buffers them.
            enableRawMode(l.ifd);
            inq_clear();
            if (rec_active && rec_redraw_on_resume)
            {
                // -ajf - replay restores the screen content (prompt history,
                // previous output rows). The recording's last cursor position
                // may not perfectly match linenoise's bufpos state — small
                // drift here causes subsequent refresh_whole_line writes to
                // land a few cols off and confuses the next Ctrl-Z. So after
                // replay, force a refresh_full: that redraws the prompt + buf
                // at linenoise's expected position and resyncs cursor /
                // screenpos / promptrow from internal state. The rows ABOVE
                // promptrow (the history the replay restored) are preserved
                // because refresh_full only clears from promptrow downward.
                replay_recording();
                get_screenpos(&l);
                get_bufpos(&l);
                l.rc.promptrow = l.rc.screenpos_r - l.rc.bufpos_r;
                if (l.rc.promptrow < 1) l.rc.promptrow = 1;
                l.rc.refresh_type = REFRESH_FULL;
                int old_ml = in_ml_paste_or_edit;
                in_ml_paste_or_edit = 1;
                refreshLine(&l);
                in_ml_paste_or_edit = old_ml;
            }
            else
            {
                // Fallback when the recorder didn't initialize: redraw
                // prompt + buf at the current cursor row.
                get_screenpos(&l);
                l.rc.promptrow = l.rc.screenpos_r;
                l.rc.refresh_type = REFRESH_FULL;
                int old = in_ml_paste_or_edit;
                in_ml_paste_or_edit = 1;
                refreshLine(&l);
                in_ml_paste_or_edit = old;
            }
            break;
        case CTRL_N: /* ctrl-n */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
        case ESC: /* escape sequence */
            if (handle_escape(&l) < 0)
                goto end_eof;
            break;
        default:
        {
            char seq[4];
            size_t sn = 1;

            seq[0] = c;
            if ((unsigned char)c < 0x20 && c != TAB)
                break; /* control key with no binding */
            if ((unsigned char)c >= 0x80)
                sn = read_u8_rest(&l, seq); /* whole character, one refresh */
            if (force_ml_edit && in_wait(&l, 5) == 1)
            {
                if (polpaste(&l, seq, sn))
                    break;
            }
            if (linenoiseEditInsertN(&l, seq, sn))
                goto end_fail;
            break;
        }
        case CTRL_U: /* Ctrl+u, delete the current line (multiline) or whole buffer (single-line). */
            if (in_ml_paste_or_edit || force_ml_edit)
            {
                size_t s = line_start(&l), e = line_end(&l);
                if (e > s)
                {
                    memmove(l.buf + s, l.buf + e, l.len - e);
                    l.len -= (e - s);
                    l.pos = s;
                    l.buf[l.len] = '\0';
                    l.rc.hshift = 0;
                    l.rc.refresh_type = REFRESH_LINE;
                    refreshLine(&l);
                }
            }
            else
            {
                l.buf[0] = '\0';
                l.pos = l.len = 0;
                refreshLine(&l);
            }
            break;
        case CTRL_K: /* Ctrl+k, delete from cursor to end of current line. */
            if (in_ml_paste_or_edit || force_ml_edit)
            {
                size_t e = line_end(&l);
                if (e > l.pos)
                {
                    memmove(l.buf + l.pos, l.buf + e, l.len - e);
                    l.len -= (e - l.pos);
                    l.buf[l.len] = '\0';
                    l.rc.refresh_type = REFRESH_LINE;
                    refreshLine(&l);
                }
            }
            else
            {
                l.buf[l.pos] = '\0';
                l.len = l.pos;
                refreshLine(&l);
            }
            break;
        case CTRL_A: /* Ctrl+a, go to the start of the line */
            linenoiseEditMoveHome(&l);
            break;
        case CTRL_E: /* ctrl+e, go to the end of the line */
            linenoiseEditMoveEnd(&l);
            break;
        case CTRL_L: /* ctrl+l, clear screen */
            if (in_ml_paste_or_edit || force_ml_edit)
            {
                (void)write(l.ofd, "\033[2J\033[1;1H", 10);
                // -ajf - we just sent \033[1;1H, so the cursor is at (1,1).
                // No need to query the terminal for it.
                l.rc.screenpos_r = 1;
                l.rc.screenpos_c = 1;
                l.rc.promptrow=1;
                refresh_window(&l);
                break;
            }
            linenoiseClearScreen();
            l.rc.promptrow = 1;
            refreshLine(&l);
            break;
        case CTRL_W: /* ctrl+w, delete previous word */
            linenoiseEditDeletePrevWord(&l);
            break;
        }
    }

    return l.buf;

end_eof: // read error or EOF: hand back whatever was typed
    pop_scratch_history();
    return l.buf;

end_fail:

    pop_scratch_history();
    if (l.buf)
        free(l.buf);
    return NULL;
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static char *linenoiseRaw(size_t buflen, const char *prompt)
{
    char *ret = NULL; // -ajf

    if (buflen == 0)
    {
        errno = EINVAL;
        return NULL;
    }

    if (enableRawMode(STDIN_FILENO) == -1)
        return NULL;
    winch_install();
    ret = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buflen, prompt);
    {
        int e = errno; // the caller reads errno to tell ctrl-c/EOF/wake apart
        winch_restore();
        linenoise_lnstate = NULL; //-ajf
        disableRawMode(STDIN_FILENO);
        printf("\n");
        errno = e;
    }
    return ret;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoiseNoTTY(void)
{
    char *line = NULL;
    size_t len = 0, maxlen = 0;

    while (1)
    {
        if (len == maxlen)
        {
            if (maxlen == 0)
                maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line, maxlen);
            if (line == NULL)
            {
                if (oldval)
                    free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n')
        {
            if (c == EOF && len == 0)
            {
                free(line);
                return NULL;
            }
            else
            {
                line[len] = '\0';
                return line;
            }
        }
        else
        {
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
char *linenoise(const char *prompt)
{

    if (!isatty(STDIN_FILENO))
    {
        /* Not a tty: read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoiseNoTTY();
    }
    else if (isUnsupportedTerm())
    {
        char buf[LINENOISE_MAX_LINE];
        size_t len;

        printf("%s", prompt);
        fflush(stdout);
        if (fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL)
            return NULL;
        len = strlen(buf);
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    }
    else
    {
        // - ajf - now returns malloc'd buffer or NULL
        return linenoiseRaw(LINENOISE_INITIAL_LINE, prompt);
    }
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoiseFree(void *ptr)
{
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(void)
{
    if (history)
    {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void)
{
    disableRawMode(STDIN_FILENO);
    linenoiseShutdown();
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

static int lhAdd_to(char *line, int take_ownership)
{
    char *linecopy;

    if (history_max_len == 0)
        return 0;

    /* Initialization on first call. */
    if (history == NULL)
    {
        history = malloc(sizeof(char *) * history_max_len);
        if (history == NULL)
            return 0;
        memset(history, 0, (sizeof(char *) * history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len - 1], line))
    {
        if (take_ownership)
            free(line);
        return 0;
    }

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    if (take_ownership) //--ajf
        linecopy = line;
    else
        linecopy = strdup(line);

    if (!linecopy)
        return 0;
    if (history_len == history_max_len)
    {
        free(history[0]);
        memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
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

// -ajf - read access to the process-global history buffer. The returned
// pointers are owned by linenoise; do not free or hold across operations
// that may modify history (linenoiseHistoryAdd/Set/Load/Save, arrow-key
// scrolling).
int linenoiseHistoryLen(void)
{
    return history_len;
}

const char *linenoiseHistoryGet(int idx)
{
    if (!history || idx < 0 || idx >= history_len)
        return NULL;
    return history[idx];
}

// -ajf - drop all in-memory history entries. Allocation is freed so the
// next History*-add reallocs as needed. history_max_len is preserved.
void linenoiseHistoryClear(void)
{
    if (history)
    {
        int j;
        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
        history = NULL;
    }
    history_len = 0;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len)
{
    char **new;

    if (len < 1)
        return 0;
    if (history)
    {
        int tocopy = history_len;

        new = malloc(sizeof(char *) * len);
        if (new == NULL)
            return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy)
        {
            int j;

            for (j = 0; j < tocopy - len; j++)
                free(history[j]);
            tocopy = len;
        }
        memset(new, 0, sizeof(char *) * len);
        memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
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
    while (*p)
    {
        if (*p == s)
            *p = r;
        p++;
    }
}

#define PLACEHOLDER_CHAR 0x01

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename)
{
    mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
    FILE *fp;
    int j;

    fp = fopen(filename, "w");
    umask(old_umask);
    if (fp == NULL)
        return -1;
    chmod(filename, S_IRUSR | S_IWUSR);
    for (j = 0; j < history_len; j++)
    {
        strchr_rep(history[j], '\n', PLACEHOLDER_CHAR);
        fprintf(fp, "%s\n", history[j]);
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
int linenoiseHistoryLoad(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    char *buf = NULL;
    size_t buflen = 0;
    ssize_t nread;

    if (fp == NULL)
        return -1;

    while ((nread = getline(&buf, &buflen, fp)) != -1)
    { // --ajf
        // strip the line ending only: a CR inside the entry is content
        while (nread > 0 && (buf[nread - 1] == '\n' || buf[nread - 1] == '\r'))
            buf[--nread] = '\0';
        strchr_rep(buf, PLACEHOLDER_CHAR, '\n'); // -ajf
        lhAdd_to(buf, 1);
        buf = NULL;
        buflen = 0;
    }
    if (buf)
        free(buf);
    fclose(fp);
    return 0;
}
