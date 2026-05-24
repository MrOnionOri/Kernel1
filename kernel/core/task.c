#include "task.h"

#include "context.h"
#include "arch.h"
#include "heap.h"
#include "terminal.h"
#include "vfs.h"

#define MAX_TASKS 8
#define TASK_KERNEL_STACK_SIZE 4096

static struct task tasks[MAX_TASKS];
static struct task* current_task;
static uint32_t next_task_id;
static uint32_t scheduler_cursor;
static enum scheduler_mode current_scheduler_mode;
static struct kernel_context scheduler_context;

static const char* task_state_name(enum task_state state) {
    switch (state) {
        case TASK_UNUSED:
            return "unused";
        case TASK_READY:
            return "ready";
        case TASK_RUNNING:
            return "running";
        case TASK_EXITED:
            return "exited";
        default:
            return "unknown";
    }
}

const char* scheduler_mode_name(enum scheduler_mode mode) {
    switch (mode) {
        case SCHEDULER_COOPERATIVE:
            return "cooperative";
        case SCHEDULER_AUTO:
            return "auto";
        default:
            return "unknown";
    }
}

static void string_copy(char* destination, const char* source, uint32_t size) {
    uint32_t index = 0;

    if (size == 0) {
        return;
    }

    if (source == 0) {
        source = "";
    }

    while (index < size - 1 && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static void task_reset_files(struct task* task) {
    for (uint32_t i = 0; i < TASK_MAX_FILES; i++) {
        task->file_fds[i] = VFS_INVALID_FD;
    }
}

static uint32_t task_open_file_count(const struct task* task) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < TASK_MAX_FILES; i++) {
        if (task->file_fds[i] != VFS_INVALID_FD) {
            count++;
        }
    }

    return count;
}

static void task_close_files(struct task* task) {
    for (uint32_t i = 0; i < TASK_MAX_FILES; i++) {
        if (task->file_fds[i] != VFS_INVALID_FD) {
            vfs_close(task->file_fds[i]);
            task->file_fds[i] = VFS_INVALID_FD;
        }
    }
}

static void task_clear_slot(struct task* task) {
    uint32_t reusable_kernel_stack_top = task->kernel_stack_top;

    task->id = 0;
    task->name[0] = '\0';
    task->state = TASK_UNUSED;
    task->entry = 0;
    task->user_stack_top = 0;
    task->kernel_stack_top = reusable_kernel_stack_top;
    task->exit_code = 0;
    task->yields = 0;
    task->args[0] = '\0';
    task_reset_files(task);
}

void task_initialize(void) {
    next_task_id = 1;
    current_task = 0;
    scheduler_cursor = 0;
    current_scheduler_mode = SCHEDULER_COOPERATIVE;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].kernel_stack_top = 0;
        task_clear_slot(&tasks[i]);
    }
}

enum scheduler_mode scheduler_get_mode(void) {
    return current_scheduler_mode;
}

void scheduler_set_mode(enum scheduler_mode mode) {
    current_scheduler_mode = mode;
}

void scheduler_print_status(void) {
    terminal_write("Scheduler:\n  mode=");
    terminal_write(scheduler_mode_name(current_scheduler_mode));
    terminal_write("\n  ready=");
    terminal_write(task_has_ready() ? "yes" : "no");
    terminal_write("\n  auto preemption=");
    terminal_write(current_scheduler_mode == SCHEDULER_AUTO ? "armed (timer hook pending)" : "off");
    terminal_write("\n  cursor=");
    terminal_write_dec(scheduler_cursor);
    terminal_write("\n");
}

void scheduler_tick(void) {
    if (current_scheduler_mode != SCHEDULER_AUTO) {
        return;
    }

    /*
     * Preemptive switching will live here once IRQ return can save and restore
     * user contexts safely. For now, auto mode is an armed no-op.
     */
}

uint32_t task_next_id(void) {
    return next_task_id;
}

uint32_t task_current_id(void) {
    return current_task == 0 ? 0 : current_task->id;
}

uint32_t task_copy_current_args(char* buffer, uint32_t size) {
    uint32_t index = 0;

    if (current_task == 0 || buffer == 0 || size == 0) {
        return 0;
    }

    while (index < size - 1 && current_task->args[index] != '\0') {
        buffer[index] = current_task->args[index];
        index++;
    }

    buffer[index] = '\0';
    return index;
}

