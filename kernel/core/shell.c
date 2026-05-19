#include "shell.h"

#include "initrd.h"
#include "terminal.h"
#include "shell_apps.h"
#include "shell_env.h"
#include "shell_fs.h"
#include "shell_parser.h"
#include "shell_script.h"
#include "shell_system.h"
#include "vfs.h"

#include <stddef.h>

#define COMMAND_BUFFER_SIZE 80
#define COMMAND_HISTORY_SIZE 8

static char command_buffer[COMMAND_BUFFER_SIZE];
static size_t command_length;
static size_t command_cursor;
static int command_pending;
static char command_history[COMMAND_HISTORY_SIZE][COMMAND_BUFFER_SIZE];
static size_t command_history_count;
static size_t command_history_view;
static int shell_last_status;

static const char* shell_commands[] = {
    "about",
    "alloc",
    "alias",
    "aliases",
    "apps",
    "appinfo",
    "append",
    "cat",
    "cd",
    "clear",
    "cp",
    "df",
    "diskinfo",
    "diskfree",
    "diskread",
    "diskwrite",
    "echo",
    "env",
    "exit",
    "foreach",
    "gdt",
    "heap",
    "help",
    "ifeq",
    "ifneq",
    "ifnotset",
    "ifset",
    "initrd",
    "kapp",
    "kfscat",
    "kfscheck",
    "kfsformat",
    "kfsinfo",
    "kfsls",
    "kfsrm",
    "kfssave",
    "kfsstat",
    "kill",
    "kmalloc",
    "ls",
    "mem",
    "mkdir",
    "mv",
    "paging",
    "pmm",
    "ps",
    "pwd",
    "reap",
    "ring3",
    "run",
    "runall",
    "rm",
    "set",
    "source",
    "spawn",
    "stat",
    "tasks",
    "tasksv",
    "ticks",
    "touch",
    "tree",
    "vmmtest",
    "wait",
    "which",
    "write",
    "unset",
    "unalias",
};

static void shell_execute_command(void);

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

