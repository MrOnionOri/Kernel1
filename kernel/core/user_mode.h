#ifndef KERNEL_USER_MODE_H
#define KERNEL_USER_MODE_H

#include <stdint.h>

void user_mode_enter_test(void);
void user_mode_spawn_test(void);
void user_mode_switch(uint32_t entry, uint32_t user_stack);

#endif
