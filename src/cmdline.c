#include "duktape/core/duktape.h"
#include "duktape/register.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define RP_REPL_GREETING             \
    "      |>>            |>>\n"     \
    "    __|__          __|__\n"     \
    "   \\  |  /         \\   /\n"   \
    "    | ^ |          | ^ |  \n"   \
    "  __| o |__________| o |__\n"   \
    " [__|_|__|(rp)|  | |______]\n"  \
    "_[|||||||||||||__|||||||||]_\n" \
    "powered by Duktape " DUK_GIT_DESCRIBE

#define RP_REPL_PREFIX "rp> "
#define RP_REPL_MAX_LINE_SIZE 32768
#define RP_REPL_HISTORY_LENGTH 256
struct repl_history
{
    char *buffer[RP_REPL_HISTORY_LENGTH];
    int size;
};

static void handle_input(struct repl_history *history)
{
    int cursor_pos = 0;
    int buffer_end = 0;
    int cur_char;
    int cur_history_idx = history->size - 1;
    while ((cur_char = getchar()) != '\n')
    {
        if (cur_char == '\033')
        {
            getchar();
            int esc_char = getchar();
            switch (esc_char)
            {
            case 'A':
            {
                // arrow down
                if (cur_history_idx > 0)
                {
                    cur_history_idx--;
                    buffer_end = strlen(history->buffer[cur_history_idx]);
                    // clear line, move to col 1, print repl prefix
                    printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                    // print out buffer from history
                    printf("%s", history->buffer[cur_history_idx]);
                    // move to correct position and update cursor pos
                    cursor_pos = cursor_pos > buffer_end ? buffer_end : cursor_pos;
                    printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
                }
                break;
            }
            case 'B':
                // arrow up
                if (cur_history_idx + 1 < history->size)
                {
                    cur_history_idx++;
                    buffer_end = strlen(history->buffer[cur_history_idx]);
                    // clear line, move to col 1, print repl prefix
                    printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                    // print out buffer from history
                    printf("%s", history->buffer[cur_history_idx]);
                    // move to correct position and update cursor pos
                    cursor_pos = cursor_pos > buffer_end ? buffer_end : cursor_pos;
                    printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
                }
                break;
            case 'C':
                if (cursor_pos < buffer_end)
                {
                    printf("%s", "\033[1C");
                    cursor_pos++;
                }
                // code for arrow right
                break;
            case 'D':
                if (cursor_pos > 0)
                {
                    printf("%s", "\033[1D");
                    cursor_pos--;
                }
                // code for arrow left
                break;
            }
        }
        else
        {
            char *line_buffer = history->buffer[cur_history_idx];
            line_buffer[buffer_end] = cur_char;
            if (cur_char == '\177')
            {
                // delete character
                if (cursor_pos <= 0)
                    continue;
                printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                memmove(line_buffer + cursor_pos - 1, line_buffer + cursor_pos,
                        buffer_end - cursor_pos + 1);
                line_buffer[buffer_end] = '\0';
                buffer_end--;
                printf("%.*s", buffer_end, line_buffer);
                cursor_pos--;
                printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
            }
            else
            {
                if (cursor_pos >= RP_REPL_MAX_LINE_SIZE)
                    continue;

                printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                memmove(line_buffer + cursor_pos + 1, line_buffer + cursor_pos,
                        buffer_end - cursor_pos);
                line_buffer[cursor_pos] = cur_char;
                buffer_end++;
                printf("%.*s", buffer_end, line_buffer);
                cursor_pos++;
                printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
            }
        }
    }
    history->buffer[cur_history_idx][buffer_end] = '\0';
    printf("\n");
    // copy any changed history into current line
    memcpy(history->buffer[history->size - 1], history->buffer[cur_history_idx],
           strlen(history->buffer[cur_history_idx]));
}

static int repl(duk_context *ctx)
{
    struct repl_history history;
    history.size = 0;

    printf("%s", RP_REPL_GREETING);
    putchar('\n');

    while (1)
    {
        printf("%s", RP_REPL_PREFIX);

        char *line = malloc(RP_REPL_MAX_LINE_SIZE);
        if (history.size < RP_REPL_HISTORY_LENGTH)
        {
            history.buffer[history.size] = line;
            history.size++;
        }
        else
        {
            // pop last from buffer
            free(history.buffer[0]);
            int i = 0;
            for (i = 0; i < RP_REPL_HISTORY_LENGTH; i++)
            {
                history.buffer[i] = history.buffer[i + 1];
            }
            history.buffer[RP_REPL_HISTORY_LENGTH - 1] = line;
        }
        handle_input(&history);

        // ignore empty input
        if (strlen(line) == 0)
        {
            continue;
        }

        // line too long
        if (line[RP_REPL_MAX_LINE_SIZE - 1] == '\n')
        {
            printf("Line too long. The max line size is %d", RP_REPL_MAX_LINE_SIZE);
            continue;
        }

        // evaluate input
        duk_push_string(ctx, line);
        if (duk_peval(ctx) != 0)
        {
            printf("%s\n", duk_safe_to_string(ctx, -1));
        }
        else
        {
            printf("%s\n", duk_safe_to_stacktrace(ctx, -1));
        }
        duk_pop(ctx);
    }
}

int main(int argc, char *argv[])
{
    duk_context *ctx = duk_create_heap_default();
    if (!ctx)
    {
        printf("could not create duktape context\n");
        return 1;
    }

    duk_init_context(ctx);

    if (argc == 1)
    {
        // store old terminal settings
        struct termios old_tio, new_tio;
        tcgetattr(STDIN_FILENO, &old_tio);
        new_tio = old_tio;

        // disable buffered output and echo
        new_tio.c_lflag &= (~ICANON & ~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
        int ret = repl(ctx);
        // restore terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        return ret;
    }
    else if (argc == 2)
    {
        duk_push_string(ctx, "require('");
        duk_push_string(ctx, argv[1]);
        duk_push_string(ctx, "');");
        duk_concat(ctx, 3);
        if (duk_peval(ctx) == DUK_EXEC_ERROR)
        {
            printf("%s\n", duk_safe_to_stacktrace(ctx, -1));
        }
    }

    return 0;
}
