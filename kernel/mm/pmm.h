#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>

#define PMM_PAGE_SIZE 4096

void pmm_initialize(void);
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t address);
void pmm_print_stats(void);

#endif
