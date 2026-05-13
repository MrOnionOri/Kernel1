#include "shell_apps.h"

#include "app.h"
#include "kapp.h"
#include "shell_fs.h"
#include "task.h"
#include "terminal.h"
#include "user_mode.h"

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

static uint32_t string_to_uint(const char* text, int* ok) {
    uint32_t value = 0;
    size_t index = 0;

    *ok = 0;

    if (text[0] == '\0') {
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

static int shell_complete_app_command(const char* command_buffer,
        const char* command_prefix, char* output, size_t size) {
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
        string_copy(output, command_prefix, size);

        size_t index = prefix_length;
        size_t app_index = 0;
        while (index < size - 1 && app->name[app_index] != '\0') {
            output[index++] = app->name[app_index++];
        }
        output[index] = '\0';
    }

    return 1;
}

int shell_apps_handle_line(const struct shell_line* line, int* last_status) {
    if (string_equals(line->args[0], "run") || string_equals(line->args[0], "spawn")) {
        if (line->count < 2) {
            terminal_write(line->args[0]);
            terminal_write(": missing app\n");
            *last_status = 1;
        } else {
            char app_text[SHELL_PARSER_BUFFER_SIZE];
            shell_join_args(line, 1, app_text, sizeof(app_text));
            if (string_equals(line->args[0], "run")) {
                shell_run_foreground(shell_spawn_app_text(app_text));
            } else {
                shell_spawn_app_text(app_text);
            }
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "kapp")) {
        if (line->count < 2) {
            terminal_write("kapp: usage kapp <file>\n");
            *last_status = 1;
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            kapp_inspect(path);
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "apps")) {
        app_print_all();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "appinfo")) {
        if (line->count < 2) {
            terminal_write("appinfo: usage appinfo <app>\n");
            *last_status = 1;
        } else {
            app_print_info(line->args[1]);
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "which")) {
        if (line->count < 2) {
            terminal_write("which: usage which <app>\n");
            *last_status = 1;
        } else {
            app_print_source(line->args[1]);
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "runall")) {
        terminal_ensure_rows(6);
        if (task_has_ready()) {
            task_run_all_ready();
        } else {
            terminal_write("No READY tasks\n");
        }
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "kill")) {
        if (line->count < 2) {
            terminal_write("kill: usage kill <id>\n");
            *last_status = 1;
        } else {
            int ok;
            uint32_t id = string_to_uint(line->args[1], &ok);
            if (!ok || id == 0) {
                terminal_write("kill: invalid id\n");
                *last_status = 1;
            } else if (task_kill(id, 130)) {
                terminal_write("Killed task ");
                terminal_write_dec(id);
                terminal_write("\n");
                *last_status = 0;
            } else {
                terminal_write("kill: task not found or not killable: ");
                terminal_write_dec(id);
                terminal_write("\n");
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "wait")) {
        if (line->count < 2) {
            terminal_write("wait: usage wait <id>\n");
            *last_status = 1;
        } else {
            int ok;
            uint32_t id = string_to_uint(line->args[1], &ok);
            uint32_t exit_code = 0;

            if (!ok || id == 0) {
                terminal_write("wait: invalid id\n");
                *last_status = 1;
            } else if (task_wait(id, &exit_code)) {
                terminal_write("Task ");
                terminal_write_dec(id);
                terminal_write(" exited with ");
                terminal_write_dec(exit_code);
                terminal_write("\n");
                *last_status = (int)exit_code;
            } else {
                terminal_write("wait: task not found or not waitable: ");
                terminal_write_dec(id);
                terminal_write("\n");
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "tasks")) {
        terminal_ensure_rows(10);
        task_print_all();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "tasksv")) {
        terminal_ensure_rows(14);
        task_print_all_verbose();
        *last_status = 0;
        return 1;
    }

    return 0;
}

int shell_apps_complete(const char* command_buffer, char* output, size_t size) {
    if (size == 0) {
        return 0;
    }

    output[0] = '\0';

    return shell_complete_app_command(command_buffer, "appinfo ", output, size) ||
        shell_complete_app_command(command_buffer, "spawn ", output, size) ||
        shell_complete_app_command(command_buffer, "which ", output, size) ||
        shell_complete_app_command(command_buffer, "run ", output, size);
}
