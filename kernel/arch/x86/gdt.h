#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x1B
#define GDT_USER_DATA 0x23
#define GDT_TSS 0x28

void gdt_initialize(void);
void gdt_set_kernel_stack(uint32_t stack);
void gdt_print_status(void);
void user_mode_switch(uint32_t entry, uint32_t user_stack);

#endif
