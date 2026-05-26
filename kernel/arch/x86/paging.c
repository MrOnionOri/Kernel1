#include "paging.h"

#include "pmm.h"
#include "terminal.h"

#define PAGE_SIZE 4096
#define PAGE_ENTRIES 1024
#define VMM_TEST_BASE 0x00400000
#define VMM_TEMP_BASE 0x00F00000
#define VMM_TEMP_TABLE 0x00F01000
#define VMM_TEMP_OLD_TABLE 0x00F02000
#define VMM_KERNEL_OWNED_BASE 0x00100000
#define STATIC_PAGE_TABLE_COUNT 8

static uint32_t page_directory[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t first_page_table[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t static_page_tables[STATIC_PAGE_TABLE_COUNT][PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static int enabled;
static uint32_t next_static_table;

static void load_page_directory(uint32_t address) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(address));
}

static void enable_paging(void) {
    uint32_t cr0;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

void paging_initialize(void) {
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = VMM_PAGE_WRITABLE;
        first_page_table[i] = (i * PAGE_SIZE) | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE;
    }

    for (uint32_t table = 0; table < STATIC_PAGE_TABLE_COUNT; table++) {
        for (uint32_t entry = 0; entry < PAGE_ENTRIES; entry++) {
            static_page_tables[table][entry] = 0;
        }
    }

    page_directory[0] = ((uint32_t)first_page_table) | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE;

    load_page_directory((uint32_t)page_directory);
    enable_paging();
    enabled = 1;
}

int paging_is_enabled(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & 0x80000000) != 0;
}

