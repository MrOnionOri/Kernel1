#include "arch.h"

#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "pic.h"
#include "timer.h"

void arch_initialize(void) {
    gdt_initialize();
    idt_initialize();
    pic_remap();
    paging_initialize();
    timer_initialize(100);
}

void arch_enable_interrupts(void) {
    __asm__ volatile("sti");
}

void arch_halt(void) {
    __asm__ volatile("hlt");
}

void arch_enable_irq(uint8_t irq) {
    pic_clear_mask(irq);
}

int arch_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags) {
    uint32_t x86_flags = 0;

    if (flags & ARCH_PAGE_WRITABLE) {
        x86_flags |= VMM_PAGE_WRITABLE;
    }

    if (flags & ARCH_PAGE_USER) {
        x86_flags |= VMM_PAGE_USER;
    }

    return vmm_map_page(virtual_address, physical_address, x86_flags);
}

uint32_t arch_get_physical(uint32_t virtual_address) {
    return vmm_get_physical(virtual_address);
}

void arch_set_kernel_stack(uint32_t stack) {
    gdt_set_kernel_stack(stack);
}

void arch_enter_user_mode(uint32_t entry, uint32_t user_stack) {
    user_mode_switch(entry, user_stack);
}

void arch_enter_user_context(const struct task_context* context) {
    user_context_switch(context);
}

void arch_test_mapping(void) {
    vmm_test_mapping();
}

void arch_print_status(void) {
    gdt_print_status();
    paging_print_status();
}
