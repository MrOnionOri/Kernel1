#include "task.h"

#include "context.h"
#include "arch.h"
#include "heap.h"
#include "terminal.h"
#include "timer.h"
#include "vfs.h"

#define MAX_TASKS 8
#define TASK_KERNEL_STACK_SIZE 4096
#define USER_CODE_SELECTOR 0x1B
#define USER_DATA_SELECTOR 0x23

static struct task tasks[MAX_TASKS];
static struct task* current_task;
static uint32_t next_task_id;
static uint32_t scheduler_cursor;
static volatile uint32_t scheduler_pending_ticks;
static uint32_t scheduler_auto_steps;
static uint32_t scheduler_preemptions;
static enum scheduler_mode current_scheduler_mode;
static struct kernel_context scheduler_context;

static void task_wake_sleeping(void);

static const char* task_state_name(enum task_state state) {
    switch (state) {
        case TASK_UNUSED:
            return "unused";
        case TASK_READY:
            return "ready";
        case TASK_RUNNING:
            return "running";
        case TASK_SLEEPING:
            return "sleeping";
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

static struct task* task_find(uint32_t id) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].id == id) {
            return &tasks[i];
        }
    }

    return 0;
}

static void task_close_files(struct task* task) {
    for (uint32_t i = 0; i < TASK_MAX_FILES; i++) {
        if (task->file_fds[i] != VFS_INVALID_FD) {
            vfs_close(task->file_fds[i]);
            task->file_fds[i] = VFS_INVALID_FD;
        }
    }
}

static void task_print_context_details(const struct task* task) {
    if (!task->context.valid) {
        terminal_write("    ctx <empty>\n");
        return;
    }

    terminal_write("    ctx eip=");
    terminal_write_hex(task->context.eip);
    terminal_write(" esp=");
    terminal_write_hex(task->context.esp);
    terminal_write(" eflags=");
    terminal_write_hex(task->context.eflags);
    terminal_write(" cs=");
    terminal_write_hex(task->context.cs);
    terminal_write(" ss=");
    terminal_write_hex(task->context.ss);
    terminal_write("\n    regs eax=");
    terminal_write_hex(task->context.eax);
    terminal_write(" ebx=");
    terminal_write_hex(task->context.ebx);
    terminal_write(" ecx=");
    terminal_write_hex(task->context.ecx);
    terminal_write(" edx=");
    terminal_write_hex(task->context.edx);
    terminal_write("\n    regs esi=");
    terminal_write_hex(task->context.esi);
    terminal_write(" edi=");
    terminal_write_hex(task->context.edi);
    terminal_write(" ebp=");
    terminal_write_hex(task->context.ebp);
    terminal_write("\n");
}

static int task_range_contains(uint32_t start, uint32_t end, uint32_t address, uint32_t length) {
    if (length == 0) {
        return 1;
    }

    if (start >= end || address + length < address) {
        return 0;
    }

    return address >= start && address + length <= end;
}

static void task_clear_context(struct task* task) {
    task->context.eax = 0;
    task->context.ebx = 0;
    task->context.ecx = 0;
    task->context.edx = 0;
    task->context.esi = 0;
    task->context.edi = 0;
    task->context.ebp = 0;
    task->context.esp = 0;
    task->context.eip = 0;
    task->context.eflags = 0;
    task->context.cs = 0;
    task->context.ss = 0;
    task->context.valid = 0;
}

static void task_save_context(struct task* task, const struct interrupt_frame* frame) {
    if (task == 0 || frame == 0) {
        return;
    }

    task->context.eax = frame->eax;
    task->context.ebx = frame->ebx;
    task->context.ecx = frame->ecx;
    task->context.edx = frame->edx;
    task->context.esi = frame->esi;
    task->context.edi = frame->edi;
    task->context.ebp = frame->ebp;
    task->context.esp = frame->useresp;
    task->context.eip = frame->eip;
    task->context.eflags = frame->eflags;
    task->context.cs = frame->cs;
    task->context.ss = frame->ss;
    task->context.valid = 1;
}

