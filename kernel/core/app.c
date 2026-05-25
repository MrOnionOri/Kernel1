#include "app.h"

#include "initrd.h"
#include "kapp.h"
#include "terminal.h"
#include "vfs.h"

#include <stddef.h>

#define APP_MANIFEST_PATH "apps/manifest.txt"

extern void user_test(void);
extern void busy_app(void);
extern void clock_app(void);
extern void reader_app(void);

static const struct app_descriptor apps[] = {
    { "demo", (uint32_t)user_test },
    { "busy", (uint32_t)busy_app },
    { "clock", (uint32_t)clock_app },
    { "reader", (uint32_t)reader_app },
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

static void string_copy_until(char* destination, const char* source, char delimiter, uint32_t size) {
    uint32_t index = 0;

    if (size == 0) {
        return;
    }

    while (index < size - 1 && source[index] != '\0' &&
            source[index] != delimiter && source[index] != '\n') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static const char* string_after_delimiter(const char* text, char delimiter) {
    while (*text != '\0' && *text != delimiter && *text != '\n') {
        text++;
    }

    if (*text == delimiter) {
        return text + 1;
    }

    return text;
}

static int app_is_built_in(const char* name) {
    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (string_equals(apps[i].name, name)) {
            return 1;
        }
    }

    return 0;
}

static void app_print_metadata(const char* app_name, const char* path, void* context) {
    (void)context;

    if (app_is_built_in(app_name)) {
        return;
    }

    terminal_write("  ");
    terminal_write(app_name);
    terminal_write(" (metadata ");
    terminal_write(path);
    terminal_write(")\n");
}

static void app_print_kapp(const char* app_name, const char* path, void* context) {
    (void)path;
    (void)context;

    terminal_write("  ");
    terminal_write(app_name);
    terminal_write(" (kapp)\n");
}

static void app_print_manifest(void) {
    int fd = vfs_open(APP_MANIFEST_PATH);

    if (fd == VFS_INVALID_FD) {
        return;
    }

    terminal_write("Apps:\n");

    char line[96];
    uint32_t line_length = 0;

    for (;;) {
        char character;
        int32_t bytes = vfs_read(fd, &character, 1);

        if (bytes <= 0 && line_length == 0) {
            break;
        }

        if (bytes > 0 && character != '\n' && line_length < sizeof(line) - 1) {
            line[line_length++] = character;
            continue;
        }

        line[line_length] = '\0';

        char name[24];
        char kind[16];
        const char* kind_start;
        const char* description_start;

        string_copy_until(name, line, '|', sizeof(name));
        kind_start = string_after_delimiter(line, '|');
        string_copy_until(kind, kind_start, '|', sizeof(kind));
        description_start = string_after_delimiter(kind_start, '|');

        terminal_write("  ");
        terminal_write(name);
        terminal_write("  ");
        terminal_write(kind);
        terminal_write("  ");
        terminal_write(description_start);
        terminal_write("\n");

        line_length = 0;
    }

    vfs_close(fd);
}

enum app_kind app_manifest_kind(const char* name) {
    int fd = vfs_open(APP_MANIFEST_PATH);

    if (fd == VFS_INVALID_FD) {
        return APP_KIND_UNKNOWN;
    }

    char line[96];
    uint32_t line_length = 0;

    for (;;) {
        char character;
        int32_t bytes = vfs_read(fd, &character, 1);

        if (bytes <= 0 && line_length == 0) {
            break;
        }

        if (bytes > 0 && character != '\n' && line_length < sizeof(line) - 1) {
            line[line_length++] = character;
            continue;
        }

        line[line_length] = '\0';

        char entry_name[24];
        char kind[16];
        const char* kind_start;

        string_copy_until(entry_name, line, '|', sizeof(entry_name));

        if (string_equals(entry_name, name)) {
            kind_start = string_after_delimiter(line, '|');
            string_copy_until(kind, kind_start, '|', sizeof(kind));
            vfs_close(fd);

            if (string_equals(kind, "built-in")) {
                return APP_KIND_BUILT_IN;
            }

            if (string_equals(kind, "kapp")) {
                return APP_KIND_KAPP;
            }

            return APP_KIND_UNKNOWN;
        }

        line_length = 0;
    }

    vfs_close(fd);
    return APP_KIND_UNKNOWN;
}

void app_print_info(const char* name) {
    int fd = vfs_open(APP_MANIFEST_PATH);

    if (fd != VFS_INVALID_FD) {
        char line[96];
        uint32_t line_length = 0;

        for (;;) {
            char character;
            int32_t bytes = vfs_read(fd, &character, 1);

            if (bytes <= 0 && line_length == 0) {
                break;
            }

            if (bytes > 0 && character != '\n' && line_length < sizeof(line) - 1) {
                line[line_length++] = character;
                continue;
            }

            line[line_length] = '\0';

            char entry_name[24];
            char kind[16];
            const char* kind_start;
            const char* description_start;

            string_copy_until(entry_name, line, '|', sizeof(entry_name));

            if (string_equals(entry_name, name)) {
                kind_start = string_after_delimiter(line, '|');
                string_copy_until(kind, kind_start, '|', sizeof(kind));
                description_start = string_after_delimiter(kind_start, '|');

                terminal_write(entry_name);
                terminal_write(" (");
                terminal_write(kind);
                terminal_write(")\n");
                terminal_write(description_start);
                terminal_write("\n");
                vfs_close(fd);
                return;
            }

            line_length = 0;
        }

        vfs_close(fd);
    }

    initrd_cat_app_metadata(name);
}

void app_print_source(const char* name) {
    enum app_kind kind = app_manifest_kind(name);

    if (kind == APP_KIND_BUILT_IN) {
        terminal_write(name);
        terminal_write(": built-in\n");
        return;
    }

    if (kind == APP_KIND_KAPP) {
        terminal_write(name);
        terminal_write(": kapp apps/");
        terminal_write(name);
        terminal_write(".kapp\n");
        return;
    }

    if (app_find(name) != 0) {
        terminal_write(name);
        terminal_write(": built-in (fallback)\n");
        return;
    }

    if (kapp_exists(name)) {
        terminal_write(name);
        terminal_write(": kapp apps/");
        terminal_write(name);
        terminal_write(".kapp (fallback)\n");
        return;
    }

    terminal_write(name);
    terminal_write(": not found\n");
}

void app_print_all(void) {
    int manifest_fd = vfs_open(APP_MANIFEST_PATH);

    if (manifest_fd != VFS_INVALID_FD) {
        vfs_close(manifest_fd);
        app_print_manifest();
        return;
    }

    terminal_write("Apps:\n");

    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        terminal_write("  ");
        terminal_write(apps[i].name);
        terminal_write(" (built-in)\n");
    }

    initrd_for_each_kapp_app(app_print_kapp, 0);
    initrd_for_each_app_metadata(app_print_metadata, 0);
}

const struct app_descriptor* app_find(const char* name) {
    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (string_equals(apps[i].name, name)) {
            return &apps[i];
        }
    }

    return 0;
}

const struct app_descriptor* app_find_prefix(const char* prefix) {
    const struct app_descriptor* match = 0;

    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (!string_starts_with(apps[i].name, prefix)) {
            continue;
        }

        if (match != 0) {
            return 0;
        }

        match = &apps[i];
    }

    return match;
}
