#include "shell_fs.h"

#include "terminal.h"
#include "vfs.h"

static char current_directory[SHELL_FS_PATH_SIZE];

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

static int shell_mkdir_p(const char* input) {
    char resolved[SHELL_FS_PATH_SIZE];
    char partial[SHELL_FS_PATH_SIZE];
    size_t read = 0;
    size_t write = 0;

    shell_fs_resolve_path(input, resolved, sizeof(resolved));

    if (resolved[0] == '\0' || string_equals(resolved, "/")) {
        return 1;
    }

    partial[0] = '\0';

    while (resolved[read] != '\0') {
        while (resolved[read] == '/') {
            read++;
        }

        if (resolved[read] == '\0') {
            break;
        }

        if (write > 0 && write < sizeof(partial) - 1) {
            partial[write++] = '/';
            partial[write] = '\0';
        }

        while (resolved[read] != '\0' && resolved[read] != '/' && write < sizeof(partial) - 1) {
            partial[write++] = resolved[read++];
            partial[write] = '\0';
        }

        if (vfs_is_directory(partial)) {
            continue;
        }

        if (!vfs_mkdir(partial)) {
            return 0;
        }
    }

    return 1;
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

    char resolved[SHELL_FS_PATH_SIZE];
    shell_fs_resolve_path(path, resolved, sizeof(resolved));

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

void shell_fs_initialize(void) {
    current_directory[0] = '\0';
}

const char* shell_fs_current_directory(void) {
    return current_directory;
}

void shell_fs_resolve_path(const char* input, char* output, size_t size) {
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

int shell_fs_write_redirect(const struct shell_line* line, const char* text) {
    if (!line->redirect) {
        return 0;
    }

    char resolved[SHELL_FS_PATH_SIZE];
    shell_fs_resolve_path(line->redirect_path, resolved, sizeof(resolved));

    if (line->redirect_append) {
        if (vfs_append_text(resolved, text)) {
            return 1;
        }
    } else if (vfs_write_text(resolved, text)) {
        return 1;
    }

    return 1;
}

int shell_fs_handle_line(const struct shell_line* line, int* last_status) {
    if (string_equals(line->args[0], "write") || string_equals(line->args[0], "append")) {
        if (line->count < 3) {
            terminal_write(line->args[0]);
            terminal_write(": usage ");
            terminal_write(line->args[0]);
            terminal_write(" <file> <text>\n");
            *last_status = 1;
        } else {
            char resolved[SHELL_FS_PATH_SIZE];
            char text[SHELL_PARSER_BUFFER_SIZE];
            shell_fs_resolve_path(line->args[1], resolved, sizeof(resolved));
            shell_join_args(line, 2, text, sizeof(text));

            int ok = string_equals(line->args[0], "append") ?
                vfs_append_text(resolved, text) : vfs_write_text(resolved, text);

            if (ok) {
                terminal_write(string_equals(line->args[0], "append") ? "Appended " : "Wrote ");
                terminal_write(resolved);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "cp") || string_equals(line->args[0], "mv")) {
        if (line->count < 3) {
            terminal_write(line->args[0]);
            terminal_write(": usage ");
            terminal_write(line->args[0]);
            terminal_write(" <src> <dst>\n");
            *last_status = 1;
        } else {
            char source[SHELL_FS_PATH_SIZE];
            char destination[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], source, sizeof(source));
            shell_fs_resolve_path(line->args[2], destination, sizeof(destination));

            if (string_equals(line->args[0], "cp")) {
                if (vfs_copy(source, destination)) {
                    terminal_write("Copied ");
                    terminal_write(source);
                    terminal_write(" -> ");
                    terminal_write(destination);
                    terminal_write("\n");
                    *last_status = 0;
                } else {
                    *last_status = 1;
                }
            } else if (vfs_move(source, destination)) {
                terminal_write("Moved ");
                terminal_write(source);
                terminal_write(" -> ");
                terminal_write(destination);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "cd")) {
        shell_change_directory(line->count > 1 ? line->args[1] : "/");
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "pwd")) {
        terminal_write("/");
        terminal_write(current_directory);
        terminal_write("\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "ls")) {
        char path[SHELL_FS_PATH_SIZE];
        if (line->count > 1) {
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            vfs_list_path(path);
        } else {
            vfs_list_path(current_directory);
        }
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "cat")) {
        if (line->count < 2) {
            terminal_write("cat: usage cat <file>\n");
            *last_status = 1;
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            vfs_cat(path);
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "stat")) {
        if (line->count < 2) {
            terminal_write("stat: usage stat <path>\n");
            *last_status = 1;
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            vfs_stat(path);
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "du")) {
        char path[SHELL_FS_PATH_SIZE];
        if (line->count > 1) {
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
        } else {
            string_copy(path, current_directory, sizeof(path));
        }
        vfs_du(path);
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "find")) {
        char path[SHELL_FS_PATH_SIZE];
        const char* pattern;

        if (line->count < 2) {
            terminal_write("find: usage find [path] <text>\n");
            *last_status = 1;
            return 1;
        }

        if (line->count == 2) {
            string_copy(path, current_directory, sizeof(path));
            pattern = line->args[1];
        } else {
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            pattern = line->args[2];
        }

        vfs_find(path, pattern);
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "tree")) {
        char path[SHELL_FS_PATH_SIZE];
        if (line->count > 1) {
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            vfs_tree(path);
        } else {
            vfs_tree(current_directory);
        }
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "mkdir")) {
        if (line->count < 2) {
            terminal_write("mkdir: usage mkdir [-p] <dir>\n");
            *last_status = 1;
        } else if (string_equals(line->args[1], "-p")) {
            if (line->count < 3) {
                terminal_write("mkdir: usage mkdir -p <dir>\n");
                *last_status = 1;
            } else if (shell_mkdir_p(line->args[2])) {
                terminal_write("Directory ready: ");
                terminal_write(line->args[2]);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            if (vfs_mkdir(path)) {
                terminal_write("Created directory: ");
                terminal_write(path);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "touch")) {
        if (line->count < 2) {
            terminal_write("touch: usage touch <file>\n");
            *last_status = 1;
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            if (vfs_write_text(path, "")) {
                terminal_write("Touched ");
                terminal_write(path);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        }
        return 1;
    }

    if (string_equals(line->args[0], "rm")) {
        if (line->count < 2) {
            terminal_write("rm: usage rm [-r] <path>\n");
            *last_status = 1;
        } else if (string_equals(line->args[1], "-r")) {
            if (line->count < 3) {
                terminal_write("rm: usage rm -r <path>\n");
                *last_status = 1;
            } else {
                char path[SHELL_FS_PATH_SIZE];
                shell_fs_resolve_path(line->args[2], path, sizeof(path));
                if (vfs_remove_recursive(path)) {
                    terminal_write("Removed recursively: ");
                    terminal_write(path);
                    terminal_write("\n");
                    *last_status = 0;
                } else {
                    *last_status = 1;
                }
            }
        } else {
            char path[SHELL_FS_PATH_SIZE];
            shell_fs_resolve_path(line->args[1], path, sizeof(path));
            if (vfs_remove(path)) {
                terminal_write("Removed: ");
                terminal_write(path);
                terminal_write("\n");
                *last_status = 0;
            } else {
                *last_status = 1;
            }
        }
        return 1;
    }

    return 0;
}