static void task_load_context_into_frame(const struct task* task, struct interrupt_frame* frame) {
    frame->ds = task->context.ss == 0 ? USER_DATA_SELECTOR : task->context.ss;
    frame->edi = task->context.edi;
    frame->esi = task->context.esi;
    frame->ebp = task->context.ebp;
    frame->esp = task->context.esp;
    frame->ebx = task->context.ebx;
    frame->edx = task->context.edx;
    frame->ecx = task->context.ecx;
    frame->eax = task->context.eax;
    frame->eip = task->context.eip;
    frame->cs = task->context.cs == 0 ? USER_CODE_SELECTOR : task->context.cs;
    frame->eflags = task->context.eflags | 0x200;
    frame->useresp = task->context.esp;
    frame->ss = task->context.ss == 0 ? USER_DATA_SELECTOR : task->context.ss;
}

static struct task* scheduler_pick_next_ready(void) {
    task_wake_sleeping();

    for (uint32_t scan = 0; scan < MAX_TASKS; scan++) {
        uint32_t i = (scheduler_cursor + scan) % MAX_TASKS;

        if (tasks[i].state == TASK_READY) {
            scheduler_cursor = (i + 1) % MAX_TASKS;
            return &tasks[i];
        }
    }

    return 0;
}

static int ticks_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static void task_wake_sleeping(void) {
    uint32_t now = timer_ticks();

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && ticks_reached(now, tasks[i].wake_tick)) {
            tasks[i].state = TASK_READY;
            tasks[i].wake_tick = 0;
            if (current_scheduler_mode == SCHEDULER_AUTO) {
                scheduler_pending_ticks++;
            }
        }
    }
}

static void task_clear_slot(struct task* task) {
    uint32_t reusable_kernel_stack_top = task->kernel_stack_top;

    if (task->page_directory != 0) {
        arch_free_address_space(task->page_directory);
    }

    task->id = 0;
    task->name[0] = '\0';
    task->state = TASK_UNUSED;
    task->page_directory = 0;
    task->entry = 0;
    task->user_image_base = 0;
    task->user_image_limit = 0;
    task->user_stack_base = 0;
    task->user_stack_top = 0;
    task->kernel_stack_top = reusable_kernel_stack_top;
    task->exit_code = 0;
    task->yields = 0;
    task->preemptions = 0;
    task->wake_tick = 0;
    task_clear_context(task);
    task->args[0] = '\0';
    task_reset_files(task);
}

void task_initialize(void) {
    next_task_id = 1;
    current_task = 0;
    scheduler_cursor = 0;
    scheduler_pending_ticks = 0;
    scheduler_auto_steps = 0;
    scheduler_preemptions = 0;
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
    scheduler_pending_ticks = 0;
}

void scheduler_reset_stats(void) {
    scheduler_pending_ticks = 0;
    scheduler_auto_steps = 0;
    scheduler_preemptions = 0;
}

uint32_t scheduler_preemption_count(void) {
    return scheduler_preemptions;
}

void scheduler_print_status(void) {
    terminal_write("Scheduler:\n  mode=");
    terminal_write(scheduler_mode_name(current_scheduler_mode));
    terminal_write("\n  ready=");
    terminal_write(task_has_ready() ? "yes" : "no");
    terminal_write("\n  auto preemption=");
    terminal_write(current_scheduler_mode == SCHEDULER_AUTO ? "irq0 user-mode" : "off");
    terminal_write("\n  cursor=");
    terminal_write_dec(scheduler_cursor);
    terminal_write("\n  pending ticks=");
    terminal_write_dec(scheduler_pending_ticks);
    terminal_write("\n  auto steps=");
    terminal_write_dec(scheduler_auto_steps);
    terminal_write("\n  preemptions=");
    terminal_write_dec(scheduler_preemptions);
    terminal_write("\n");
}

