#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_initialize(void);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, uint32_t alignment);
void kfree(void* pointer);
void heap_print_stats(void);

#endif
