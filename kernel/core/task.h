#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include "idt.h"

#include <stdint.h>

#define TASK_ARGS_SIZE 64
#define TASK_NAME_SIZE 24
#define TASK_MAX_FILES 8

enum task_state {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_EXITED,
};

enum scheduler_mode {
    SCHEDULER_COOPERATIVE = 0,
    SCHEDULER_AUTO,
};

struct task_context {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ss;
    uint32_t valid;
};

struct task {
    uint32_t id;
    char name[TASK_NAME_SIZE];
    enum task_state state;
    uint32_t page_directory;
    uint32_t entry;
    uint32_t user_image_base;
    uint32_t user_image_limit;
    uint32_t user_stack_base;
    uint32_t user_stack_top;
    uint32_t kernel_stack_top;
    uint32_t exit_code;
    uint32_t yields;
    uint32_t preemptions;
    uint32_t wake_tick;
    struct task_context context;
    char args[TASK_ARGS_SIZE];
    int file_fds[TASK_MAX_FILES];
};

void task_initialize(void);
enum scheduler_mode scheduler_get_mode(void);
const char* scheduler_mode_name(enum scheduler_mode mode);
void scheduler_set_mode(enum scheduler_mode mode);
void scheduler_reset_stats(void);
uint32_t scheduler_preemption_count(void);
void scheduler_print_status(void);
void scheduler_tick(void);
int scheduler_preempt_if_needed(struct interrupt_frame* frame);
uint32_t scheduler_service_pending(void);
uint32_t task_next_id(void);
uint32_t task_current_id(void);
uint32_t task_copy_current_args(char* buffer, uint32_t size);
int task_current_add_file(int vfs_fd);
int task_current_get_file(uint32_t task_fd);
int task_current_close_file(uint32_t task_fd);
struct task* task_create_user(uint32_t entry, uint32_t user_stack_top);
struct task* task_create_user_named(const char* name, uint32_t entry, uint32_t user_stack_top);
struct task* task_create_user_with_args(const char* name, uint32_t entry, uint32_t user_stack_top, const char* args);
void task_set_user_memory(struct task* task, uint32_t image_base, uint32_t image_limit,
    uint32_t stack_base, uint32_t stack_top);
int task_current_user_range_is_valid(uint32_t address, uint32_t length);
void task_run(struct task* task);
void task_run_all_ready(void);
uint32_t task_run_all_ready_until_idle(void);
int task_has_ready(void);
int task_kill(uint32_t id, uint32_t exit_code);
int task_wait(uint32_t id, uint32_t* exit_code);
int task_get_exit_status(uint32_t id, uint32_t* exit_code);
int task_reap(uint32_t id);
uint32_t task_reap_exited(void);
void task_exit_current(uint32_t exit_code);
void task_yield_current(struct interrupt_frame* frame);
void task_sleep_current(struct interrupt_frame* frame, uint32_t ticks);
void task_prepare_exit_return(struct interrupt_frame* frame, uint32_t exit_code);
void task_prepare_yield_return(struct interrupt_frame* frame);
void task_prepare_sleep_return(struct interrupt_frame* frame, uint32_t ticks);
void task_print_all(void);
void task_print_all_verbose(void);
int task_print_context(uint32_t id);
void task_print_summary(void);

#endif