void scheduler_tick(void) {
    task_wake_sleeping();

    if (current_scheduler_mode != SCHEDULER_AUTO) {
        return;
    }

    if (task_has_ready()) {
        scheduler_pending_ticks++;
    }
}

int scheduler_preempt_if_needed(struct interrupt_frame* frame) {
    if (current_scheduler_mode != SCHEDULER_AUTO ||
            current_task == 0 ||
            current_task->state != TASK_RUNNING ||
            frame == 0 ||
            (frame->cs & 0x3) != 0x3 ||
            !task_has_ready()) {
        return 0;
    }

    struct task* next = scheduler_pick_next_ready();
    if (next == 0 || !next->context.valid) {
        return 0;
    }

    task_save_context(current_task, frame);
    current_task->entry = frame->eip;
    current_task->state = TASK_READY;
    current_task->preemptions++;

    next->state = TASK_RUNNING;
    current_task = next;
    arch_set_kernel_stack(next->kernel_stack_top);
    arch_switch_address_space(next->page_directory);
    task_load_context_into_frame(next, frame);
    scheduler_auto_steps++;
    scheduler_preemptions++;
    return 1;
}

uint32_t scheduler_service_pending(void) {
    if (current_scheduler_mode != SCHEDULER_AUTO ||
            scheduler_pending_ticks == 0 ||
            current_task != 0 ||
            !task_has_ready()) {
        return 0;
    }

    scheduler_pending_ticks = 0;
    task_run_all_ready();
    scheduler_auto_steps++;
    return 1;
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

            uint32_t page_directory = arch_create_address_space();
            if (page_directory == 0) {
                return 0;
            }

            tasks[i].id = next_task_id++;
            string_copy(tasks[i].name, name == 0 ? "user" : name, sizeof(tasks[i].name));
            tasks[i].state = TASK_READY;
            tasks[i].page_directory = page_directory;
            tasks[i].entry = entry;
            tasks[i].user_image_base = 0;
            tasks[i].user_image_limit = 0;
            tasks[i].user_stack_base = 0;
            tasks[i].user_stack_top = user_stack_top;
            tasks[i].exit_code = 0;
            tasks[i].yields = 0;
            tasks[i].preemptions = 0;
            tasks[i].wake_tick = 0;
            task_clear_context(&tasks[i]);
            tasks[i].context.eip = entry;
            tasks[i].context.esp = user_stack_top;
            tasks[i].context.eflags = 0x202;
            tasks[i].context.cs = USER_CODE_SELECTOR;
            tasks[i].context.ss = USER_DATA_SELECTOR;
            tasks[i].context.valid = 1;
            string_copy(tasks[i].args, args, sizeof(tasks[i].args));
            task_reset_files(&tasks[i]);
            if (current_scheduler_mode == SCHEDULER_AUTO) {
                scheduler_pending_ticks++;
            }
            return &tasks[i];
        }
    }

    return 0;
}

void task_set_user_memory(struct task* task, uint32_t image_base, uint32_t image_limit,
        uint32_t stack_base, uint32_t stack_top) {
    if (task == 0) {
        return;
    }

    task->user_image_base = image_base;
    task->user_image_limit = image_limit;
    task->user_stack_base = stack_base;
    task->user_stack_top = stack_top;
}

int task_current_user_range_is_valid(uint32_t address, uint32_t length) {
    if (current_task == 0) {
        return 0;
    }

    if (task_range_contains(current_task->user_image_base, current_task->user_image_limit,
            address, length)) {
        return 1;
    }

    return task_range_contains(current_task->user_stack_base, current_task->user_stack_top,
        address, length);
}

struct task* task_create_user_named(const char* name, uint32_t entry, uint32_t user_stack_top) {
    return task_create_user_with_args(name, entry, user_stack_top, "");
}

