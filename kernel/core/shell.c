#include "shell.h"

#include "app.h"
#include "arch.h"
#include "heap.h"
#include "initrd.h"
#include "kapp.h"
#include "memory_map.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "user_mode.h"
#include "vfs.h"

#include <stddef.h>

#define COMMAND_BUFFER_SIZE 80
#define COMMAND_HISTORY_SIZE 8
#define SHELL_PATH_SIZE 64

static char command_buffer[COMMAND_BUFFER_SIZE];
static size_t command_length;
static size_t command_cursor;
static int command_pending;
static char command_history[COMMAND_HISTORY_SIZE][COMMAND_BUFFER_SIZE];
static size_t command_history_count;
static size_t command_history_view;
static char current_directory[SHELL_PATH_SIZE];

static const char* shell_commands[] = {
    "about",
    "alloc",
    "apps",
    "appinfo",
    "cat",
    "cd",
    "clear",
    "cp",
    "gdt",
    "heap",
    "help",
    "initrd",
    "kapp",
    "kmalloc",
    "ls",
    "mem",
    "mkdir",
    "mv",
    "paging",
    "pmm",
    "pwd",
    "ring3",
    "run",
    "runall",
    "rm",
    "spawn",
    "stat",
    "tasks",
    "tasksv",
    "ticks",
    "tree",
    "vmmtest",
    "which",
    "write",
};

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

static void shell_resolve_path(const char* input, char* output, size_t size) {
    size_t index = 0;
    size_t source = 0;

    if (size == 0) {
        return;
    }

    if (input[0] == '/') {
        source = 1;
    } else if (current_directory[0] != '\0') {
        while (index < size - 1 && current_directory[index] != '\0') {
            output[index] = current_directory[index];
            index++;
        }

        if (index < size - 1) {
            output[index++] = '/';
        }
    }

    while (index < size - 1 && input[source] != '\0') {
        output[index++] = input[source++];
    }

    output[index] = '\0';
}