int task_current_add_file(int vfs_fd) {
    if (current_task == 0 || vfs_fd == VFS_INVALID_FD) {
        return VFS_INVALID_FD;
    }

    for (uint32_t i = 0; i < TASK_MAX_FILES; i++) {
        if (current_task->file_fds[i] == VFS_INVALID_FD) {
            current_task->file_fds[i] = vfs_fd;
            return (int)i;
        }
    }

    return VFS_INVALID_FD;
}

int task_current_get_file(uint32_t task_fd) {
    if (current_task == 0 || task_fd >= TASK_MAX_FILES) {
        return VFS_INVALID_FD;
    }

    return current_task->file_fds[task_fd];
}

int task_current_close_file(uint32_t task_fd) {
    int vfs_fd;

    if (current_task == 0 || task_fd >= TASK_MAX_FILES) {
        return -1;
    }

    vfs_fd = current_task->file_fds[task_fd];
    if (vfs_fd == VFS_INVALID_FD) {
        return -1;
    }

    vfs_close(vfs_fd);
    current_task->file_fds[task_fd] = VFS_INVALID_FD;
    return 0;
}

struct task* task_create_user_with_args(const char* name, uint32_t entry, uint32_t user_stack_top, const char* args) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_EXITED) {
            if (tasks[i].state == TASK_EXITED) {
                task_clear_slot(&tasks[i]);
            }

            if (tasks[i].kernel_stack_top == 0) {
                void* kernel_stack = kmalloc_aligned(TASK_KERNEL_STACK_SIZE, 16);
                if (kernel_stack == 0) {
                    return 0;
                }

                tasks[i].kernel_stack_top = (uint32_t)kernel_stack + TASK_KERNEL_STACK_SIZE;
            }

            tasks[i].id = next_task_id++;
            string_copy(tasks[i].name, name == 0 ? "user" : name, sizeof(tasks[i].name));
            tasks[i].state = TASK_READY;
            tasks[i].entry = entry;
            tasks[i].user_stack_top = user_stack_top;
            tasks[i].exit_code = 0;
            tasks[i].yields = 0;
            string_copy(tasks[i].args, args, sizeof(tasks[i].args));
            task_reset_files(&tasks[i]);
            return &tasks[i];
        }
    }

    return 0;
}

struct task* task_create_user_named(const char* name, uint32_t entry, uint32_t user_stack_top) {
    return task_create_user_with_args(name, entry, user_stack_top, "");
}

struct task* task_create_user(uint32_t entry, uint32_t user_stack_top) {
    return task_create_user_named("user", entry, user_stack_top);
}

static void task_run_internal(struct task* task, int print_shell_return) {
    current_task = task;
    task->state = TASK_RUNNING;

    if (context_save(&scheduler_context) == 0) {
        arch_set_kernel_stack(task->kernel_stack_top);
        arch_enter_user_mode(task->entry, task->user_stack_top);
    }

    struct task* returned_task = current_task;

    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write(print_shell_return ? "Back in kernel shell. Task " : "Task ");
    terminal_write_dec(returned_task->id);
    terminal_write(" ");
    terminal_write(returned_task->name);
    if (returned_task->state == TASK_EXITED) {
        terminal_write(" exit code ");
        terminal_write_dec(returned_task->exit_code);
    } else {
        terminal_write(" yielded");
    }
    terminal_write("\n");
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (returned_task->state == TASK_EXITED) {
        current_task = 0;
    }
}

void task_run(struct task* task) {
    if (task == 0 || task->state != TASK_READY) {
        return;
    }

    task_run_internal(task, 1);
}

int task_has_ready(void) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_READY) {
            return 1;
        }
    }

    return 0;
}

void task_run_all_ready(void) {
    uint32_t start_cursor = scheduler_cursor;

    for (uint32_t scan = 0; scan < MAX_TASKS; scan++) {
        uint32_t i = (scheduler_cursor + scan) % MAX_TASKS;

        if (tasks[i].state == TASK_READY) {
            scheduler_cursor = (i + 1) % MAX_TASKS;
            task_run_internal(&tasks[i], 0);
            return;
        }
    }

    scheduler_cursor = start_cursor;
    terminal_write("No READY tasks\n");
}

uint32_t task_run_all_ready_until_idle(void) {
    uint32_t ran = 0;

    while (task_has_ready()) {
        task_run_all_ready();
        ran++;
    }

    return ran;
}

