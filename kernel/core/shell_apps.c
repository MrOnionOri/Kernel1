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

static int string_contains_char(const char* text, char needle) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == needle) {
            return 1;
        }
    }

    return 0;
}

static int string_ends_with(const char* text, const char* suffix) {
    size_t text_length = 0;
    size_t suffix_length = 0;

    while (text[text_length] != '\0') {
        text_length++;
    }

    while (suffix[suffix_length] != '\0') {
        suffix_length++;
    }

    if (suffix_length > text_length) {
        return 0;
    }

    return string_equals(text + text_length - suffix_length, suffix);
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

    if (string_contains_char(app_name, '/') || string_ends_with(app_name, ".kapp")) {
        char path[SHELL_FS_PATH_SIZE];
        shell_fs_resolve_path(app_name, path, sizeof(path));
        struct task* task = kapp_spawn_path(path, args);
        if (task != 0) {
            return task;
        }

        terminal_write("KAPP path failed: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

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
        if (line->count > 1 && string_equals(line->args[1], "-a")) {
            uint32_t ran = task_run_all_ready_until_idle();
            terminal_write("runall -a: ran ");
            terminal_write_dec(ran);
            terminal_write(" scheduler step");
            if (ran != 1) {
                terminal_write("s");
            }
            terminal_write("\n");
        } else if (task_has_ready()) {
            task_run_all_ready();
        } else {
            terminal_write("No READY tasks\n");
        }
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "schedtest")) {
        enum scheduler_mode previous_mode = scheduler_get_mode();
        int reap_after_test = 0;
        uint32_t rounds = 1;
        uint32_t total_steps = 0;
        uint32_t preemptions_before;
        uint32_t preemptions_after;
        uint32_t failed = 0;

        for (uint32_t i = 1; i < line->count; i++) {
            if (string_equals(line->args[i], "-r")) {
                reap_after_test = 1;
            } else if (string_equals(line->args[i], "-n")) {
                int ok;

                if (i + 1 >= line->count) {
                    terminal_write("schedtest: usage schedtest [-r] [-n rounds]\n");
                    *last_status = 1;
                    return 1;
                }

                rounds = string_to_uint(line->args[i + 1], &ok);
                if (!ok || rounds == 0 || rounds > 9) {
                    terminal_write("schedtest: rounds must be 1..9\n");
                    *last_status = 1;
                    return 1;
                }
                i++;
            } else {
                terminal_write("schedtest: usage schedtest [-r] [-n rounds]\n");
                *last_status = 1;
                return 1;
            }
        }

        terminal_ensure_rows(14);
        terminal_write("schedtest: busy + two demo tasks, rounds=");
        terminal_write_dec(rounds);
        terminal_write("\n");
        preemptions_before = scheduler_preemption_count();

        for (uint32_t round = 0; round < rounds; round++) {
            uint32_t ran;

            if (reap_after_test) {
                task_reap_exited();
            }

            terminal_write("schedtest: round ");
            terminal_write_dec(round + 1);
            terminal_write("\n");
            scheduler_set_mode(SCHEDULER_COOPERATIVE);

            if (shell_spawn_app_text("busy") == 0 ||
                    shell_spawn_app_text("demo") == 0 ||
                    shell_spawn_app_text("demo") == 0) {
                terminal_write("schedtest: spawn failed\n");
                failed = 1;
                break;
            }

            scheduler_set_mode(SCHEDULER_AUTO);
            ran = task_run_all_ready_until_idle();
            total_steps += ran;
            terminal_write("schedtest: round steps ");
            terminal_write_dec(ran);
            terminal_write("\n");

            if (task_has_ready()) {
                terminal_write("schedtest: ready tasks remain\n");
                failed = 1;
                break;
            }
        }

        preemptions_after = scheduler_preemption_count();
        terminal_write("schedtest: total steps ");
        terminal_write_dec(total_steps);
        terminal_write("\n");
        task_print_summary();
        scheduler_print_status();
        if (reap_after_test) {
            uint32_t reaped = task_reap_exited();
            terminal_write("schedtest: reaped ");
            terminal_write_dec(reaped);
            terminal_write(" exited task");
            if (reaped != 1) {
                terminal_write("s");
            }
            terminal_write("\n");
        }
        if (!failed && !task_has_ready() && preemptions_after > preemptions_before) {
            terminal_write("schedtest: PASS\n");
        } else {
            terminal_write("schedtest: FAIL\n");
            if (preemptions_after == preemptions_before) {
                terminal_write("schedtest: no IRQ0 preemptions observed\n");
            }
        }
        scheduler_set_mode(previous_mode);
        *last_status = (failed || task_has_ready() || preemptions_after == preemptions_before) ? 1 : 0;
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
            terminal_write("wait: usage wait [-r] <id>\n");
            *last_status = 1;
        } else {
            int ok;
            int reap_after_wait = 0;
            const char* id_text = line->args[1];
            uint32_t exit_code = 0;

            if (string_equals(line->args[1], "-r")) {
                if (line->count < 3) {
                    terminal_write("wait: usage wait -r <id>\n");
                    *last_status = 1;
                    return 1;
                }

                reap_after_wait = 1;
                id_text = line->args[2];
            }

            uint32_t id = string_to_uint(id_text, &ok);
            if (!ok || id == 0) {
                terminal_write("wait: invalid id\n");
                *last_status = 1;
            } else if (task_wait(id, &exit_code)) {
                terminal_write("Task ");
                terminal_write_dec(id);
                terminal_write(" exited with ");
                terminal_write_dec(exit_code);
                terminal_write("\n");
                if (reap_after_wait && task_reap(id)) {
                    terminal_write("Reaped task ");
                    terminal_write_dec(id);
                    terminal_write("\n");
                }
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

    if (string_equals(line->args[0], "reap")) {
        if (line->count > 1 && !string_equals(line->args[1], "-a")) {
            int ok;
            uint32_t id = string_to_uint(line->args[1], &ok);

            if (!ok || id == 0) {
                terminal_write("reap: usage reap [id|-a]\n");
                *last_status = 1;
            } else if (task_reap(id)) {
                terminal_write("Reaped task ");
                terminal_write_dec(id);
                terminal_write("\n");
                *last_status = 0;
            } else {
                terminal_write("reap: task not found or not exited: ");
                terminal_write_dec(id);
                terminal_write("\n");
                *last_status = 1;
            }
            return 1;
        }

        uint32_t count = task_reap_exited();

        terminal_write("Reaped ");
        terminal_write_dec(count);
        terminal_write(" exited task");
        if (count != 1) {
            terminal_write("s");
        }
        terminal_write("\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "ps")) {
        if (line->count > 1 && string_equals(line->args[1], "-s")) {
            task_print_summary();
        } else if (line->count > 1 && string_equals(line->args[1], "-v")) {
            terminal_ensure_rows(14);
            task_print_all_verbose();
        } else {
            terminal_ensure_rows(10);
            task_print_all();
        }
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "ctx")) {
        if (line->count < 2) {
            terminal_write("ctx: usage ctx <task-id>\n");
            *last_status = 1;
        } else {
            int ok;
            uint32_t id = string_to_uint(line->args[1], &ok);

            if (!ok || id == 0) {
                terminal_write("ctx: invalid id\n");
                *last_status = 1;
            } else if (task_print_context(id)) {
                *last_status = 0;
            } else {
                terminal_write("ctx: task not found: ");
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
