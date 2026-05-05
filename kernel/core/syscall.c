#include "syscall.h"

#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "vfs.h"

#define STDOUT_FD 1
#define USER_FILE_FD_BASE 3
#define WRITE_BUF_MAX 1024
#define USER_STRING_MAX 128
#define USER_STACK_REGION_BASE 0x02000000
#define USER_STACK_REGION_LIMIT 0x02100000
#define USER_KAPP_REGION_BASE 0x03000000
#define USER_KAPP_REGION_LIMIT 0x03100000

extern uint8_t user_image_start;
extern uint8_t user_image_end;

static int range_contains(uint32_t start, uint32_t end, uint32_t address, uint32_t length) {
    if (length == 0) {
        return 1;
    }

    if (address + length < address) {
        return 0;
    }

    return address >= start && address + length <= end;
}

static int user_range_is_valid(const void* pointer, uint32_t length) {
    uint32_t address = (uint32_t)pointer;

    if (pointer == 0) {
        return 0;
    }

    if (range_contains((uint32_t)&user_image_start, (uint32_t)&user_image_end, address, length)) {
        return 1;
    }

    if (range_contains(USER_KAPP_REGION_BASE, USER_KAPP_REGION_LIMIT, address, length)) {
        return 1;
    }

    return range_contains(USER_STACK_REGION_BASE, USER_STACK_REGION_LIMIT, address, length);
}

static int user_string_is_valid(const char* text) {
    if (!user_range_is_valid(text, 1)) {
        return 0;
    }

    for (uint32_t i = 0; i < USER_STRING_MAX; i++) {
        if (!user_range_is_valid(text + i, 1)) {
            return 0;
        }

        if (text[i] == '\0') {
            return 1;
        }
    }

    return 0;
}

static void syscall_write(const char* text) {
    if (!user_string_is_valid(text)) {
        return;
    }

    terminal_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_write("[user] ");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write(text);
    terminal_write("\n");
}

static int32_t syscall_write_buffer(uint32_t fd, const char* text, uint32_t length) {
    if (fd != STDOUT_FD || length > WRITE_BUF_MAX || !user_range_is_valid(text, length)) {
        return -1;
    }

    for (uint32_t i = 0; i < length; i++) {
        terminal_putchar(text[i]);
    }

    return (int32_t)length;
}

static void syscall_exit(uint32_t code) {
    task_exit_current(code);
}

static void syscall_write_dec(uint32_t value) {
    terminal_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_write("[user] ");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write_dec(value);
    terminal_write("\n");
}

static void syscall_yield(struct interrupt_frame* frame) {
    (void)frame;
    terminal_set_color(VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);
    terminal_write("SYS_YIELD: cooperative context switch parked for stabilization\n");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static int32_t syscall_open(const char* path) {
    if (!user_string_is_valid(path)) {
        return -1;
    }

    int fd = vfs_open(path);

    if (fd == VFS_INVALID_FD) {
        return -1;
    }

    return fd + USER_FILE_FD_BASE;
}

static int32_t syscall_read(uint32_t user_fd, char* buffer, uint32_t size) {
    if (user_fd < USER_FILE_FD_BASE || !user_range_is_valid(buffer, size)) {
        return -1;
    }

    return vfs_read((int)(user_fd - USER_FILE_FD_BASE), buffer, size);
}

static int32_t syscall_close(uint32_t user_fd) {
    if (user_fd < USER_FILE_FD_BASE) {
        return -1;
    }

    vfs_close((int)(user_fd - USER_FILE_FD_BASE));
    return 0;
}

static int32_t syscall_getargs(char* buffer, uint32_t size) {
    if (!user_range_is_valid(buffer, size)) {
        return -1;
    }

    return (int32_t)task_copy_current_args(buffer, size);
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
        case SYS_WRITE_DEC:
            syscall_write_dec(frame->ebx);
            break;
        case SYS_GETPID:
            frame->eax = task_current_id();
            break;
        case SYS_TICKS:
            frame->eax = timer_ticks();
            break;
        case SYS_WRITE_BUF:
            frame->eax = (uint32_t)syscall_write_buffer(frame->ebx, (const char*)frame->ecx, frame->edx);
            break;
        case SYS_OPEN:
            frame->eax = (uint32_t)syscall_open((const char*)frame->ebx);
            break;
        case SYS_READ:
            frame->eax = (uint32_t)syscall_read(frame->ebx, (char*)frame->ecx, frame->edx);
            break;
        case SYS_CLOSE:
            frame->eax = (uint32_t)syscall_close(frame->ebx);
            break;
        case SYS_GETARGS:
            frame->eax = (uint32_t)syscall_getargs((char*)frame->ebx, frame->ecx);
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
