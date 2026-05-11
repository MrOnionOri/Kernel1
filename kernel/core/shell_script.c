#include "shell_script.h"

#include "terminal.h"
#include "vfs.h"

#include <stdint.h>

#define SHELL_SCRIPT_SIZE 512
#define SHELL_MAX_SOURCE_DEPTH 2
#define SHELL_COMMAND_BUFFER_SIZE 80
#define SHELL_PATH_SIZE 64

static int shell_source_depth;
static int shell_script_should_stop;
static char shell_script_arg_values[SHELL_SCRIPT_ARG_COUNT][SHELL_SCRIPT_ARG_SIZE];

static int string_equals(const char* left, const char* right) {
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static int string_starts_with(const char* text, const char* prefix) {
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }

        index++;
    }

    return 1;
}

static size_t string_length(const char* text) {
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static void string_copy(char* destination, const char* source, size_t size) {
    size_t index = 0;

    if (size == 0) {
        return;
    }

    while (index < size - 1 && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static uint32_t string_to_uint(const char* text, int* ok) {
    uint32_t value = 0;
    size_t index = 0;

    *ok = 0;

    while (text[index] == ' ') {
        index++;
    }

    if (text[index] == '\0') {
        return 0;
    }

    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }

        value = value * 10 + (uint32_t)(text[index] - '0');
        index++;
    }

    *ok = 1;
    return value;
}

static const char* skip_spaces(const char* text) {
    while (*text == ' ') {
        text++;
    }

    return text;
}

static int shell_is_token_boundary(char character) {
    return character == '\0' || character == ' ';
}

static size_t shell_find_do_token(const char* text) {
    size_t index = 0;
    int in_quote = 0;
    char quote = '\0';

    while (text[index] != '\0') {
        if (in_quote) {
            if (text[index] == quote) {
                in_quote = 0;
            }
            index++;
            continue;
        }

        if (text[index] == '"' || text[index] == '\'') {
            in_quote = 1;
            quote = text[index++];
            continue;
        }

        if ((index == 0 || text[index - 1] == ' ') &&
                text[index] == 'd' && text[index + 1] == 'o' &&
                shell_is_token_boundary(text[index + 2])) {
            return index;
        }

        index++;
    }

    return (size_t)-1;
}

static size_t shell_read_word(const char* text, size_t limit, char* output, size_t size) {
    size_t read = 0;
    size_t write = 0;

    while (read < limit && text[read] == ' ') {
        read++;
    }

    if (read >= limit || size == 0) {
        if (size > 0) {
            output[0] = '\0';
        }
        return read;
    }

    if (text[read] == '"' || text[read] == '\'') {
        char quote = text[read++];
        while (read < limit && text[read] != quote && write < size - 1) {
            output[write++] = text[read++];
        }

        while (read < limit && text[read] != quote) {
            read++;
        }

        if (read < limit && text[read] == quote) {
            read++;
        }
    } else {
        while (read < limit && text[read] != ' ' && write < size - 1) {
            output[write++] = text[read++];
        }

        while (read < limit && text[read] != ' ') {
            read++;
        }
    }

    output[write] = '\0';
    return read;
}

static void shell_run_inline_command(const struct shell_line* line, size_t start,
        int* last_status, shell_script_run_command_fn run_command) {
    char command[SHELL_COMMAND_BUFFER_SIZE];

    shell_join_args(line, start, command, sizeof(command));

    if (command[0] == '\0') {
        *last_status = 1;
        return;
    }

    run_command(command);
}

static int shell_handle_source_command(const struct shell_line* line, int* last_status,
        shell_script_run_command_fn run_command, shell_script_resolve_path_fn resolve_path) {
    if (line->count < 2) {
        terminal_write("source: usage source <file>\n");
        *last_status = 1;
        return 1;
    }

    if (shell_source_depth >= SHELL_MAX_SOURCE_DEPTH) {
        terminal_write("source: nesting too deep\n");
        *last_status = 1;
        return 1;
    }

    char path[SHELL_PATH_SIZE];
    resolve_path(line->args[1], path, sizeof(path));
    int fd = vfs_open(path);

    if (fd == VFS_INVALID_FD) {
        terminal_write("source: file not found: ");
        terminal_write(path);
        terminal_write("\n");
        *last_status = 1;
        return 1;
    }

    char script[SHELL_SCRIPT_SIZE + 1];
    int32_t bytes = vfs_read(fd, script, SHELL_SCRIPT_SIZE);
    vfs_close(fd);

    if (bytes < 0) {
        terminal_write("source: read failed: ");
        terminal_write(path);
        terminal_write("\n");
        *last_status = 1;
        return 1;
    }

    script[bytes] = '\0';

    char saved_args[SHELL_SCRIPT_ARG_COUNT][SHELL_SCRIPT_ARG_SIZE];
    for (size_t i = 0; i < SHELL_SCRIPT_ARG_COUNT; i++) {
        string_copy(saved_args[i], shell_script_arg_values[i], sizeof(saved_args[i]));
        shell_script_arg_values[i][0] = '\0';
    }

    string_copy(shell_script_arg_values[0], path, sizeof(shell_script_arg_values[0]));
    for (size_t i = 1; i < SHELL_SCRIPT_ARG_COUNT && i + 1 < line->count; i++) {
        string_copy(shell_script_arg_values[i], line->args[i + 1], sizeof(shell_script_arg_values[i]));
    }

    int saved_stop = shell_script_should_stop;
    shell_script_should_stop = 0;
    shell_source_depth++;

    size_t read = 0;
    while (script[read] != '\0' && !shell_script_should_stop) {
        char line_buffer[SHELL_COMMAND_BUFFER_SIZE];
        size_t index = 0;

        while (script[read] == '\n' || script[read] == '\r') {
            read++;
        }

        if (script[read] == '\0') {
            break;
        }

        while (script[read] != '\0' && script[read] != '\n' && script[read] != '\r' &&
                index < sizeof(line_buffer) - 1) {
            line_buffer[index++] = script[read++];
        }

        line_buffer[index] = '\0';

        if (line_buffer[0] == '\0' || line_buffer[0] == '#') {
            continue;
        }

        terminal_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        terminal_write("+ ");
        terminal_write(line_buffer);
        terminal_write("\n");
        terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        run_command(line_buffer);
    }

    shell_source_depth--;
    shell_script_should_stop = saved_stop;
    for (size_t i = 0; i < SHELL_SCRIPT_ARG_COUNT; i++) {
        string_copy(shell_script_arg_values[i], saved_args[i], sizeof(shell_script_arg_values[i]));
    }

    return 1;
}

void shell_script_initialize(void) {
    shell_source_depth = 0;
    shell_script_should_stop = 0;

    for (size_t i = 0; i < SHELL_SCRIPT_ARG_COUNT; i++) {
        shell_script_arg_values[i][0] = '\0';
    }
}

char (*shell_script_args(void))[SHELL_SCRIPT_ARG_SIZE] {
    return shell_script_arg_values;
}

int shell_script_handle_foreach(const char* input, int* last_status,
        shell_script_run_command_fn run_command) {
    const char* text = skip_spaces(input);

    if (!string_starts_with(text, "foreach") || !shell_is_token_boundary(text[7])) {
        return 0;
    }

    text = skip_spaces(text + 7);

    char variable[SHELL_VAR_NAME_SIZE];
    size_t var_read = shell_read_word(text, string_length(text), variable, sizeof(variable));

    if (!shell_env_name_is_valid(variable)) {
        terminal_write("foreach: invalid variable name\n");
        *last_status = 1;
        return 1;
    }

    text = skip_spaces(text + var_read);
    size_t do_index = shell_find_do_token(text);

    if (do_index == (size_t)-1 || do_index == 0) {
        terminal_write("foreach: usage foreach <var> <items...> do <command>\n");
        *last_status = 1;
        return 1;
    }

    char saved_value[SHELL_VAR_VALUE_SIZE];
    int had_value = shell_env_find_var(variable) >= 0;
    string_copy(saved_value, shell_env_get_var(variable), sizeof(saved_value));

    const char* command = skip_spaces(text + do_index + 2);

    if (command[0] == '\0') {
        terminal_write("foreach: missing command after do\n");
        *last_status = 1;
        return 1;
    }

    size_t item_read = 0;
    while (item_read < do_index) {
        char item[SHELL_VAR_VALUE_SIZE];
        item_read += shell_read_word(text + item_read, do_index - item_read, item, sizeof(item));

        if (item[0] == '\0') {
            break;
        }

        if (!shell_env_set_var(variable, item)) {
            *last_status = 1;
            break;
        }

        run_command(command);

        if (shell_script_should_stop) {
            break;
        }
    }

    if (had_value) {
        shell_env_set_var(variable, saved_value);
    } else {
        shell_env_unset_var(variable);
    }

    return 1;
}

int shell_script_handle_control_line(const struct shell_line* line, int* last_status,
        shell_script_run_command_fn run_command, shell_script_resolve_path_fn resolve_path) {
    if (string_equals(line->args[0], "ifset") || string_equals(line->args[0], "ifnotset")) {
        if (line->count < 3) {
            terminal_write(line->args[0]);
            terminal_write(": usage ");
            terminal_write(line->args[0]);
            terminal_write(" <var> <command>\n");
            *last_status = 1;
            return 1;
        }

        int exists = shell_env_find_var(line->args[1]) >= 0;
        int should_run = string_equals(line->args[0], "ifset") ? exists : !exists;

        if (should_run) {
            shell_run_inline_command(line, 2, last_status, run_command);
        } else {
            *last_status = 1;
        }
        return 1;
    }

    if (string_equals(line->args[0], "ifeq") || string_equals(line->args[0], "ifneq")) {
        if (line->count < 4) {
            terminal_write(line->args[0]);
            terminal_write(": usage ");
            terminal_write(line->args[0]);
            terminal_write(" <left> <right> <command>\n");
            *last_status = 1;
            return 1;
        }

        int equal = string_equals(line->args[1], line->args[2]);
        int should_run = string_equals(line->args[0], "ifeq") ? equal : !equal;

        if (should_run) {
            shell_run_inline_command(line, 3, last_status, run_command);
        } else {
            *last_status = 1;
        }
        return 1;
    }

    if (string_equals(line->args[0], "exit")) {
        if (shell_source_depth == 0) {
            terminal_write("exit: only valid inside source scripts\n");
            *last_status = 1;
            return 1;
        }

        if (line->count > 1) {
            int ok;
            *last_status = (int)string_to_uint(line->args[1], &ok);
            if (!ok) {
                terminal_write("exit: invalid code\n");
                *last_status = 1;
            }
        } else {
            *last_status = 0;
        }

        shell_script_should_stop = 1;
        return 1;
    }

    if (string_equals(line->args[0], "source")) {
        return shell_handle_source_command(line, last_status, run_command, resolve_path);
    }

    return 0;
}
