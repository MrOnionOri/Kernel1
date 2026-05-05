#include "initrd.h"

#include "terminal.h"

#include <stdint.h>

#define INITRD_ADDRESS 0x0001C000
#define INITRD_SIZE 8192

static const char initrd_magic[] = "K1RD2";

static int string_equals(const char* left, const char* right) {
    uint32_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static int string_starts_with(const char* text, const char* prefix) {
    uint32_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }

        index++;
    }

    return 1;
}

static int string_ends_with(const char* text, const char* suffix) {
    uint32_t text_length = 0;
    uint32_t suffix_length = 0;

    while (text[text_length] != '\0') {
        text_length++;
    }

    while (suffix[suffix_length] != '\0') {
        suffix_length++;
    }

    if (suffix_length > text_length) {
        return 0;
    }

    for (uint32_t i = 0; i < suffix_length; i++) {
        if (text[text_length - suffix_length + i] != suffix[i]) {
            return 0;
        }
    }

    return 1;
}

static uint32_t read_u32(const char* data) {
    return (uint32_t)(uint8_t)data[0] |
        ((uint32_t)(uint8_t)data[1] << 8) |
        ((uint32_t)(uint8_t)data[2] << 16) |
        ((uint32_t)(uint8_t)data[3] << 24);
}

static int initrd_has_magic(const char* data) {
    for (uint32_t i = 0; initrd_magic[i] != '\0'; i++) {
        if (data[i] != initrd_magic[i]) {
            return 0;
        }
    }

    return data[5] == '\0';
}

static uint32_t string_length_bounded(const char* text, uint32_t max) {
    uint32_t length = 0;

    while (length < max && text[length] != '\0') {
        length++;
    }

    return length;
}

static const char* initrd_first_record(void) {
    const char* base = (const char*)INITRD_ADDRESS;

    if (!initrd_has_magic(base)) {
        return 0;
    }

    return base + 6;
}

static int initrd_read_file(const char* record, struct initrd_file* file) {
    const char* end = (const char*)INITRD_ADDRESS + INITRD_SIZE;
    uint32_t name_length = string_length_bounded(record, (uint32_t)(end - record));

    if (name_length == 0 || record + name_length + 5 > end) {
        return 0;
    }

    const char* size_field = record + name_length + 1;
    uint32_t size = read_u32(size_field);
    const char* data = size_field + 4;

    if (data + size > end) {
        return 0;
    }

    file->name = record;
    file->data = data;
    file->size = size;
    return 1;
}

static const char* initrd_next_record(const char* record) {
    struct initrd_file file;

    if (!initrd_read_file(record, &file)) {
        return 0;
    }

    return file.data + file.size;
}

int initrd_find(const char* name, struct initrd_file* file) {
    const char* record = initrd_first_record();

    if (record == 0) {
        return 0;
    }

    while (record != 0 && record[0] != '\0') {
        struct initrd_file current;
        if (!initrd_read_file(record, &current)) {
            return 0;
        }

        if (string_equals(current.name, name)) {
            *file = current;
            return 1;
        }

        record = initrd_next_record(record);
    }

    return 0;
}

void initrd_for_each(void (*callback)(const struct initrd_file* file, void* context), void* context) {
    const char* record = initrd_first_record();

    if (record == 0) {
        return;
    }

    while (record != 0 && record[0] != '\0') {
        struct initrd_file file;
        if (!initrd_read_file(record, &file)) {
            return;
        }

        callback(&file, context);
        record = initrd_next_record(record);
    }
}

void initrd_print_info(void) {
    const char* first = initrd_first_record();

    if (first == 0) {
        terminal_write("Initrd not found\n");
        return;
    }

    terminal_write("Initrd v2 at ");
    terminal_write_hex(INITRD_ADDRESS);
    terminal_write(" size ");
    terminal_write_dec(INITRD_SIZE);
    terminal_write(" bytes\n");
    terminal_write("Records: name\\0 + u32 size + data\n");
}

void initrd_list(void) {
    const char* record = initrd_first_record();

    if (record == 0) {
        terminal_write("Initrd not found\n");
        return;
    }

    terminal_write("Initrd files:\n");

    while (record != 0 && record[0] != '\0') {
        struct initrd_file file;
        if (!initrd_read_file(record, &file)) {
            terminal_write("  <corrupt record>\n");
            return;
        }

        terminal_write("  ");
        terminal_write(file.name);
        terminal_write("  ");
        terminal_write_dec(file.size);
        terminal_write(" bytes\n");

        record = initrd_next_record(record);
    }
}

void initrd_cat(const char* name) {
    struct initrd_file file;
    if (!initrd_find(name, &file)) {
        terminal_write("Initrd file not found\n");
        return;
    }

    for (uint32_t i = 0; i < file.size; i++) {
        terminal_putchar(file.data[i]);
    }

    terminal_write("\n");
}

void initrd_list_app_metadata(void) {
    const char* record = initrd_first_record();

    if (record == 0) {
        terminal_write("  <no initrd metadata>\n");
        return;
    }

    while (record != 0 && record[0] != '\0') {
        struct initrd_file file;
        if (!initrd_read_file(record, &file)) {
            terminal_write("  <corrupt initrd app metadata>\n");
            return;
        }

        if (string_starts_with(file.name, "apps/") && string_ends_with(file.name, ".txt")) {
            terminal_write("  ");
            terminal_write(file.name);
            terminal_write(" (initrd)\n");
        }

        record = initrd_next_record(record);
    }
}

void initrd_cat_app_metadata(const char* app_name) {
    char path[48];
    const char prefix[] = "apps/";
    const char suffix[] = ".txt";
    uint32_t index = 0;
    uint32_t source = 0;

    while (prefix[source] != '\0' && index < sizeof(path) - 1) {
        path[index++] = prefix[source++];
    }

    source = 0;
    while (app_name[source] != '\0' && index < sizeof(path) - 1) {
        path[index++] = app_name[source++];
    }

    source = 0;
    while (suffix[source] != '\0' && index < sizeof(path) - 1) {
        path[index++] = suffix[source++];
    }

    path[index] = '\0';
    initrd_cat(path);
}
