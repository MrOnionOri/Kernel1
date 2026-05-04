#include "syscall.h"

#include "task.h"
#include "terminal.h"

static void syscall_write(const char* text) {
    terminal_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_write("[user] ");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write(text);
    terminal_write("\n");
}

static void syscall_exit(uint32_t code) {
    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write("User task exited with code ");
    terminal_write_dec(code);
    terminal_write("\n");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    (void)code;
}

static void syscall_yield(struct interrupt_frame* frame) {
    (void)frame;
    terminal_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    terminal_write("SYS_YIELD: cooperative context switch parked for stabilization\n");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void syscall_dispatch(struct interrupt_frame* frame) {
    switch (frame->eax) {
        case SYS_WRITE:
            syscall_write((const char*)frame->ebx);
            break;
        case SYS_EXIT:
            syscall_exit(frame->ebx);
            task_prepare_exit_return(frame, frame->ebx);
            break;
        case SYS_YIELD:
            syscall_yield(frame);
            break;
        default:
            terminal_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            terminal_write("Unknown syscall: ");
            terminal_write_dec(frame->eax);
            terminal_write("\n");
            terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            break;
    }
}