static void invalidate_page(uint32_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static void memory_zero(void* destination, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    for (uint32_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static void memory_copy(void* destination, const void* source, uint32_t size) {
    uint8_t* dest = (uint8_t*)destination;
    const uint8_t* src = (const uint8_t*)source;

    for (uint32_t i = 0; i < size; i++) {
        dest[i] = src[i];
    }
}

static uint32_t* ensure_page_table(uint32_t directory_index, uint32_t flags) {
    if (page_directory[directory_index] & VMM_PAGE_PRESENT) {
        page_directory[directory_index] |= flags & 0xFFF;
        return (uint32_t*)(page_directory[directory_index] & 0xFFFFF000);
    }

    if (next_static_table >= STATIC_PAGE_TABLE_COUNT) {
        return 0;
    }

    uint32_t* table = static_page_tables[next_static_table++];
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        table[i] = 0;
    }

    page_directory[directory_index] = ((uint32_t)table) | VMM_PAGE_PRESENT | (flags & 0xFFF);
    return table;
}

int vmm_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags) {
    virtual_address &= 0xFFFFF000;
    physical_address &= 0xFFFFF000;

    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FF;
    uint32_t* table = ensure_page_table(directory_index, flags | VMM_PAGE_WRITABLE);

    if (table == 0) {
        return 0;
    }

    table[table_index] = physical_address | VMM_PAGE_PRESENT | (flags & 0xFFF);
    invalidate_page(virtual_address);
    return 1;
}

void vmm_unmap_page(uint32_t virtual_address) {
    virtual_address &= 0xFFFFF000;

    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FF;

    if ((page_directory[directory_index] & VMM_PAGE_PRESENT) == 0) {
        return;
    }

    uint32_t* table = (uint32_t*)(page_directory[directory_index] & 0xFFFFF000);
    table[table_index] = 0;
    invalidate_page(virtual_address);
}

uint32_t vmm_get_physical(uint32_t virtual_address) {
    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FF;

    if ((page_directory[directory_index] & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    uint32_t* table = (uint32_t*)(page_directory[directory_index] & 0xFFFFF000);
    if ((table[table_index] & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    return (table[table_index] & 0xFFFFF000) | (virtual_address & 0xFFF);
}

static uint32_t* map_temporary(uint32_t virtual_address, uint32_t physical_address) {
    if (!vmm_map_page(virtual_address, physical_address, VMM_PAGE_WRITABLE)) {
        return 0;
    }

    return (uint32_t*)virtual_address;
}

static int process_entry_uses_kernel_table(uint32_t directory_index, uint32_t entry) {
    if ((entry & VMM_PAGE_PRESENT) == 0 ||
            (page_directory[directory_index] & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    return (entry & 0xFFFFF000) == (page_directory[directory_index] & 0xFFFFF000);
}

uint32_t vmm_create_process_directory(void) {
    uint32_t directory_physical = pmm_alloc_page();

    if (directory_physical == 0) {
        return 0;
    }

    uint32_t* directory = map_temporary(VMM_TEMP_BASE, directory_physical);
    if (directory == 0) {
        pmm_free_page(directory_physical);
        return 0;
    }

    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        directory[i] = 0;
        if ((page_directory[i] & VMM_PAGE_PRESENT) != 0 &&
                (page_directory[i] & VMM_PAGE_USER) == 0) {
            directory[i] = page_directory[i];
        }
    }

    vmm_unmap_page(VMM_TEMP_BASE);
    return directory_physical;
}

void vmm_free_process_directory(uint32_t directory_physical) {
    if (directory_physical == 0) {
        return;
    }

    uint32_t* directory = map_temporary(VMM_TEMP_BASE, directory_physical);
    if (directory == 0) {
        return;
    }

    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        if ((directory[i] & VMM_PAGE_PRESENT) == 0 ||
                process_entry_uses_kernel_table(i, directory[i])) {
            continue;
        }

        uint32_t table_physical = directory[i] & 0xFFFFF000;
        uint32_t* table = map_temporary(VMM_TEMP_TABLE, table_physical);
        if (table != 0) {
            for (uint32_t entry = 0; entry < PAGE_ENTRIES; entry++) {
                if ((table[entry] & (VMM_PAGE_PRESENT | VMM_PAGE_USER)) ==
                        (VMM_PAGE_PRESENT | VMM_PAGE_USER)) {
                    uint32_t physical = table[entry] & 0xFFFFF000;
                    if (physical >= VMM_KERNEL_OWNED_BASE) {
                        pmm_free_page(physical);
                    }
                }
            }
            vmm_unmap_page(VMM_TEMP_TABLE);
        }

        pmm_free_page(table_physical);
    }

    vmm_unmap_page(VMM_TEMP_BASE);
    pmm_free_page(directory_physical);
}

static uint32_t clone_process_table(uint32_t* directory, uint32_t directory_index,
        uint32_t old_entry, uint32_t flags) {
    uint32_t old_table_physical = old_entry & 0xFFFFF000;
    uint32_t new_table_physical = pmm_alloc_page();

    if (new_table_physical == 0) {
        return 0;
    }

    uint32_t* old_table = map_temporary(VMM_TEMP_OLD_TABLE, old_table_physical);
    uint32_t* new_table = map_temporary(VMM_TEMP_TABLE, new_table_physical);
    if (old_table == 0 || new_table == 0) {
        if (old_table != 0) {
            vmm_unmap_page(VMM_TEMP_OLD_TABLE);
        }
        if (new_table != 0) {
            vmm_unmap_page(VMM_TEMP_TABLE);
        }
        pmm_free_page(new_table_physical);
        return 0;
    }

    for (uint32_t i = 0; i < PAGE_ENTRIES; i++) {
        new_table[i] = old_table[i];
    }

    vmm_unmap_page(VMM_TEMP_OLD_TABLE);
    vmm_unmap_page(VMM_TEMP_TABLE);
    directory[directory_index] = new_table_physical | VMM_PAGE_PRESENT |
        ((old_entry | flags) & 0xFFF);
    return new_table_physical;
}

int vmm_map_process_page(uint32_t directory_physical, uint32_t virtual_address,
        uint32_t physical_address, uint32_t flags) {
    if (directory_physical == 0) {
        return vmm_map_page(virtual_address, physical_address, flags);
    }

    virtual_address &= 0xFFFFF000;
    physical_address &= 0xFFFFF000;

    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FF;
    uint32_t* directory = map_temporary(VMM_TEMP_BASE, directory_physical);

    if (directory == 0) {
        return 0;
    }

    uint32_t table_physical = 0;
    uint32_t entry = directory[directory_index];

    if ((entry & VMM_PAGE_PRESENT) == 0) {
        table_physical = pmm_alloc_page();
        if (table_physical == 0) {
            vmm_unmap_page(VMM_TEMP_BASE);
            return 0;
        }

        uint32_t* new_table = map_temporary(VMM_TEMP_TABLE, table_physical);
        if (new_table == 0) {
            pmm_free_page(table_physical);
            vmm_unmap_page(VMM_TEMP_BASE);
            return 0;
        }

        memory_zero(new_table, PAGE_SIZE);
        vmm_unmap_page(VMM_TEMP_TABLE);
        directory[directory_index] = table_physical | VMM_PAGE_PRESENT |
            (flags & 0xFFF);
    } else if ((flags & VMM_PAGE_USER) != 0 &&
            process_entry_uses_kernel_table(directory_index, entry)) {
        table_physical = clone_process_table(directory, directory_index, entry, flags);
        if (table_physical == 0) {
            vmm_unmap_page(VMM_TEMP_BASE);
            return 0;
        }
    } else {
        directory[directory_index] |= flags & 0xFFF;
        table_physical = directory[directory_index] & 0xFFFFF000;
    }

    uint32_t* table = map_temporary(VMM_TEMP_TABLE, table_physical);
    if (table == 0) {
        vmm_unmap_page(VMM_TEMP_BASE);
        return 0;
    }

    table[table_index] = physical_address | VMM_PAGE_PRESENT | (flags & 0xFFF);

    vmm_unmap_page(VMM_TEMP_TABLE);
    vmm_unmap_page(VMM_TEMP_BASE);
    return 1;
}

void vmm_switch_directory(uint32_t directory_physical) {
    load_page_directory(directory_physical == 0 ? (uint32_t)page_directory : directory_physical);
}

void vmm_zero_physical_page(uint32_t physical_address) {
    uint32_t* page = map_temporary(VMM_TEMP_BASE, physical_address & 0xFFFFF000);

    if (page == 0) {
        return;
    }

    memory_zero(page, PAGE_SIZE);
    vmm_unmap_page(VMM_TEMP_BASE);
}

void vmm_copy_to_physical(uint32_t physical_address, const void* source, uint32_t length) {
    if (source == 0 || length == 0 || length > PAGE_SIZE) {
        return;
    }

    uint8_t* page = (uint8_t*)map_temporary(VMM_TEMP_BASE, physical_address & 0xFFFFF000);
    if (page == 0) {
        return;
    }

    memory_copy(page + (physical_address & 0xFFF), source, length);
    vmm_unmap_page(VMM_TEMP_BASE);
}

void vmm_test_mapping(void) {
    uint32_t physical = pmm_alloc_page();

    if (physical == 0) {
        terminal_write("VMM test failed: no physical page\n");
        return;
    }

    if (!vmm_map_page(VMM_TEST_BASE, physical, VMM_PAGE_WRITABLE)) {
        terminal_write("VMM test failed: map failed\n");
        return;
    }

    volatile uint32_t* test = (volatile uint32_t*)VMM_TEST_BASE;
    *test = 0xC0DEF00D;

    terminal_write("VMM mapped virtual ");
    terminal_write_hex(VMM_TEST_BASE);
    terminal_write(" -> physical ");
    terminal_write_hex(vmm_get_physical(VMM_TEST_BASE));
    terminal_write(" value=");
    terminal_write_hex(*test);
    terminal_write("\n");
}

void paging_print_status(void) {
    uint32_t cr0;
    uint32_t cr3;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    terminal_write("Paging initialized: ");
    terminal_write(enabled ? "yes" : "no");
    terminal_write("\n");

    terminal_write("CR0: ");
    terminal_write_hex(cr0);
    terminal_write("\n");

    terminal_write("CR3: ");
    terminal_write_hex(cr3);
    terminal_write("\n");

    terminal_write("Paging bit: ");
    terminal_write(paging_is_enabled() ? "on" : "off");
    terminal_write("\n");

    terminal_write("Identity mapped: 0x00000000-0x003FFFFF\n");
    terminal_write("Static VMM tables left: ");
    terminal_write_dec(STATIC_PAGE_TABLE_COUNT - next_static_table);
    terminal_write("\n");
}
