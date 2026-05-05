#include "app.h"

#include "initrd.h"
#include "terminal.h"

#include <stddef.h>

extern void user_test(void);
extern void clock_app(void);
extern void reader_app(void);

static const struct app_descriptor apps[] = {
    { "demo", (uint32_t)user_test },
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

void app_print_all(void) {
    terminal_write("Apps:\n");

    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        terminal_write("  ");
        terminal_write(apps[i].name);
        terminal_write(" (built-in)\n");
    }

    initrd_list_app_metadata();
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
