#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

/* Public Readline API. Resolve these symbols from the running Bash. */
extern FILE *rl_instream, *rl_outstream;
extern char *rl_line_buffer;
extern int rl_point, rl_end;
extern int rl_add_defun(const char *, int (*)(int, int), int);
extern int rl_stuff_char(int);
extern void rl_get_screen_size(int *, int *);

/* Bash's loadable builtin ABI. The argument list is opaque here. */
struct word_list;
struct bash_builtin {
    char *name;
    int (*function)(struct word_list *);
    int flags;
    char *const *long_doc;
    const char *short_doc;
    char *handle;
};

static int read_byte(int fd, int timeout_ms)
{
    struct pollfd ready = {fd, POLLIN, 0};
    unsigned char byte;

    if (poll(&ready, 1, timeout_ms) <= 0 || !(ready.revents & POLLIN))
        return -1;
    return read(fd, &byte, 1) == 1 ? byte : -1;
}

/* Preserve keystrokes that arrive while waiting for the CPR response. */
static int cursor_position(int fd, int *row, int *column)
{
    char input[64] = {0};
    int length = 0;
    int escape = -1;

    if (fputs("\033[6n", rl_outstream) == EOF || fflush(rl_outstream) == EOF)
        return 0;

    while (length < (int)sizeof input - 1) {
        int byte = read_byte(fd, length == 0 ? 500 : 100);
        if (byte < 0)
            break;

        if (byte == '\033')
            escape = length;
        input[length++] = (char)byte;
        if (byte == 'R' && escape >= 0) {
            int consumed = 0;
            if (sscanf(input + escape, "\033[%5d;%5dR%n", row, column, &consumed) == 2
                && consumed == length - escape) {
                for (int i = 0; i < escape; ++i)
                    rl_stuff_char((unsigned char)input[i]);
                return 1;
            }
        }
    }

    for (int i = 0; i < length; ++i)
        rl_stuff_char((unsigned char)input[i]);
    return 0;
}

static int mouse_click(int count, int key)
{
    (void)count;
    (void)key;

    int fd = fileno(rl_instream);
    char report[19] = {0};
    int column, row, cursor_column, cursor_row, rows, columns;
    int consumed = 0;

    /* Readline matched ESC [ <. Drain presses, releases and wheel events. */
    for (int i = 0; i < (int)sizeof report - 1; ++i) {
        int byte = read_byte(fd, 100);
        if (byte < 0)
            return 0;
        report[i] = (char)byte;
        if (byte == 'm' || byte == 'M')
            break;
        if ((byte < '0' || byte > '9') && byte != ';') {
            rl_stuff_char(byte);
            return 0;
        }
    }

    /* Act on an unmodified left release, so its press can't race our query. */
    if (sscanf(report, "0;%5d;%5dm%n", &column, &row, &consumed) != 2
        || consumed == 0 || rl_end == 0)
        return 0;

    for (int i = 0; i < rl_end; ++i) {
        unsigned char byte = (unsigned char)rl_line_buffer[i];
        if (byte < ' ' || byte > '~')
            return 0;
    }

    rl_get_screen_size(&rows, &columns);
    if (row < 1 || row > rows || column < 1 || column > columns
        || !cursor_position(fd, &cursor_row, &cursor_column)
        || cursor_row < 1 || cursor_row > rows
        || cursor_column < 1 || cursor_column > columns)
        return 0;

    /* The current cursor supplies the origin without parsing the prompt. */
    long target = rl_point + (row - cursor_row) * (long)columns + column - cursor_column;
    if (target >= 0 && target <= rl_end)
        rl_point = (int)target;
    return 0;
}

static int register_mouse_click(struct word_list *args)
{
    (void)args;
    return rl_add_defun("bash-mouse-position-click", mouse_click, -1) == 0 ? 0 : 1;
}

static char *const help[] = {"Register the Bash mouse-position Readline command.", NULL};

struct bash_builtin _bmp_register_struct = {
    "_bmp_register", register_mouse_click, 1, help, "register mouse positioning", NULL
};
