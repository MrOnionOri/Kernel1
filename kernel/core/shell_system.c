#include "shell_system.h"

#include "arch.h"
#include "heap.h"
#include "memory_map.h"
#include "pmm.h"
#include "terminal.h"
#include "timer.h"
#include "user_mode.h"

#include <stdint.h>

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

int shell_system_handle_line(const struct shell_line* line, int* last_status) {
    if (string_equals(line->args[0], "ticks")) {
        terminal_write("Timer ticks: ");
        terminal_write_dec(timer_ticks());
        terminal_write("\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "mem")) {
        memory_map_print();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "pmm")) {
        pmm_print_stats();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "alloc")) {
        uint32_t address = pmm_alloc_page();

        if (address == 0) {
            terminal_write("PMM allocation failed\n");
            *last_status = 1;
        } else {
            terminal_write("Allocated page: ");
            terminal_write_hex(address);
            terminal_write("\n");
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "heap")) {
        heap_print_stats();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "kmalloc")) {
        void* pointer = kmalloc(64);

        if (pointer == 0) {
            terminal_write("kmalloc failed\n");
            *last_status = 1;
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
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "paging") || string_equals(line->args[0], "gdt")) {
        arch_print_status();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "vmmtest")) {
        arch_test_mapping();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "ring3")) {
        user_mode_enter_test();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "about")) {
        terminal_write("Kernel1: 32-bit educational kernel in ASM + C.\n");
        *last_status = 0;
        return 1;
    }

    return 0;
}
