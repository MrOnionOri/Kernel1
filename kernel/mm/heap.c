#include "heap.h"

#include "paging.h"
#include "pmm.h"
#include "terminal.h"

#define KERNEL_HEAP_BASE 0x01000000
#define KERNEL_HEAP_LIMIT 0x01400000

static uint32_t current_virtual_page;
static uint32_t next_virtual_page;
static uint32_t current_offset;
static uint32_t pages_used;
static uint32_t bytes_allocated;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static int ensure_page(void) {
    if (current_virtual_page != 0 && current_offset < PMM_PAGE_SIZE) {
        return 1;
    }

    if (next_virtual_page >= KERNEL_HEAP_LIMIT) {
        return 0;
    }

    uint32_t physical_page = pmm_alloc_page();
    if (physical_page == 0) {
        return 0;
    }

    current_virtual_page = next_virtual_page;
    next_virtual_page += PMM_PAGE_SIZE;
    current_offset = 0;

    if (!vmm_map_page(current_virtual_page, physical_page, VMM_PAGE_WRITABLE)) {
        return 0;
    }

    pages_used++;
    return 1;
}

void heap_initialize(void) {
    current_virtual_page = 0;
    next_virtual_page = KERNEL_HEAP_BASE;
    current_offset = PMM_PAGE_SIZE;
    pages_used = 0;
    bytes_allocated = 0;
}

void* kmalloc_aligned(size_t size, uint32_t alignment) {
    if (size == 0) {
        return 0;
    }

    if (alignment == 0) {
        alignment = 1;
    }

    if (!ensure_page()) {
        return 0;
    }

    uint32_t address = align_up(current_virtual_page + current_offset, alignment);
    uint32_t new_offset = (address - current_virtual_page) + (uint32_t)size;

    if (new_offset > PMM_PAGE_SIZE) {
        current_virtual_page = 0;
        current_offset = PMM_PAGE_SIZE;

        if (!ensure_page()) {
            return 0;
        }

        address = align_up(current_virtual_page, alignment);
        new_offset = (address - current_virtual_page) + (uint32_t)size;

        if (new_offset > PMM_PAGE_SIZE) {
            return 0;
        }
    }

    current_offset = new_offset;
    bytes_allocated += (uint32_t)size;
    return (void*)address;
}

void* kmalloc(size_t size) {
    return kmalloc_aligned(size, 4);
}

void heap_print_stats(void) {
    terminal_write("Heap pages used: ");
    terminal_write_dec(pages_used);
    terminal_write("\n");

    terminal_write("Heap bytes requested: ");
    terminal_write_dec(bytes_allocated);
    terminal_write("\n");

    terminal_write("Heap virtual range: ");
    terminal_write_hex(KERNEL_HEAP_BASE);
    terminal_write("-");
    terminal_write_hex(KERNEL_HEAP_LIMIT - 1);
    terminal_write("\n");

    terminal_write("Heap current virtual page: ");
    terminal_write_hex(current_virtual_page);
    terminal_write("\n");

    terminal_write("Heap current physical page: ");
    terminal_write_hex(current_virtual_page == 0 ? 0 : vmm_get_physical(current_virtual_page));
    terminal_write("\n");

    terminal_write("Heap current offset: ");
    terminal_write_dec(current_offset);
    terminal_write("\n");
}
