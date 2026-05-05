#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include "idt.h"

#include <stdint.h>

#define TASK_ARGS_SIZE 64
#define TASK_NAME_SIZE 24

enum task_state {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_EXITED,
};

struct task {
    uint32_t id;
    char name[TASK_NAME_SIZE];
    enum task_state state;
    uint32_t entry;
    uint32_t user_stack_top;
    uint32_t kernel_stack_top;
    uint32_t exit_code;
    uint32_t yields;
    char args[TASK_ARGS_SIZE];
};

void task_initialize(void);
uint32_t task_next_id(void);
uint32_t task_current_id(void);
uint32_t task_copy_current_args(char* buffer, uint32_t size);
struct task* task_create_user(uint32_t entry, uint32_t user_stack_top);
struct task* task_create_user_named(const char* name, uint32_t entry, uint32_t user_stack_top);
struct task* task_create_user_with_args(const char* name, uint32_t entry, uint32_t user_stack_top, const char* args);
void task_run(struct task* task);
void task_run_all_ready(void);
int task_has_ready(void);
void task_exit_current(uint32_t exit_code);
void task_yield_current(struct interrupt_frame* frame);
void task_prepare_exit_return(struct interrupt_frame* frame, uint32_t exit_code);
void task_prepare_yield_return(struct interrupt_frame* frame);
void task_print_all(void);
void task_print_all_verbose(void);

#endif
