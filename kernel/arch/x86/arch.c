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

int arch_map_range(uint32_t virtual_address, uint32_t physical_address, uint32_t length, uint32_t flags) {
    uint32_t x86_flags = 0;

    if (flags & ARCH_PAGE_WRITABLE) {
        x86_flags |= VMM_PAGE_WRITABLE;
    }

    if (flags & ARCH_PAGE_USER) {
        x86_flags |= VMM_PAGE_USER;
    }

    return vmm_map_range(virtual_address, physical_address, length, x86_flags);
}

uint32_t arch_get_physical(uint32_t virtual_address) {
    return vmm_get_physical(virtual_address);
}

uint32_t arch_create_address_space(void) {
    return vmm_create_process_directory();
}

void arch_free_address_space(uint32_t directory_physical) {
    vmm_free_process_directory(directory_physical);
}

int arch_map_address_space_page(uint32_t directory_physical, uint32_t virtual_address,
        uint32_t physical_address, uint32_t flags) {
    uint32_t x86_flags = 0;

    if (flags & ARCH_PAGE_WRITABLE) {
        x86_flags |= VMM_PAGE_WRITABLE;
    }

    if (flags & ARCH_PAGE_USER) {
        x86_flags |= VMM_PAGE_USER;
    }

    return vmm_map_process_page(directory_physical, virtual_address, physical_address, x86_flags);
}

void arch_switch_address_space(uint32_t directory_physical) {
    vmm_switch_directory(directory_physical);
}

void arch_zero_physical_page(uint32_t physical_address) {
    vmm_zero_physical_page(physical_address);
}

void arch_copy_to_physical(uint32_t physical_address, const void* source, uint32_t length) {
    vmm_copy_to_physical(physical_address, source, length);
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
