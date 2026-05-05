#ifndef KERNEL_USER_MODE_H
#define KERNEL_USER_MODE_H

#include <stdint.h>

struct task;

void user_mode_enter_test(void);
void user_mode_spawn_test(void);
struct task* user_mode_spawn_app(const char* name, uint32_t entry);
struct task* user_mode_spawn_app_with_args(const char* name, uint32_t entry, const char* args);
void user_mode_switch(uint32_t entry, uint32_t user_stack);

#endif
