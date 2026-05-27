#include "arch.h"
#include "framebuffer.h"
#include "heap.h"
#include "keyboard.h"
#include "mouse.h"
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
    framebuffer_initialize();
    if (framebuffer_get_info()->hardware_backed) {
        framebuffer_console_reset();
        terminal_set_graphics_mirror(1);
        terminal_write("Kernel1 graphics console\n");
    }
    task_initialize();
    mouse_initialize();
    arch_enable_irq(0);
    arch_enable_irq(1);
    arch_enable_irq(12);

    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("Architecture ready. IRQ0 timer, IRQ1 keyboard, and IRQ12 mouse enabled.\n");

    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("Portable layout ready. Next: app loader/initrd.\n");
    keyboard_initialize();
    shell_initialize();

    arch_enable_interrupts();

    for (;;) {
        shell_poll();
        arch_halt();
    }
}