static void shell_prompt(void) {
    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("kernel1> ");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void shell_clear_buffer(void) {
    for (size_t i = 0; i < COMMAND_BUFFER_SIZE; i++) {
        command_buffer[i] = '\0';
    }

    command_length = 0;
    command_cursor = 0;
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

static const char* skip_spaces(const char* text) {
    while (*text == ' ') {
        text++;
    }

    return text;
}

static void shell_copy_alias_value(const char* value, char* output, size_t size) {
    value = skip_spaces(value);

    if (value[0] == '"' || value[0] == '\'') {
        size_t index = 0;
        size_t read = 1;
        char quote = value[0];

        while (value[read] != '\0' && value[read] != quote && index < size - 1) {
            output[index++] = value[read++];
        }

        if (value[read] == quote) {
            read++;
        }

        while (value[read] != '\0' && index < size - 1) {
            output[index++] = value[read++];
        }

        output[index] = '\0';
        return;
    }

    string_copy(output, value, size);
}

static int shell_handle_alias_definition(const char* input) {
    const char* text = skip_spaces(input + 5);
    char name[SHELL_ALIAS_NAME_SIZE];
    char value[SHELL_ALIAS_VALUE_SIZE];
    size_t index = 0;

    if (*text == '\0') {
        shell_env_print_aliases();
        shell_last_status = 0;
        return 1;
    }

    while (text[index] != '\0' && text[index] != ' ' && index < sizeof(name) - 1) {
        name[index] = text[index];
        index++;
    }

    name[index] = '\0';
    text = skip_spaces(text + index);
    shell_copy_alias_value(text, value, sizeof(value));
    shell_last_status = shell_env_set_alias(name, value) ? 0 : 1;
    return 1;
}

static void shell_replace_buffer(const char* text) {
    while (command_cursor < command_length) {
        terminal_cursor_right();
        command_cursor++;
    }

    while (command_length > 0) {
        command_length--;
        command_buffer[command_length] = '\0';
        terminal_backspace();
    }

    string_copy(command_buffer, text, COMMAND_BUFFER_SIZE);
    command_length = 0;

    while (command_buffer[command_length] != '\0') {
        terminal_putchar(command_buffer[command_length]);
        command_length++;
    }

    command_cursor = command_length;
}

static void shell_redraw_from_cursor(int erase_extra) {
    size_t start = command_cursor;
    size_t printed = 0;

    for (size_t i = start; i < command_length; i++) {
        terminal_putchar(command_buffer[i]);
        printed++;
    }

    if (erase_extra) {
        terminal_putchar(' ');
        printed++;
    }

    while (printed > 0) {
        terminal_cursor_left();
        printed--;
    }
}

static void shell_insert_char(char character) {
    if (command_length >= COMMAND_BUFFER_SIZE - 1) {
        return;
    }

    if (command_cursor == command_length) {
        command_buffer[command_length++] = character;
        command_buffer[command_length] = '\0';
        command_cursor = command_length;
        terminal_putchar(character);
        return;
    }

    for (size_t i = command_length; i > command_cursor; i--) {
        command_buffer[i] = command_buffer[i - 1];
    }

    command_buffer[command_cursor] = character;
    command_length++;
    command_buffer[command_length] = '\0';
    shell_redraw_from_cursor(0);
    terminal_cursor_right();
    command_cursor++;
}

static void shell_backspace_char(void) {
    if (command_cursor == 0) {
        return;
    }

    command_cursor--;

    for (size_t i = command_cursor; i < command_length; i++) {
        command_buffer[i] = command_buffer[i + 1];
    }

    command_length--;
    terminal_cursor_left();
    shell_redraw_from_cursor(1);
}

static void shell_history_add(const char* command) {
    if (command[0] == '\0') {
        return;
    }

    if (command_history_count > 0 &&
            string_equals(command_history[(command_history_count - 1) % COMMAND_HISTORY_SIZE], command)) {
        command_history_view = command_history_count;
        return;
    }

    string_copy(command_history[command_history_count % COMMAND_HISTORY_SIZE], command, COMMAND_BUFFER_SIZE);
    command_history_count++;
    command_history_view = command_history_count;
}

static const char* shell_find_command_prefix(const char* prefix) {
    const char* match = 0;

    for (size_t i = 0; i < sizeof(shell_commands) / sizeof(shell_commands[0]); i++) {
        if (!string_starts_with(shell_commands[i], prefix)) {
            continue;
        }

        if (match != 0) {
            return 0;
        }

        match = shell_commands[i];
    }

    return match;
}

static int shell_complete_path_command(const char* command_prefix) {
    size_t prefix_length = 0;

    while (command_prefix[prefix_length] != '\0') {
        prefix_length++;
    }

    if (!string_starts_with(command_buffer, command_prefix)) {
        return 0;
    }

    char resolved[SHELL_FS_PATH_SIZE];
    char completed_path[SHELL_FS_PATH_SIZE];
    const char* partial = command_buffer + prefix_length;

    shell_fs_resolve_path(partial, resolved, sizeof(resolved));

    if (!vfs_complete_path(resolved, completed_path, sizeof(completed_path))) {
        return 1;
    }

    char completed_command[COMMAND_BUFFER_SIZE];
    string_copy(completed_command, command_prefix, sizeof(completed_command));

    size_t index = prefix_length;
    size_t source = 0;

    const char* current_directory = shell_fs_current_directory();
    if (current_directory[0] != '\0' &&
            string_starts_with(completed_path, current_directory) &&
            completed_path[prefix_length == 0 ? 0 : 0] != '\0') {
        size_t dir_length = 0;

        while (current_directory[dir_length] != '\0') {
            dir_length++;
        }

        if (completed_path[dir_length] == '/') {
            source = dir_length + 1;
        }
    }

    while (index < sizeof(completed_command) - 1 && completed_path[source] != '\0') {
        completed_command[index++] = completed_path[source++];
    }

    completed_command[index] = '\0';
    shell_replace_buffer(completed_command);
    return 1;
}

static int shell_complete_last_path_argument(const char* command_prefix) {
    size_t prefix_length = 0;
    size_t argument_start;

    while (command_prefix[prefix_length] != '\0') {
        prefix_length++;
    }

    if (!string_starts_with(command_buffer, command_prefix)) {
        return 0;
    }

    argument_start = command_length;
    while (argument_start > prefix_length && command_buffer[argument_start - 1] != ' ') {
        argument_start--;
    }

    char resolved[SHELL_FS_PATH_SIZE];
    char completed_path[SHELL_FS_PATH_SIZE];
    shell_fs_resolve_path(command_buffer + argument_start, resolved, sizeof(resolved));

    if (!vfs_complete_path(resolved, completed_path, sizeof(completed_path))) {
        return 1;
    }

    char completed_command[COMMAND_BUFFER_SIZE];
    size_t index = 0;
    size_t source = 0;

    while (index < argument_start && index < sizeof(completed_command) - 1) {
        completed_command[index] = command_buffer[index];
        index++;
    }

    const char* current_directory = shell_fs_current_directory();
    if (current_directory[0] != '\0' &&
            string_starts_with(completed_path, current_directory)) {
        size_t dir_length = 0;

        while (current_directory[dir_length] != '\0') {
            dir_length++;
        }

        if (completed_path[dir_length] == '/') {
            source = dir_length + 1;
        }
    }

    while (index < sizeof(completed_command) - 1 && completed_path[source] != '\0') {
        completed_command[index++] = completed_path[source++];
    }

    completed_command[index] = '\0';
    shell_replace_buffer(completed_command);
    return 1;
}

static void shell_run_command_text(const char* command) {
    if (command[0] == '\0') {
        shell_last_status = 1;
        return;
    }

    string_copy(command_buffer, command, sizeof(command_buffer));
    command_length = string_length(command_buffer);
    command_cursor = command_length;
    shell_execute_command();
}

static void shell_execute_command(void) {
    command_buffer[command_length] = '\0';
    struct shell_line line;
    char aliased[COMMAND_BUFFER_SIZE];
    char expanded[COMMAND_BUFFER_SIZE];

    if (command_length == 0) {
        return;
    }

    shell_history_add(command_buffer);

    if (string_equals(command_buffer, "alias") || string_starts_with(command_buffer, "alias ")) {
        shell_handle_alias_definition(command_buffer);
        return;
    }

    shell_env_expand_alias(command_buffer, aliased, sizeof(aliased));

    if (shell_script_handle_foreach(aliased, &shell_last_status, shell_run_command_text)) {
        return;
    }

    shell_env_expand_variables(aliased, expanded, sizeof(expanded),
        shell_last_status, shell_script_args());

    if (!shell_parse_line(expanded, &line)) {
        terminal_write("shell: parse error\n");
        shell_last_status = 1;
        return;
    }

    if (line.count == 0) {
        return;
    }

    if (shell_script_handle_control_line(&line, &shell_last_status,
            shell_run_command_text, shell_fs_resolve_path)) {
        return;
    }

    if (string_equals(line.args[0], "echo")) {
        char text[COMMAND_BUFFER_SIZE];
        char output[COMMAND_BUFFER_SIZE];
        shell_join_args(&line, 1, text, sizeof(text));
        string_copy(output, text, sizeof(output));
        size_t length = string_length(output);
        if (length < sizeof(output) - 1) {
            output[length++] = '\n';
            output[length] = '\0';
        }

        if (!shell_fs_write_redirect(&line, output)) {
            terminal_write(output);
        }
        shell_last_status = 0;
        return;
    } else if (string_equals(line.args[0], "history")) {
        size_t start = command_history_count > COMMAND_HISTORY_SIZE ?
            command_history_count - COMMAND_HISTORY_SIZE : 0;

        for (size_t i = start; i < command_history_count; i++) {
            terminal_write_dec((uint32_t)(i + 1));
            terminal_write("  ");
            terminal_write(command_history[i % COMMAND_HISTORY_SIZE]);
            terminal_write("\n");
        }
        shell_last_status = 0;
        return;
    } else if (string_equals(line.args[0], "aliases")) {
        shell_env_print_aliases();
        shell_last_status = 0;
        return;
    } else if (string_equals(line.args[0], "unalias")) {
        if (line.count < 2) {
            terminal_write("unalias: usage unalias <name>\n");
            shell_last_status = 1;
        } else {
            shell_env_unset_alias(line.args[1]);
            shell_last_status = 0;
        }
        return;
    } else if (string_equals(line.args[0], "env")) {
        shell_env_print_vars();
        shell_last_status = 0;
        return;
    } else if (string_equals(line.args[0], "set")) {
        if (line.count < 2) {
            shell_env_print_vars();
            shell_last_status = 0;
        } else {
            char value[COMMAND_BUFFER_SIZE];
            shell_join_args(&line, 2, value, sizeof(value));
            shell_last_status = shell_env_set_var(line.args[1], value) ? 0 : 1;
        }
        return;
    } else if (string_equals(line.args[0], "unset")) {
        if (line.count < 2) {
            terminal_write("unset: usage unset <name>\n");
            shell_last_status = 1;
        } else {
            shell_env_unset_var(line.args[1]);
            shell_last_status = 0;
        }
        return;
    } else if (string_equals(line.args[0], "initrd") && line.count > 2 && string_equals(line.args[1], "cat")) {
        initrd_cat(line.args[2]);
        shell_last_status = 0;
        return;
    }

    if (shell_apps_handle_line(&line, &shell_last_status)) {
        return;
    }

    if (shell_fs_handle_line(&line, &shell_last_status)) {
        return;
    }

    if (shell_system_handle_line(&line, &shell_last_status)) {
        return;
    }

    if (string_equals(command_buffer, "help")) {
        terminal_write("Commands: help, history, env, set <name> <value>, unset <name>, alias <name> <cmd>, unalias <name>, aliases, source <file> [args], exit <code>, foreach <var> <items...> do <cmd>, ifset <var> <cmd>, ifnotset <var> <cmd>, ifeq <a> <b> <cmd>, ifneq <a> <b> <cmd>, echo <text> [> file|>> file], clear, pwd, cd <dir>, ls, tree, cat <file>, stat <path>, cp <src> <dst>, mv <src> <dst>, mkdir [-p] <dir>, touch <file>, write <file> <text>, append <file> <text>, rm [-r] <path>, /disk persistent files, kapp <file>, df, diskinfo, diskread <lba>, diskwrite <lba> <text>, kfsformat, kfscheck [-v], kfsinfo, kfsls, kfssave <name> <text>, kfscat <name>, kfsstat <name>, kfsrm <name>, ticks, mem, pmm, alloc, heap, kmalloc, paging, vmmtest, gdt, ring3, apps, appinfo <app>, which <app>, initrd, spawn <app>, run <app>, runall, kill <id>, wait <id>, reap, ps, ps -v, tasks, tasksv, about\n");
    } else if (string_equals(command_buffer, "clear")) {
        terminal_initialize();
        terminal_write("Kernel1 shell\n");
    } else if (string_equals(command_buffer, "initrd")) {
        initrd_print_info();
    } else if (string_equals(command_buffer, "initrd ls")) {
        initrd_list();
    } else if (string_starts_with(command_buffer, "initrd cat ")) {
        initrd_cat(command_buffer + 11);
    } else {
        terminal_write("Unknown command: ");
        terminal_write(command_buffer);
        terminal_write("\n");
        shell_last_status = 1;
    }
}

void shell_initialize(void) {
    shell_clear_buffer();
    command_pending = 0;
    command_history_count = 0;
    command_history_view = 0;
    shell_last_status = 0;

    shell_env_initialize();
    shell_fs_initialize();
    shell_script_initialize();
    terminal_write("Type 'help' for commands.\n");
    shell_prompt();
}

void shell_put_char(char character) {
    if (command_pending) {
        return;
    }

    if (character == '\n') {
        while (command_cursor < command_length) {
            terminal_cursor_right();
            command_cursor++;
        }

        terminal_putchar('\n');
        command_pending = 1;
        return;
    }

    if (character == '\b') {
        shell_backspace_char();
        return;
    }

    shell_insert_char(character);
}

void shell_cursor_left(void) {
    if (command_pending || command_cursor == 0) {
        return;
    }

    command_cursor--;
    terminal_cursor_left();
}

void shell_cursor_right(void) {
    if (command_pending || command_cursor >= command_length) {
        return;
    }

    command_cursor++;
    terminal_cursor_right();
}

void shell_cursor_home(void) {
    while (!command_pending && command_cursor > 0) {
        shell_cursor_left();
    }
}

void shell_cursor_end(void) {
    while (!command_pending && command_cursor < command_length) {
        shell_cursor_right();
    }
}

void shell_delete_char(void) {
    if (command_pending || command_cursor >= command_length) {
        return;
    }

    for (size_t i = command_cursor; i < command_length; i++) {
        command_buffer[i] = command_buffer[i + 1];
    }

    command_length--;
    shell_redraw_from_cursor(1);
}

void shell_complete(void) {
    if (command_pending || command_length == 0) {
        return;
    }

    command_buffer[command_length] = '\0';

    if (shell_complete_last_path_argument("append ") ||
            shell_complete_path_command("cat ") ||
            shell_complete_last_path_argument("cp ") ||
            shell_complete_path_command("kapp ") ||
            shell_complete_path_command("ls ") ||
            shell_complete_path_command("cd ") ||
            shell_complete_last_path_argument("mv ") ||
            shell_complete_last_path_argument("rm ") ||
            shell_complete_path_command("source ") ||
            shell_complete_path_command("stat ") ||
            shell_complete_path_command("tree ") ||
            shell_complete_path_command("touch ") ||
            shell_complete_last_path_argument("write ") ||
            shell_complete_path_command("initrd cat ")) {
        return;
    }

    char completed_app[COMMAND_BUFFER_SIZE];
    if (shell_apps_complete(command_buffer, completed_app, sizeof(completed_app))) {
        if (completed_app[0] != '\0') {
            shell_replace_buffer(completed_app);
        }
        return;
    }

    const char* command = shell_find_command_prefix(command_buffer);
    if (command != 0) {
        shell_replace_buffer(command);
    }
}

void shell_history_previous(void) {
    if (command_pending || command_history_count == 0 || command_history_view == 0) {
        return;
    }

    command_history_view--;
    shell_replace_buffer(command_history[command_history_view % COMMAND_HISTORY_SIZE]);
}

void shell_history_next(void) {
    if (command_pending || command_history_count == 0 || command_history_view >= command_history_count) {
        return;
    }

    command_history_view++;

    if (command_history_view == command_history_count) {
        shell_replace_buffer("");
    } else {
        shell_replace_buffer(command_history[command_history_view % COMMAND_HISTORY_SIZE]);
    }
}

void shell_poll(void) {
    if (!command_pending) {
        return;
    }

    shell_execute_command();
    shell_clear_buffer();
    command_pending = 0;
    shell_prompt();
}
