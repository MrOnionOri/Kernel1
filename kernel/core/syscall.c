#include "syscall.h"

#include "app.h"
#include "kapp.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "user_mode.h"
#include "vfs.h"

#define STDOUT_FD 1
#define USER_FILE_FD_BASE 3
#define WRITE_BUF_MAX 1024
#define USER_STRING_MAX 128
#define SYS_WAIT_RUNNING ((int32_t)-2)

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

static int string_contains_char(const char* text, char needle) {
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == needle) {
            return 1;
        }
    }

    return 0;
}

static int user_range_is_valid(const void* pointer, uint32_t length) {
    if (pointer == 0) {
        return 0;
    }

    return task_current_user_range_is_valid((uint32_t)pointer, length);
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

static int copy_user_string(char* destination, const char* source, uint32_t size) {
    uint32_t index = 0;

    if (destination == 0 || source == 0 || size == 0 || !user_string_is_valid(source)) {
        return 0;
    }

    while (index < size - 1 && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
    return 1;
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
    task_prepare_yield_return(frame);
}

static void syscall_sleep(struct interrupt_frame* frame, uint32_t ticks) {
    frame->eax = 0;
    task_prepare_sleep_return(frame, ticks);
}

static int32_t syscall_open(const char* path) {
    if (!user_string_is_valid(path)) {
        return -1;
    }

    int fd = vfs_open(path);
    int task_fd;

    if (fd == VFS_INVALID_FD) {
        return -1;
    }

    task_fd = task_current_add_file(fd);
    if (task_fd == VFS_INVALID_FD) {
        vfs_close(fd);
        return -1;
    }

    return task_fd + USER_FILE_FD_BASE;
}

static int32_t syscall_open_flags(const char* path, uint32_t flags) {
    if (!user_string_is_valid(path)) {
        return -1;
    }

    int fd = vfs_open_flags(path, flags);
    int task_fd;

    if (fd == VFS_INVALID_FD) {
        return -1;
    }

    task_fd = task_current_add_file(fd);
    if (task_fd == VFS_INVALID_FD) {
        vfs_close(fd);
        return -1;
    }

    return task_fd + USER_FILE_FD_BASE;
}

static int32_t syscall_read(uint32_t user_fd, char* buffer, uint32_t size) {
    if (user_fd < USER_FILE_FD_BASE || !user_range_is_valid(buffer, size)) {
        return -1;
    }

    int fd = task_current_get_file(user_fd - USER_FILE_FD_BASE);
    if (fd == VFS_INVALID_FD) {
        return -1;
    }

    return vfs_read(fd, buffer, size);
}

static int32_t syscall_write_fd(uint32_t user_fd, const char* buffer, uint32_t size) {
    if (user_fd < USER_FILE_FD_BASE || size > WRITE_BUF_MAX || !user_range_is_valid(buffer, size)) {
        return -1;
    }

    int fd = task_current_get_file(user_fd - USER_FILE_FD_BASE);
    if (fd == VFS_INVALID_FD) {
        return -1;
    }

    return vfs_write(fd, buffer, size);
}

static int32_t syscall_close(uint32_t user_fd) {
    if (user_fd < USER_FILE_FD_BASE) {
        return -1;
    }

    return task_current_close_file(user_fd - USER_FILE_FD_BASE);
}

static int32_t syscall_getargs(char* buffer, uint32_t size) {
    if (!user_range_is_valid(buffer, size)) {
        return -1;
    }

    return (int32_t)task_copy_current_args(buffer, size);
}

static int32_t syscall_write_file(const char* path, const char* text) {
    if (!user_string_is_valid(path) || !user_string_is_valid(text)) {
        return -1;
    }

    return vfs_write_text(path, text) ? 0 : -1;
}

static int32_t syscall_append_file(const char* path, const char* text) {
    if (!user_string_is_valid(path) || !user_string_is_valid(text)) {
        return -1;
    }

    return vfs_append_text(path, text) ? 0 : -1;
}

static int32_t syscall_mkdir(const char* path) {
    if (!user_string_is_valid(path)) {
        return -1;
    }

    if (vfs_is_directory(path)) {
        return 0;
    }

    return vfs_mkdir(path) ? 0 : -1;
}

static int32_t syscall_stat(const char* path, struct vfs_stat_info* info) {
    if (!user_string_is_valid(path) ||
            !user_range_is_valid(info, sizeof(struct vfs_stat_info))) {
        return -1;
    }

    return vfs_stat_info(path, info) ? 0 : -1;
}

static int32_t syscall_readdir(const char* path, uint32_t index,
        struct vfs_dir_entry* entry) {
    if (!user_string_is_valid(path) ||
            !user_range_is_valid(entry, sizeof(struct vfs_dir_entry))) {
        return -1;
    }

    return vfs_read_dir(path, index, entry);
}

static int32_t syscall_exec(const char* user_name, const char* user_args) {
    char name[32];
    char args[TASK_ARGS_SIZE];
    struct task* task = 0;

    if (!copy_user_string(name, user_name, sizeof(name))) {
        return -1;
    }

    if (user_args == 0) {
        args[0] = '\0';
    } else if (!copy_user_string(args, user_args, sizeof(args))) {
        return -1;
    }

    if (string_contains_char(name, '/') || string_ends_with(name, ".kapp")) {
        task = kapp_spawn_path(name, args);
        return task == 0 ? -1 : (int32_t)task->id;
    }

    enum app_kind kind = app_manifest_kind(name);
    const struct app_descriptor* app = app_find(name);

    if (kind == APP_KIND_BUILT_IN && app != 0) {
        task = user_mode_spawn_app_with_args(app->name, app->entry, args);
        return task == 0 ? -1 : (int32_t)task->id;
    }

    if (kind == APP_KIND_KAPP) {
        task = kapp_spawn_app(name, args);
        return task == 0 ? -1 : (int32_t)task->id;
    }

    if (app != 0) {
        task = user_mode_spawn_app_with_args(app->name, app->entry, args);
        return task == 0 ? -1 : (int32_t)task->id;
    }

    task = kapp_spawn_app(name, args);
    return task == 0 ? -1 : (int32_t)task->id;
}

static int32_t syscall_wait(uint32_t task_id) {
    uint32_t exit_code = 0;
    int status = task_get_exit_status(task_id, &exit_code);

    if (status < 0) {
        return -1;
    }

    if (status == 0) {
        return SYS_WAIT_RUNNING;
    }

    return (int32_t)exit_code;
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
        case SYS_OPEN_FLAGS:
            frame->eax = (uint32_t)syscall_open_flags((const char*)frame->ebx, frame->ecx);
            break;
        case SYS_READ:
            frame->eax = (uint32_t)syscall_read(frame->ebx, (char*)frame->ecx, frame->edx);
            break;
        case SYS_WRITE_FD:
            frame->eax = (uint32_t)syscall_write_fd(frame->ebx, (const char*)frame->ecx, frame->edx);
            break;
        case SYS_CLOSE:
            frame->eax = (uint32_t)syscall_close(frame->ebx);
            break;
        case SYS_GETARGS:
            frame->eax = (uint32_t)syscall_getargs((char*)frame->ebx, frame->ecx);
            break;
        case SYS_WRITE_FILE:
            frame->eax = (uint32_t)syscall_write_file((const char*)frame->ebx, (const char*)frame->ecx);
            break;
        case SYS_APPEND_FILE:
            frame->eax = (uint32_t)syscall_append_file((const char*)frame->ebx, (const char*)frame->ecx);
            break;
        case SYS_MKDIR:
            frame->eax = (uint32_t)syscall_mkdir((const char*)frame->ebx);
            break;
        case SYS_SLEEP:
            syscall_sleep(frame, frame->ebx);
            break;
        case SYS_STAT:
            frame->eax = (uint32_t)syscall_stat((const char*)frame->ebx,
                (struct vfs_stat_info*)frame->ecx);
            break;
        case SYS_READDIR:
            frame->eax = (uint32_t)syscall_readdir((const char*)frame->ebx,
                frame->ecx, (struct vfs_dir_entry*)frame->edx);
            break;
        case SYS_EXEC:
            frame->eax = (uint32_t)syscall_exec((const char*)frame->ebx,
                (const char*)frame->ecx);
            break;
        case SYS_WAIT:
            frame->eax = (uint32_t)syscall_wait(frame->ebx);
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
