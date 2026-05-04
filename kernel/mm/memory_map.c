#include "memory_map.h"

#include "terminal.h"

#define MEMORY_MAP_COUNT_ADDR 0x9000
#define MEMORY_MAP_ENTRIES_ADDR 0x9004
#define MEMORY_MAP_MAX 16

static const char* memory_type_name(uint32_t type) {
    switch (type) {
        case 1:
            return "usable";
        case 2:
            return "reserved";
        case 3:
            return "acpi reclaim";
        case 4:
            return "acpi nvs";
        case 5:
            return "bad";
        default:
            return "unknown";
    }
}

uint32_t memory_map_count(void) {
    uint32_t count = *(volatile uint32_t*)MEMORY_MAP_COUNT_ADDR;

    if (count > MEMORY_MAP_MAX) {
        count = MEMORY_MAP_MAX;
    }

    return count;
}

const struct memory_map_entry* memory_map_entries(void) {
    return (const struct memory_map_entry*)MEMORY_MAP_ENTRIES_ADDR;
}

void memory_map_print(void) {
    uint32_t count = memory_map_count();
    const struct memory_map_entry* entries = memory_map_entries();

    terminal_write("BIOS E820 memory map entries: ");
    terminal_write_dec(count);
    terminal_write("\n");

    for (uint32_t i = 0; i < count; i++) {
        terminal_write("  ");
        terminal_write_dec(i);
        terminal_write(" base=");
        terminal_write_hex64(entries[i].base);
        terminal_write(" len=");
        terminal_write_hex64(entries[i].length);
        terminal_write(" type=");
        terminal_write_dec(entries[i].type);
        terminal_write(" ");
        terminal_write(memory_type_name(entries[i].type));
        terminal_write("\n");
    }
}
