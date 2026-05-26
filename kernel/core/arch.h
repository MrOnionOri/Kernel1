#ifndef KERNEL_CORE_ARCH_H
#define KERNEL_CORE_ARCH_H

#include <stdint.h>

#define ARCH_PAGE_WRITABLE 0x002
#define ARCH_PAGE_USER 0x004

struct task_context;

void arch_initialize(void);
void arch_enable_interrupts(void);
void arch_halt(void);
void arch_enable_irq(uint8_t irq);
int arch_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
uint32_t arch_get_physical(uint32_t virtual_address);
uint32_t arch_create_address_space(void);
void arch_free_address_space(uint32_t directory_physical);
int arch_map_address_space_page(uint32_t directory_physical, uint32_t virtual_address,
    uint32_t physical_address, uint32_t flags);
void arch_switch_address_space(uint32_t directory_physical);
void arch_zero_physical_page(uint32_t physical_address);
void arch_copy_to_physical(uint32_t physical_address, const void* source, uint32_t length);
void arch_set_kernel_stack(uint32_t stack);
void arch_enter_user_mode(uint32_t entry, uint32_t user_stack);
void arch_enter_user_context(const struct task_context* context);
void arch_test_mapping(void);
void arch_print_status(void);

#endif
