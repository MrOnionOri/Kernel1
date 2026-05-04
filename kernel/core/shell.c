#include "shell.h"

#include "arch.h"
#include "heap.h"
#include "memory_map.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "user_mode.h"

#include <stddef.h>

#define COMMAND_BUFFER_SIZE 80

static char command_buffer[COMMAND_BUFFER_SIZE];
static size_t command_length;

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

static void shell_prompt(void) {
    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("kernel1> ");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void shell_clear_buffer(void) {
    for (size_t i = 0; i < COMMAND_BUFFER_SIZE; i++) {
        command_buffer[i] = '\0';
    }

    command_length = 0;
}

static void shell_execute_command(void) {
    command_buffer[command_length] = '\0';

    if (command_length == 0) {
        return;
    }

    if (string_equals(command_buffer, "help")) {
        terminal_write("Commands: help, clear, ticks, mem, pmm, alloc, heap, kmalloc, paging, vmmtest, gdt, ring3, spawn, runall, tasks, about\n");
    } else if (string_equals(command_buffer, "clear")) {
        terminal_initialize();
        terminal_write("Kernel1 shell\n");
    } else if (string_equals(command_buffer, "ticks")) {
        terminal_write("Timer ticks: ");
        terminal_write_dec(timer_ticks());
        terminal_write("\n");
    } else if (string_equals(command_buffer, "mem")) {
        memory_map_print();
    } else if (string_equals(command_buffer, "pmm")) {
        pmm_print_stats();
    } else if (string_equals(command_buffer, "alloc")) {
        uint32_t address = pmm_alloc_page();

        if (address == 0) {
            terminal_write("PMM allocation failed\n");
        } else {
            terminal_write("Allocated page: ");
            terminal_write_hex(address);
            terminal_write("\n");
        }
    } else if (string_equals(command_buffer, "heap")) {
        heap_print_stats();
    } else if (string_equals(command_buffer, "kmalloc")) {
        void* pointer = kmalloc(64);

        if (pointer == 0) {
            terminal_write("kmalloc failed\n");
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
        }
    } else if (string_equals(command_buffer, "paging")) {
        arch_print_status();
    } else if (string_equals(command_buffer, "vmmtest")) {
        arch_test_mapping();
    } else if (string_equals(command_buffer, "gdt")) {
        arch_print_status();
    } else if (string_equals(command_buffer, "ring3")) {
        user_mode_enter_test();
    } else if (string_equals(command_buffer, "spawn")) {
        user_mode_spawn_test();
    } else if (string_equals(command_buffer, "runall")) {
        task_run_all_ready();
    } else if (string_equals(command_buffer, "tasks")) {
        task_print_all();
    } else if (string_equals(command_buffer, "about")) {
        terminal_write("Kernel1: 32-bit educational kernel in ASM + C.\n");
    } else {
        terminal_write("Unknown command: ");
        terminal_write(command_buffer);
        terminal_write("\n");
    }
}

void shell_initialize(void) {
    shell_clear_buffer();
    terminal_write("Type 'help' for commands.\n");
    shell_prompt();
}

void shell_put_char(char character) {
    if (character == '\n') {
        terminal_putchar('\n');
        shell_execute_command();
        shell_clear_buffer();
        shell_prompt();
        return;
    }

    if (character == '\b') {
        if (command_length > 0) {
            command_length--;
            command_buffer[command_length] = '\0';
            terminal_backspace();
        }

        return;
    }

    if (command_length >= COMMAND_BUFFER_SIZE - 1) {
        return;
    }

    command_buffer[command_length++] = character;
    terminal_putchar(character);
}
