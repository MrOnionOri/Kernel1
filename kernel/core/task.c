#include "task.h"

#include "context.h"
#include "arch.h"
#include "terminal.h"

#define MAX_TASKS 8

static struct task tasks[MAX_TASKS];
static struct task* current_task;
static uint32_t next_task_id;
static uint32_t scheduler_cursor;
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

void task_initialize(void) {
    next_task_id = 1;
    current_task = 0;
    scheduler_cursor = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].id = 0;
        tasks[i].state = TASK_UNUSED;
        tasks[i].entry = 0;
        tasks[i].user_stack_top = 0;
        tasks[i].exit_code = 0;
        tasks[i].yields = 0;
    }
}

uint32_t task_next_id(void) {
    return next_task_id;
}

struct task* task_create_user(uint32_t entry, uint32_t user_stack_top) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_EXITED) {
            tasks[i].id = next_task_id++;
            tasks[i].state = TASK_READY;
            tasks[i].entry = entry;
            tasks[i].user_stack_top = user_stack_top;
            tasks[i].exit_code = 0;
            tasks[i].yields = 0;
            return &tasks[i];
        }
    }

    return 0;
}

static void task_run_internal(struct task* task, int print_shell_return) {
    current_task = task;
    task->state = TASK_RUNNING;

    if (context_save(&scheduler_context) == 0) {
        arch_enter_user_mode(task->entry, task->user_stack_top);
    }

    struct task* returned_task = current_task;

    terminal_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_write(print_shell_return ? "Back in kernel shell. Task " : "Task ");
    terminal_write_dec(returned_task->id);
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

void task_run_all_ready(void) {
    uint32_t ran = 0;
    uint32_t start_cursor = scheduler_cursor;

    for (uint32_t round = 0; round < MAX_TASKS; round++) {
        struct task* next = 0;
        uint32_t next_index = 0;

        for (uint32_t scan = 0; scan < MAX_TASKS; scan++) {
            uint32_t i = (scheduler_cursor + scan) % MAX_TASKS;

            if (tasks[i].state == TASK_READY) {
                next = &tasks[i];
                next_index = i;
                break;
            }
        }

        if (next == 0) {
            scheduler_cursor = start_cursor;
            break;
        }

        scheduler_cursor = (next_index + 1) % MAX_TASKS;
        task_run_internal(next, 0);
        ran++;
    }

    if (ran == 0) {
        terminal_write("No READY tasks\n");
    }
}

void task_exit_current(uint32_t exit_code) {
    if (current_task != 0) {
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
        terminal_write(" state=");
        terminal_write(task_state_name(tasks[i].state));
        terminal_write(" entry=");
        terminal_write_hex(tasks[i].entry);
        terminal_write(" stack=");
        terminal_write_hex(tasks[i].user_stack_top);
        terminal_write(" exit=");
        terminal_write_dec(tasks[i].exit_code);
        terminal_write(" yields=");
        terminal_write_dec(tasks[i].yields);
        terminal_write("\n");
    }
}