static void shell_change_directory(const char* path) {
    if (string_equals(path, "/")) {
        current_directory[0] = '\0';
        return;
    }

    if (string_equals(path, "..")) {
        current_directory[0] = '\0';
        return;
    }

    char resolved[SHELL_PATH_SIZE];
    shell_resolve_path(path, resolved, sizeof(resolved));

    if (vfs_is_directory(resolved)) {
        if (string_equals(resolved, "/")) {
            current_directory[0] = '\0';
        } else if (resolved[0] == '/') {
            string_copy(current_directory, resolved + 1, sizeof(current_directory));
        } else {
            string_copy(current_directory, resolved, sizeof(current_directory));
        }

        return;
    }

    terminal_write("Directory not found: ");
    terminal_write(path);
    terminal_write("\n");
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

    char resolved[SHELL_PATH_SIZE];
    char completed_path[SHELL_PATH_SIZE];
    const char* partial = command_buffer + prefix_length;

    shell_resolve_path(partial, resolved, sizeof(resolved));

    if (!vfs_complete_path(resolved, completed_path, sizeof(completed_path))) {
        return 1;
    }

    char completed_command[COMMAND_BUFFER_SIZE];
    string_copy(completed_command, command_prefix, sizeof(completed_command));

    size_t index = prefix_length;
    size_t source = 0;

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

static struct task* shell_spawn_app_text(const char* app_text) {
    app_text = skip_spaces(app_text);

    char app_name[24];
    size_t index = 0;

    while (index < sizeof(app_name) - 1 && app_text[index] != '\0' && app_text[index] != ' ') {
        app_name[index] = app_text[index];
        index++;
    }

    app_name[index] = '\0';

    if (app_name[0] == '\0') {
        terminal_write("No app given\n");
        return 0;
    }

    const char* args = skip_spaces(app_text + index);
    const struct app_descriptor* app = app_find(app_name);
    enum app_kind kind = app_manifest_kind(app_name);

    if (kind == APP_KIND_BUILT_IN) {
        if (app == 0) {
            terminal_write("Built-in app missing: ");
            terminal_write(app_name);
            terminal_write("\n");
            return 0;
        }

        return user_mode_spawn_app_with_args(app->name, app->entry, args);
    }

    if (kind == APP_KIND_KAPP) {
        struct task* task = kapp_spawn_app(app_name, args);
        if (task != 0) {
            return task;
        }

        terminal_write("KAPP app failed: ");
        terminal_write(app_name);
        terminal_write("\n");
        return 0;
    }

    if (app != 0) {
        return user_mode_spawn_app_with_args(app->name, app->entry, args);
    }

    struct task* task = kapp_spawn_app(app_name, args);
    if (task != 0) {
        return task;
    }

    terminal_write("Unknown app: ");
    terminal_write(app_name);
    terminal_write("\n");
    return 0;
}

static void shell_run_foreground(struct task* task) {
    terminal_ensure_rows(6);

    while (task != 0 && task->state == TASK_READY) {
        task_run(task);
    }
}

static int shell_complete_app_command(const char* command_prefix) {
    size_t prefix_length = 0;

    while (command_prefix[prefix_length] != '\0') {
        prefix_length++;
    }

    if (!string_starts_with(command_buffer, command_prefix)) {
        return 0;
    }

    const char* app_prefix = command_buffer + prefix_length;
    const struct app_descriptor* app = app_find_prefix(app_prefix);

    if (app != 0) {
        char completed[COMMAND_BUFFER_SIZE];
        string_copy(completed, command_prefix, sizeof(completed));

        size_t index = prefix_length;
        size_t app_index = 0;
        while (index < sizeof(completed) - 1 && app->name[app_index] != '\0') {
            completed[index++] = app->name[app_index++];
        }
        completed[index] = '\0';
        shell_replace_buffer(completed);
    }

    return 1;
}

static void shell_execute_command(void) {
    command_buffer[command_length] = '\0';

    if (command_length == 0) {
        return;
    }

    shell_history_add(command_buffer);

    if (string_equals(command_buffer, "help")) {
        terminal_write("Commands: help, clear, pwd, cd <dir>, ls, tree, cat <file>, stat <path>, cp <src> <dst>, mv <src> <dst>, mkdir <dir>, write <file> <text>, rm <path>, kapp <file>, ticks, mem, pmm, alloc, heap, kmalloc, paging, vmmtest, gdt, ring3, apps, appinfo <app>, which <app>, initrd, spawn <app>, run <app>, runall, tasks, tasksv, about\n");
    } else if (string_equals(command_buffer, "clear")) {
        terminal_initialize();
        terminal_write("Kernel1 shell\n");
    } else if (string_equals(command_buffer, "pwd")) {
        terminal_write("/");
        terminal_write(current_directory);
        terminal_write("\n");
    } else if (string_starts_with(command_buffer, "cd ")) {
        shell_change_directory(command_buffer + 3);
    } else if (string_equals(command_buffer, "ls")) {
        vfs_list_path(current_directory);
    } else if (string_starts_with(command_buffer, "ls ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(command_buffer + 3, path, sizeof(path));
        vfs_list_path(path);
    } else if (string_equals(command_buffer, "tree")) {
        vfs_tree(current_directory);
    } else if (string_starts_with(command_buffer, "tree ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(skip_spaces(command_buffer + 5), path, sizeof(path));
        vfs_tree(path);
    } else if (string_starts_with(command_buffer, "cat ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(command_buffer + 4, path, sizeof(path));
        vfs_cat(path);
    } else if (string_starts_with(command_buffer, "stat ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(skip_spaces(command_buffer + 5), path, sizeof(path));
        vfs_stat(path);
    } else if (string_starts_with(command_buffer, "cp ")) {
        const char* text = skip_spaces(command_buffer + 3);
        char source[SHELL_PATH_SIZE];
        char destination[SHELL_PATH_SIZE];
        char resolved_source[SHELL_PATH_SIZE];
        char resolved_destination[SHELL_PATH_SIZE];
        size_t index = 0;

        while (index < sizeof(source) - 1 && text[index] != '\0' && text[index] != ' ') {
            source[index] = text[index];
            index++;
        }

        source[index] = '\0';
        text = skip_spaces(text + index);
        index = 0;

        while (index < sizeof(destination) - 1 && text[index] != '\0' && text[index] != ' ') {
            destination[index] = text[index];
            index++;
        }

        destination[index] = '\0';

        if (source[0] == '\0' || destination[0] == '\0') {
            terminal_write("cp: usage cp <src> <dst>\n");
        } else {
            shell_resolve_path(source, resolved_source, sizeof(resolved_source));
            shell_resolve_path(destination, resolved_destination, sizeof(resolved_destination));

            if (vfs_copy(resolved_source, resolved_destination)) {
                terminal_write("Copied ");
                terminal_write(resolved_source);
                terminal_write(" -> ");
                terminal_write(resolved_destination);
                terminal_write("\n");
            }
        }
    } else if (string_starts_with(command_buffer, "mv ")) {
        const char* text = skip_spaces(command_buffer + 3);
        char source[SHELL_PATH_SIZE];
        char destination[SHELL_PATH_SIZE];
        char resolved_source[SHELL_PATH_SIZE];
        char resolved_destination[SHELL_PATH_SIZE];
        size_t index = 0;

        while (index < sizeof(source) - 1 && text[index] != '\0' && text[index] != ' ') {
            source[index] = text[index];
            index++;
        }

        source[index] = '\0';
        text = skip_spaces(text + index);
        index = 0;

        while (index < sizeof(destination) - 1 && text[index] != '\0' && text[index] != ' ') {
            destination[index] = text[index];
            index++;
        }

        destination[index] = '\0';

        if (source[0] == '\0' || destination[0] == '\0') {
            terminal_write("mv: usage mv <src> <dst>\n");
        } else {
            shell_resolve_path(source, resolved_source, sizeof(resolved_source));
            shell_resolve_path(destination, resolved_destination, sizeof(resolved_destination));

            if (vfs_move(resolved_source, resolved_destination)) {
                terminal_write("Moved ");
                terminal_write(resolved_source);
                terminal_write(" -> ");
                terminal_write(resolved_destination);
                terminal_write("\n");
            }
        }
    } else if (string_starts_with(command_buffer, "mkdir ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(skip_spaces(command_buffer + 6), path, sizeof(path));
        if (vfs_mkdir(path)) {
            terminal_write("Created directory: ");
            terminal_write(path);
            terminal_write("\n");
        }
    } else if (string_starts_with(command_buffer, "write ")) {
        const char* text = skip_spaces(command_buffer + 6);
        char path[SHELL_PATH_SIZE];
        size_t index = 0;

        while (index < sizeof(path) - 1 && text[index] != '\0' && text[index] != ' ') {
            path[index] = text[index];
            index++;
        }

        path[index] = '\0';
        text = skip_spaces(text + index);

        if (path[0] == '\0') {
            terminal_write("write: missing path\n");
        } else {
            char resolved[SHELL_PATH_SIZE];
            shell_resolve_path(path, resolved, sizeof(resolved));
            if (vfs_write_text(resolved, text)) {
                terminal_write("Wrote ");
                terminal_write(resolved);
                terminal_write("\n");
            }
        }
    } else if (string_starts_with(command_buffer, "rm ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(skip_spaces(command_buffer + 3), path, sizeof(path));
        if (vfs_remove(path)) {
            terminal_write("Removed: ");
            terminal_write(path);
            terminal_write("\n");
        }
    } else if (string_starts_with(command_buffer, "kapp ")) {
        char path[SHELL_PATH_SIZE];
        shell_resolve_path(command_buffer + 5, path, sizeof(path));
        kapp_inspect(path);
    } else if (string_equals(command_buffer, "ticks")) {
        terminal_write("Timer ticks: ");
        terminal_write_dec(timer_ticks());
        terminal_write("\n");
    } else if (string_equals(command_buffer, "mem")) {
        memory_map_print();
    } else if (string_equals(command_buffer, "pmm")) {
        pmm_print_stats();
    } else if (string_equals(command_buffer, "alloc")) {
        uint32_t address = pmm_alloc_page();

        if (address == 0) {
            terminal_write("PMM allocation failed\n");
        } else {
            terminal_write("Allocated page: ");
            terminal_write_hex(address);
            terminal_write("\n");
        }
    } else if (string_equals(command_buffer, "heap")) {
        heap_print_stats();
    } else if (string_equals(command_buffer, "kmalloc")) {
        void* pointer = kmalloc(64);

        if (pointer == 0) {
            terminal_write("kmalloc failed\n");
        } else {
            uint8_t* bytes = (uint8_t*)pointer;
            bytes[0] = 0x4B;
            bytes[63] = 0x31;

            terminal_write("kmalloc(64): ");
            terminal_write_hex((uint32_t)pointer);
            terminal_write(" phys=");
            terminal_write_hex(arch_get_physical((uint32_t)pointer));
            terminal_write(" test=");
            terminal_write_hex(bytes[0]);
            terminal_putchar('/');
            terminal_write_hex(bytes[63]);
            terminal_write("\n");
        }
    } else if (string_equals(command_buffer, "paging")) {
        arch_print_status();
    } else if (string_equals(command_buffer, "vmmtest")) {
        arch_test_mapping();
    } else if (string_equals(command_buffer, "gdt")) {
        arch_print_status();
    } else if (string_equals(command_buffer, "ring3")) {
        user_mode_enter_test();
    } else if (string_equals(command_buffer, "apps")) {
        app_print_all();
    } else if (string_starts_with(command_buffer, "appinfo ")) {
        app_print_info(command_buffer + 8);
    } else if (string_starts_with(command_buffer, "which ")) {
        app_print_source(command_buffer + 6);
    } else if (string_equals(command_buffer, "initrd")) {
        initrd_print_info();
    } else if (string_equals(command_buffer, "initrd ls")) {
        initrd_list();
    } else if (string_starts_with(command_buffer, "initrd cat ")) {
        initrd_cat(command_buffer + 11);
    } else if (string_equals(command_buffer, "spawn")) {
        user_mode_spawn_test();
    } else if (string_starts_with(command_buffer, "spawn ")) {
        shell_spawn_app_text(command_buffer + 6);
    } else if (string_starts_with(command_buffer, "run ")) {
        shell_run_foreground(shell_spawn_app_text(command_buffer + 4));
    } else if (string_equals(command_buffer, "runall")) {
        terminal_ensure_rows(6);
        if (task_has_ready()) {
            task_run_all_ready();
        } else {
            terminal_write("No READY tasks\n");
        }
    } else if (string_equals(command_buffer, "tasks")) {
        terminal_ensure_rows(10);
        task_print_all();
    } else if (string_equals(command_buffer, "tasksv")) {
        terminal_ensure_rows(14);
        task_print_all_verbose();
    } else if (string_equals(command_buffer, "about")) {
        terminal_write("Kernel1: 32-bit educational kernel in ASM + C.\n");
    } else {
        terminal_write("Unknown command: ");
        terminal_write(command_buffer);
        terminal_write("\n");
    }
}

void shell_initialize(void) {
    shell_clear_buffer();
    command_pending = 0;
    command_history_count = 0;
    command_history_view = 0;
    current_directory[0] = '\0';
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

    if (shell_complete_path_command("cat ") ||
            shell_complete_path_command("cp ") ||
            shell_complete_path_command("kapp ") ||
            shell_complete_path_command("ls ") ||
            shell_complete_path_command("cd ") ||
            shell_complete_path_command("mv ") ||
            shell_complete_path_command("rm ") ||
            shell_complete_path_command("stat ") ||
            shell_complete_path_command("tree ") ||
            shell_complete_path_command("initrd cat ")) {
        return;
    }

    if (shell_complete_app_command("spawn ") ||
            shell_complete_app_command("run ")) {
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
