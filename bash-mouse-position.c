#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Public Readline API. Resolve these symbols from the running Bash. */
extern FILE *rl_instream, *rl_outstream;
extern char *rl_line_buffer;
extern int rl_point, rl_end, rl_mark;
extern int rl_kill_text(int, int);
extern int rl_copy_region_to_kill(int, int);
extern int rl_ding(void);
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

enum { input_capacity = 1024 };

static long long monotonic_ms(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

/* Collect input arriving before the CPR response for replay after the click. */
static int cursor_position(int fd, int *row, int *column, char input[input_capacity])
{
    int length = 0;
    int escape = -1;
    long long deadline = monotonic_ms() + 500;

    if (fputs("\033[6n", rl_outstream) == EOF || fflush(rl_outstream) == EOF)
        return 0;

    while (length < input_capacity - 1) {
        int timeout = (int)(deadline - monotonic_ms());
        if (timeout <= 0)
            break;
        int byte = read_byte(fd, timeout);
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

static int decode_legacy(const unsigned char data[3], int *button, int *row,
                         int *column, char *event)
{
    if (data[0] < 32 || data[1] < 33 || data[2] < 33)
        return 0;
    *button = data[0] - 32;
    *column = data[1] - 32;
    *row = data[2] - 32;
    *event = 'M';
    if (*button == 3) {
        *button = 0;
        *event = 'm';
    }
    return 1;
}

static int decode_sgr(const char *data, int *button, int *row, int *column,
                      char *event)
{
    int consumed = 0;
    if (sscanf(data, "%3d;%5d;%5d%c%n", button, column, row, event, &consumed) != 4 ||
        (*event != 'm' && *event != 'M'))
        return 0;
    return consumed;
}

/* A report interrupted by the cursor-query deadline can still have a tail on
   the tty. Consume the framing here so its digits never enter the command. */
static int read_mouse_report(int fd, int legacy, int *button, int *row, int *column,
                             char *event)
{
    int first = read_byte(fd, 100);
    if (first < 0)
        return 0;
    if (legacy && first != '<') {
        int second = read_byte(fd, 100);
        int third = read_byte(fd, 100);
        if (second < 0 || third < 0)
            return 0;
        unsigned char bytes[3] = {first, second, third};
        return decode_legacy(bytes, button, row, column, event);
    }

    char data[32] = {0};
    int length = 0;
    if (!legacy)
        data[length++] = (char)first;
    while (length < (int)sizeof data - 1) {
        if (length && (data[length - 1] == 'M' || data[length - 1] == 'm'))
            break;
        int byte = read_byte(fd, 100);
        if (byte < 0)
            break;
        if (byte == '\033') {
            int csi = read_byte(fd, 100);
            int kind = csi == '[' ? read_byte(fd, 100) : -1;
            if (kind != '<')
                return 0;
            length = 0;  /* Discard the damaged report and resume at the nested one. */
        } else {
            data[length++] = (char)byte;
        }
    }
    data[length] = '\0';
    return decode_sgr(data, button, row, column, event);
}

static void replay_input(int fd, const char *input)
{
    while (*input) {
        int button, column, row;
        char event;
        if (strncmp(input, "\033[<", 3) == 0 || strncmp(input, "\033[M", 3) == 0) {
            int legacy = input[2] == 'M' && input[3] != '<';
            input += input[2] == 'M' && input[3] == '<' ? 4 : 3;
            if (legacy && strlen(input) >= 3) {
                if (decode_legacy((const unsigned char *)input, &button, &row, &column, &event))
                    drag_event(button, event, row, column);
                input += 3;
            } else if (!legacy) {
                int consumed = decode_sgr(input, &button, &row, &column, &event);
                if (consumed) {
                    drag_event(button, event, row, column);
                    input += consumed;
                } else {
                    input += strspn(input, "0123456789;[<");
                    if (*input == 'm' || *input == 'M')
                        ++input;
                    else if (!*input)
                        read_mouse_report(fd, 0, &button, &row, &column, &event);
                }
            } else {
                for (size_t i = strlen(input); i < 3; ++i)
                    read_byte(fd, 100);
                return;
            }
        } else {
            rl_stuff_char((unsigned char)*input++);
        }
    }
}

static int mouse_click(int count, int key)
{
    (void)count;

    int fd = fileno(rl_instream);
    int button, column, row, cursor_column, cursor_row, rows, columns;
    char event;

    if (!read_mouse_report(fd, key == 'M', &button, &row, &column, &event) ||
        (button != 0 && button != 32) || rl_end == 0)
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
        replay_input(fd, pending);
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
    replay_input(fd, pending);
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

static int copy_selection(int count, int key)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int start = rl_mark < rl_point ? rl_mark : rl_point;
    int end = rl_mark > rl_point ? rl_mark : rl_point;
    const char *sty = getenv("STY");
    int in_screen = sty && *sty;

    if (start < 0 || end > rl_end || start == end)
        return 0;

    rl_copy_region_to_kill(count, key);
    /* Screen 4.08 has a 768-byte DCS buffer, including OSC framing. */
    if (in_screen && end - start > 560)
        return rl_ding();

    if (in_screen)
        fputs("\033P", rl_outstream);
    fputs("\033]52;c;", rl_outstream);
    for (int i = start; i < end; i += 3) {
        unsigned int a = (unsigned char)rl_line_buffer[i];
        unsigned int b = i + 1 < end ? (unsigned char)rl_line_buffer[i + 1] : 0;
        unsigned int c = i + 2 < end ? (unsigned char)rl_line_buffer[i + 2] : 0;
        fputc(alphabet[a >> 2], rl_outstream);
        fputc(alphabet[((a & 3) << 4) | (b >> 4)], rl_outstream);
        fputc(i + 1 < end ? alphabet[((b & 15) << 2) | (c >> 6)] : '=', rl_outstream);
        fputc(i + 2 < end ? alphabet[c & 63] : '=', rl_outstream);
    }
    fputc('\a', rl_outstream);
    if (in_screen)
        fputs("\033\\", rl_outstream);
    fflush(rl_outstream);
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
    if (rl_add_defun("bash-mouse-position-clear", clear_command, -1) != 0)
        return 1;
    return rl_add_defun("bash-mouse-position-copy", copy_selection, -1) != 0;
}

static char *const help[] = {"Register the Bash mouse-position Readline command.", NULL};

struct bash_builtin _bmp_register_struct = {
    "_bmp_register", register_mouse_click, 1, help, "register mouse positioning", NULL
};
