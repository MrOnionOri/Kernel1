#include "arch.h"
#include "heap.h"
#include "keyboard.h"
#include "pmm.h"
#include "shell.h"
#include "task.h"
#include "terminal.h"

void kernel_main(void) {
    terminal_initialize();

    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("Kernel1\n");

    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("ASM bootloader + C kernel running in 32-bit protected mode.\n");
    terminal_write("Installing arch, memory, drivers, tasks...\n");

    arch_initialize();
    pmm_initialize();
    heap_initialize();
    task_initialize();
    arch_enable_irq(0);
    arch_enable_irq(1);

    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("Architecture ready. IRQ0 timer and IRQ1 keyboard enabled.\n");

    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("Portable layout ready. Next: app loader/initrd.\n");
    keyboard_initialize();
    shell_initialize();

    arch_enable_interrupts();

    for (;;) {
        arch_halt();
    }
}