struct task* task_create_user(uint32_t entry, uint32_t user_stack_top) {
    return task_create_user_named("user", entry, user_stack_top);
}

static void task_run_internal(struct task* task, int print_shell_return) {
    if (task == 0 || task->state != TASK_READY) {
        return;
    }

    current_task = task;
    task->state = TASK_RUNNING;

    if (context_save(&scheduler_context) == 0) {
        arch_set_kernel_stack(task->kernel_stack_top);
        arch_switch_address_space(task->page_directory);
        if (task->context.valid) {
            arch_enter_user_context(&task->context);
        } else {
            arch_enter_user_mode(task->entry, task->user_stack_top);
        }
    }

    struct task* returned_task = current_task;
    arch_switch_address_space(0);

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

    current_task = 0;
}

void task_run(struct task* task) {
    task_run_internal(task, 1);
}

int task_has_ready(void) {
    task_wake_sleeping();

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_READY) {
            return 1;
        }
    }

    return 0;
}

void task_run_all_ready(void) {
    uint32_t start_cursor = scheduler_cursor;

    task_wake_sleeping();

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
    struct task* task = task_find(id);

    if (task == 0 || task->state == TASK_EXITED || task == current_task) {
        return 0;
    }

    task_close_files(task);
    task->exit_code = exit_code;
    task->state = TASK_EXITED;
    return 1;
}

int task_wait(uint32_t id, uint32_t* exit_code) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].id != id) {
            continue;
        }

        while (tasks[i].state == TASK_READY || tasks[i].state == TASK_SLEEPING) {
            task_wake_sleeping();
            if (tasks[i].state == TASK_READY) {
                task_run_internal(&tasks[i], 0);
            }
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

int task_get_exit_status(uint32_t id, uint32_t* exit_code) {
    task_wake_sleeping();

    struct task* task = task_find(id);
    if (task == 0) {
        return -1;
    }

    if (task->state != TASK_EXITED) {
        return 0;
    }

    if (exit_code != 0) {
        *exit_code = task->exit_code;
    }
    return 1;
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
        task_save_context(current_task, frame);
        current_task->entry = frame->eip;
        current_task->state = TASK_READY;
        current_task->yields++;
        if (current_scheduler_mode == SCHEDULER_AUTO) {
            scheduler_pending_ticks++;
        }
    }

}

void task_sleep_current(struct interrupt_frame* frame, uint32_t ticks) {
    if (current_task != 0) {
        task_save_context(current_task, frame);
        current_task->entry = frame->eip;
        current_task->state = ticks == 0 ? TASK_READY : TASK_SLEEPING;
        current_task->wake_tick = ticks == 0 ? 0 : timer_ticks() + ticks;
        current_task->yields++;
        if (current_scheduler_mode == SCHEDULER_AUTO && ticks == 0) {
            scheduler_pending_ticks++;
        }
    }

}

void task_prepare_exit_return(struct interrupt_frame* frame, uint32_t exit_code) {
    if (current_task != 0) {
        task_save_context(current_task, frame);
    }
    task_exit_current(exit_code);
    context_restore(&scheduler_context);
}

void task_prepare_yield_return(struct interrupt_frame* frame) {
    task_yield_current(frame);
    context_restore(&scheduler_context);
}

void task_prepare_sleep_return(struct interrupt_frame* frame, uint32_t ticks) {
    task_sleep_current(frame, ticks);
    context_restore(&scheduler_context);
}

void task_print_all(void) {
    task_wake_sleeping();
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
        terminal_write(" pd=");
        terminal_write_hex(tasks[i].page_directory);
        terminal_write(" ustack=");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write(" img=");
        terminal_write_hex(tasks[i].user_image_base);
        terminal_write("-");
        terminal_write_hex(tasks[i].user_image_limit);
        terminal_write(" exit=");
        terminal_write_dec(tasks[i].exit_code);
        terminal_write(" y=");
        terminal_write_dec(tasks[i].yields);
        terminal_write(" p=");
        terminal_write_dec(tasks[i].preemptions);
        if (tasks[i].state == TASK_SLEEPING) {
            terminal_write(" wake=");
            terminal_write_dec(tasks[i].wake_tick);
        }
        terminal_write(" files=");
        terminal_write_dec(task_open_file_count(&tasks[i]));
        terminal_write("\n");
    }
}

