#ifndef KERNEL_CORE_ARCH_H
#define KERNEL_CORE_ARCH_H

#include <stdint.h>

#define ARCH_PAGE_WRITABLE 0x002
#define ARCH_PAGE_USER 0x004

void arch_initialize(void);
void arch_enable_interrupts(void);
void arch_halt(void);
void arch_enable_irq(uint8_t irq);
int arch_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
uint32_t arch_get_physical(uint32_t virtual_address);
void arch_set_kernel_stack(uint32_t stack);
void arch_enter_user_mode(uint32_t entry, uint32_t user_stack);
void arch_test_mapping(void);
void arch_print_status(void);

#endif
