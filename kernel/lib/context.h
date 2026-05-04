#ifndef KERNEL_CONTEXT_H
#define KERNEL_CONTEXT_H

#include <stdint.h>

struct kernel_context {
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eip;
};

int context_save(struct kernel_context* context);
void context_restore(struct kernel_context* context);

#endif
