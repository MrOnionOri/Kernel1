#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define VMM_PAGE_PRESENT 0x001
#define VMM_PAGE_WRITABLE 0x002
#define VMM_PAGE_USER 0x004

void paging_initialize(void);
int paging_is_enabled(void);
int vmm_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
void vmm_unmap_page(uint32_t virtual_address);
uint32_t vmm_get_physical(uint32_t virtual_address);
void vmm_test_mapping(void);
void paging_print_status(void);

#endif
