#include "kapp.h"

#include "initrd.h"
#include "terminal.h"

#include <stdint.h>

#define KAPP_HEADER_SIZE 20

static uint32_t read_u32(const char* data) {
    return (uint32_t)(uint8_t)data[0] |
        ((uint32_t)(uint8_t)data[1] << 8) |
        ((uint32_t)(uint8_t)data[2] << 16) |
        ((uint32_t)(uint8_t)data[3] << 24);
}

void kapp_inspect(const char* path) {
    struct initrd_file file;

    if (!initrd_find(path, &file)) {
        terminal_write("KAPP not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    if (file.size < KAPP_HEADER_SIZE ||
            file.data[0] != 'K' || file.data[1] != 'A' ||
            file.data[2] != 'P' || file.data[3] != 'P') {
        terminal_write("Invalid KAPP: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    uint32_t header_size = read_u32(file.data + 4);
    uint32_t entry_offset = read_u32(file.data + 8);
    uint32_t image_size = read_u32(file.data + 12);
    uint32_t flags = read_u32(file.data + 16);

    terminal_write("KAPP ");
    terminal_write(path);
    terminal_write("\n  header=");
    terminal_write_dec(header_size);
    terminal_write(" entry=");
    terminal_write_hex(entry_offset);
    terminal_write("\n  image=");
    terminal_write_dec(image_size);
    terminal_write(" file=");
    terminal_write_dec(file.size);
    terminal_write(" flags=");
    terminal_write_hex(flags);
    terminal_write("\n");

    if (header_size < KAPP_HEADER_SIZE || header_size + image_size > file.size) {
        terminal_write("  status=invalid size\n");
    } else {
        terminal_write("  status=loadable candidate\n");
    }
}
