#ifndef JCOLOR
#define JCOLOR

#define COLORS   1
#define NO_COLORS 0

#define C_RESET_DEF   "\x1b[0m"
#define C_KEY_DEF     "\x1b[1;34m"  /* bold blue */
#define C_STRING_DEF  "\x1b[32m"    /* green */
#define C_NUMBER_DEF  "\x1b[36m"    /* cyan */
#define C_BOOL_DEF    "\x1b[33m"    /* yellow */
#define C_NULL_DEF    "\x1b[35m"    /* magenta */

/* Normal colors */
#define C_BLACK            "\x1b[30m"
#define C_RED              "\x1b[31m"
#define C_GREEN            "\x1b[32m"
#define C_YELLOW           "\x1b[33m"
#define C_BLUE             "\x1b[34m"
#define C_MAGENTA          "\x1b[35m"
#define C_CYAN             "\x1b[36m"
#define C_WHITE            "\x1b[37m"

/* Bold (bright) versions of the above */
#define C_BOLD_BLACK       "\x1b[1;30m"
#define C_BOLD_RED         "\x1b[1;31m"
#define C_BOLD_GREEN       "\x1b[1;32m"
#define C_BOLD_YELLOW      "\x1b[1;33m"
#define C_BOLD_BLUE        "\x1b[1;34m"
#define C_BOLD_MAGENTA     "\x1b[1;35m"
#define C_BOLD_CYAN        "\x1b[1;36m"
#define C_BOLD_WHITE       "\x1b[1;37m"

/* Bright colors (90–97) */
#define C_BRIGHT_BLACK     "\x1b[90m" /* gray */
#define C_BRIGHT_RED       "\x1b[91m"
#define C_BRIGHT_GREEN     "\x1b[92m"
#define C_BRIGHT_YELLOW    "\x1b[93m"
#define C_BRIGHT_BLUE      "\x1b[94m"
#define C_BRIGHT_MAGENTA   "\x1b[95m"
#define C_BRIGHT_CYAN      "\x1b[96m"
#define C_BRIGHT_WHITE     "\x1b[97m"

/* Bold bright colors (same as bright + bold, though some terminals treat 90–97 as already bright) */
#define C_BOLD_BRIGHT_BLACK   "\x1b[1;90m"
#define C_BOLD_BRIGHT_RED     "\x1b[1;91m"
#define C_BOLD_BRIGHT_GREEN   "\x1b[1;92m"
#define C_BOLD_BRIGHT_YELLOW  "\x1b[1;93m"
#define C_BOLD_BRIGHT_BLUE    "\x1b[1;94m"
#define C_BOLD_BRIGHT_MAGENTA "\x1b[1;95m"
#define C_BOLD_BRIGHT_CYAN    "\x1b[1;96m"
#define C_BOLD_BRIGHT_WHITE   "\x1b[1;97m"

/* Selected color set based on flag */
typedef struct {
    const char *RESET, *KEY, *STRING, *NUMBER, *BOOL, *NULLC;
} Jcolors;

char *print_json_colored(const char *json_text, int indent_spaces, int colors_flag, Jcolors *Cp);

#endif