int task_kill(uint32_t id, uint32_t exit_code) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].id != id) {
            continue;
        }

        if (tasks[i].state == TASK_EXITED) {
            return 0;
        }

        if (&tasks[i] == current_task) {
            return 0;
        }

        task_close_files(&tasks[i]);
        tasks[i].exit_code = exit_code;
        tasks[i].state = TASK_EXITED;
        return 1;
    }

    return 0;
}

int task_wait(uint32_t id, uint32_t* exit_code) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].id != id) {
            continue;
        }

        while (tasks[i].state == TASK_READY) {
            task_run_internal(&tasks[i], 0);
        }

        if (tasks[i].state == TASK_EXITED) {
            if (exit_code != 0) {
                *exit_code = tasks[i].exit_code;
            }
            return 1;
        }

        return 0;
    }

    return 0;
}

int task_reap(uint32_t id) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].id != id) {
            continue;
        }

        if (tasks[i].state != TASK_EXITED) {
            return 0;
        }

        task_clear_slot(&tasks[i]);
        return 1;
    }

    return 0;
}

uint32_t task_reap_exited(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EXITED) {
            task_clear_slot(&tasks[i]);
            count++;
        }
    }

    return count;
}

void task_exit_current(uint32_t exit_code) {
    if (current_task != 0) {
        task_close_files(current_task);
        current_task->exit_code = exit_code;
        current_task->state = TASK_EXITED;
    }

}

void task_yield_current(struct interrupt_frame* frame) {
    if (current_task != 0) {
        current_task->entry = frame->eip;
        current_task->user_stack_top = frame->useresp;
        current_task->state = TASK_READY;
        current_task->yields++;
    }

}

void task_prepare_exit_return(struct interrupt_frame* frame, uint32_t exit_code) {
    (void)frame;
    task_exit_current(exit_code);
    context_restore(&scheduler_context);
}

void task_prepare_yield_return(struct interrupt_frame* frame) {
    task_yield_current(frame);
    context_restore(&scheduler_context);
}

void task_print_all(void) {
    terminal_write("Tasks:\n");

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            continue;
        }

        terminal_write("  id=");
        terminal_write_dec(tasks[i].id);
        terminal_write(" ");
        terminal_write(tasks[i].name);
        terminal_write(" ");
        terminal_write(task_state_name(tasks[i].state));
        terminal_write(" ustack=");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write(" exit=");
        terminal_write_dec(tasks[i].exit_code);
        terminal_write(" y=");
        terminal_write_dec(tasks[i].yields);
        terminal_write(" files=");
        terminal_write_dec(task_open_file_count(&tasks[i]));
        terminal_write("\n");
    }
}

void task_print_all_verbose(void) {
    terminal_write("Tasks verbose:\n");

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            continue;
        }

        terminal_write("  id=");
        terminal_write_dec(tasks[i].id);
        terminal_write(" name=");
        terminal_write(tasks[i].name);
        terminal_write(" state=");
        terminal_write(task_state_name(tasks[i].state));
        terminal_write("\n    entry=");
        terminal_write_hex(tasks[i].entry);
        terminal_write(" ustack=");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write("\n    kstack=");
        terminal_write_hex(tasks[i].kernel_stack_top);
        terminal_write(" exit=");
        terminal_write_dec(tasks[i].exit_code);
        terminal_write(" yields=");
        terminal_write_dec(tasks[i].yields);
        terminal_write(" files=");
        terminal_write_dec(task_open_file_count(&tasks[i]));
        if (tasks[i].args[0] != '\0') {
            terminal_write(" args=");
            terminal_write(tasks[i].args);
        }
        terminal_write("\n");
    }
}

void task_print_summary(void) {
    uint32_t unused = 0;
    uint32_t ready = 0;
    uint32_t running = 0;
    uint32_t exited = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        switch (tasks[i].state) {
            case TASK_UNUSED:
                unused++;
                break;
            case TASK_READY:
                ready++;
                break;
            case TASK_RUNNING:
                running++;
                break;
            case TASK_EXITED:
                exited++;
                break;
            default:
                break;
        }
    }

    terminal_write("Task summary:\n  ready=");
    terminal_write_dec(ready);
    terminal_write(" running=");
    terminal_write_dec(running);
    terminal_write(" exited=");
    terminal_write_dec(exited);
    terminal_write(" unused=");
    terminal_write_dec(unused);
    terminal_write("\n  next id=");
    terminal_write_dec(next_task_id);
    terminal_write(" scheduler cursor=");
    terminal_write_dec(scheduler_cursor);
    terminal_write("\n");
}
