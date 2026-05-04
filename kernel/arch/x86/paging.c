#include "paging.h"

#include "pmm.h"
#include "terminal.h"

#define PAGE_SIZE 4096
#define PAGE_ENTRIES 1024
#define VMM_TEST_BASE 0x00400000
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