void task_print_all_verbose(void) {
    task_wake_sleeping();
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
        terminal_write(" pd=");
        terminal_write_hex(tasks[i].page_directory);
        terminal_write("\n    entry=");
        terminal_write_hex(tasks[i].entry);
        terminal_write(" ustack=");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write(" stack=");
        terminal_write_hex(tasks[i].user_stack_base);
        terminal_write("-");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write("\n    image=");
        terminal_write_hex(tasks[i].user_image_base);
        terminal_write("-");
        terminal_write_hex(tasks[i].user_image_limit);
        terminal_write("\n    kstack=");
        terminal_write_hex(tasks[i].kernel_stack_top);
        terminal_write(" exit=");
        terminal_write_dec(tasks[i].exit_code);
        terminal_write(" yields=");
        terminal_write_dec(tasks[i].yields);
        terminal_write(" preempts=");
        terminal_write_dec(tasks[i].preemptions);
        if (tasks[i].state == TASK_SLEEPING) {
            terminal_write(" wake=");
            terminal_write_dec(tasks[i].wake_tick);
        }
        terminal_write(" files=");
        terminal_write_dec(task_open_file_count(&tasks[i]));
        terminal_write("\n");
        task_print_context_details(&tasks[i]);
        if (tasks[i].args[0] != '\0') {
            terminal_write(" args=");
            terminal_write(tasks[i].args);
        }
        terminal_write("\n");
    }
}

int task_print_context(uint32_t id) {
    task_wake_sleeping();
    struct task* task = task_find(id);

    if (task == 0) {
        return 0;
    }

    terminal_write("Task context:\n  id=");
    terminal_write_dec(task->id);
    terminal_write(" name=");
    terminal_write(task->name);
    terminal_write(" state=");
    terminal_write(task_state_name(task->state));
    terminal_write(" pd=");
    terminal_write_hex(task->page_directory);
    terminal_write("\n  entry=");
    terminal_write_hex(task->entry);
    terminal_write(" ustack=");
    terminal_write_hex(task->user_stack_top);
    terminal_write(" stack=");
    terminal_write_hex(task->user_stack_base);
    terminal_write("-");
    terminal_write_hex(task->user_stack_top);
    terminal_write(" kstack=");
    terminal_write_hex(task->kernel_stack_top);
    terminal_write("\n  image=");
    terminal_write_hex(task->user_image_base);
    terminal_write("-");
    terminal_write_hex(task->user_image_limit);
    terminal_write("\n  yields=");
    terminal_write_dec(task->yields);
    terminal_write(" preempts=");
    terminal_write_dec(task->preemptions);
    terminal_write("\n");
    task_print_context_details(task);
    if (task->args[0] != '\0') {
        terminal_write("  args=");
        terminal_write(task->args);
        terminal_write("\n");
    }
    return 1;
}

void task_print_summary(void) {
    task_wake_sleeping();
    uint32_t unused = 0;
    uint32_t ready = 0;
    uint32_t running = 0;
    uint32_t sleeping = 0;
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
            case TASK_SLEEPING:
                sleeping++;
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
    terminal_write(" sleeping=");
    terminal_write_dec(sleeping);
    terminal_write(" unused=");
    terminal_write_dec(unused);
    terminal_write("\n  next id=");
    terminal_write_dec(next_task_id);
    terminal_write(" scheduler cursor=");
    terminal_write_dec(scheduler_cursor);
    if (exited > 0) {
        terminal_write("\n  hint: reap -a frees exited task slots");
    }
    terminal_write("\n");
}
