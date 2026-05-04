#ifndef KERNEL_MEMORY_MAP_H
#define KERNEL_MEMORY_MAP_H

#include <stdint.h>

struct memory_map_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

uint32_t memory_map_count(void);
const struct memory_map_entry* memory_map_entries(void);
void memory_map_print(void);

#endif
