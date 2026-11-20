#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

/* Public Readline API. Resolve these symbols from the running Bash. */
extern FILE *rl_instream, *rl_outstream;
extern char *rl_line_buffer;
extern int rl_point, rl_end, rl_mark;
extern int rl_kill_text(int, int);
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

enum { input_capacity = 64 };

/* Collect input arriving before the CPR response for replay after the click. */
static int cursor_position(int fd, int *row, int *column, char input[input_capacity])
{
    int length = 0;
    int escape = -1;

    if (fputs("\033[6n", rl_outstream) == EOF || fflush(rl_outstream) == EOF)
        return 0;

    while (length < input_capacity - 1) {
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
                input[escape] = '\0';
                return 1;
            }
        }
    }

    return 0;
}

static void (*activate_mark)(void);
static void (*deactivate_mark)(void);
static int drag_width, anchor, anchor_row, anchor_column;

static void drag_event(int button, char event, int row, int column)
{
    int rows, columns;
    rl_get_screen_size(&rows, &columns);
    if ((button != 0 && button != 32) || !drag_width)
        return;
    if (event == 'm') {
        if (rl_mark != rl_point && activate_mark)
            activate_mark();
        drag_width = 0;
        return;
    }
    if (button != 32 || drag_width != columns || anchor > rl_end ||
        row < 1 || row > rows || column < 1 || column > columns)
        return;

    long target = anchor + (row - anchor_row) * (long)columns + column - anchor_column;
    if (target >= 0 && target <= rl_end) {
        rl_mark = anchor;
        rl_point = (int)target;
        if (activate_mark && rl_mark != rl_point)
            activate_mark();
    }
}

static void replay_input(const char *input)
{
    while (*input) {
        int button, column, row, consumed = 0;
        char event;
        if (sscanf(input, "\033[<%3d;%5d;%5d%c%n", &button, &column, &row, &event,
                   &consumed) == 4 && consumed > 0 && (event == 'm' || event == 'M')) {
            drag_event(button, event, row, column);
            input += consumed;
        } else {
            rl_stuff_char((unsigned char)*input++);
        }
    }
}

static int mouse_click(int count, int key)
{
    (void)count;
    (void)key;

    int fd = fileno(rl_instream);
    char report[19] = {0};
    int button, column, row, cursor_column, cursor_row, rows, columns;
    char event;
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

    if (sscanf(report, "%3d;%5d;%5d%c%n", &button, &column, &row, &event, &consumed) != 4
        || consumed == 0 || (button != 0 && button != 32) || rl_end == 0)
        return 0;

    if (event == 'm' || button == 32) {
        drag_event(button, event, row, column);
        return 0;
    }

    for (int i = 0; i < rl_end; ++i) {
        unsigned char byte = (unsigned char)rl_line_buffer[i];
        if (byte < ' ' || byte > '~')
            return 0;
    }

    rl_get_screen_size(&rows, &columns);
    if (row < 1 || row > rows || column < 1 || column > columns)
        return 0;

    drag_width = 0;
    if (deactivate_mark)
        deactivate_mark();
    char pending[input_capacity] = {0};
    if (!cursor_position(fd, &cursor_row, &cursor_column, pending)
        || cursor_row < 1 || cursor_row > rows
        || cursor_column < 1 || cursor_column > columns) {
        replay_input(pending);
        return 0;
    }

    /* The current cursor supplies the origin without parsing the prompt. */
    long target = rl_point + (row - cursor_row) * (long)columns + column - cursor_column;
    if (target >= 0 && target <= rl_end) {
        rl_point = rl_mark = anchor = (int)target;
        anchor_row = row;
        anchor_column = column;
        drag_width = columns;
    }
    replay_input(pending);
    return 0;
}

static int clear_command(int count, int key)
{
    (void)count;
    (void)key;
    rl_kill_text(0, rl_end);
    rl_point = rl_mark = drag_width = 0;
    if (deactivate_mark)
        deactivate_mark();
    return 0;
}

static int register_mouse_click(struct word_list *args)
{
    (void)args;
    /* Older Readline versions can edit the region without highlighting it. */
    void *process = dlopen(NULL, RTLD_LAZY);
    if (process) {
        activate_mark = dlsym(process, "rl_activate_mark");
        deactivate_mark = dlsym(process, "rl_deactivate_mark");
        dlclose(process);
    }
    if (rl_add_defun("bash-mouse-position-click", mouse_click, -1) != 0)
        return 1;
    return rl_add_defun("bash-mouse-position-clear", clear_command, -1) != 0;
}

static char *const help[] = {"Register the Bash mouse-position Readline command.", NULL};

struct bash_builtin _bmp_register_struct = {
    "_bmp_register", register_mouse_click, 1, help, "register mouse positioning", NULL
};
