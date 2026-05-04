#include "pmm.h"

#include "memory_map.h"
#include "terminal.h"

#include <stddef.h>

#define PMM_MAX_MEMORY (128 * 1024 * 1024)
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PMM_PAGE_SIZE)
#define PMM_BITMAP_WORDS (PMM_MAX_PAGES / 32)

extern uint8_t kernel_end;

static uint32_t page_bitmap[PMM_BITMAP_WORDS];
static uint32_t managed_pages;
static uint32_t used_pages;

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t align_down(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1);
}

static void bitmap_set(uint32_t page_index) {
    page_bitmap[page_index / 32] |= (1u << (page_index % 32));
}

static void bitmap_clear(uint32_t page_index) {
    page_bitmap[page_index / 32] &= ~(1u << (page_index % 32));
}

static int bitmap_test(uint32_t page_index) {
    return (page_bitmap[page_index / 32] & (1u << (page_index % 32))) != 0;
}

static void mark_region_free(uint32_t base, uint32_t length) {
    uint32_t start = align_up(base, PMM_PAGE_SIZE);
    uint32_t end = align_down(base + length, PMM_PAGE_SIZE);

    if (end > PMM_MAX_MEMORY) {
        end = PMM_MAX_MEMORY;
    }

    for (uint32_t address = start; address < end; address += PMM_PAGE_SIZE) {
        uint32_t page_index = address / PMM_PAGE_SIZE;

        if (page_index < PMM_MAX_PAGES && bitmap_test(page_index)) {
            bitmap_clear(page_index);
            used_pages--;
        }
    }
}

static void mark_region_used(uint32_t base, uint32_t length) {
    uint32_t start = align_down(base, PMM_PAGE_SIZE);
    uint32_t end = align_up(base + length, PMM_PAGE_SIZE);

    if (end > PMM_MAX_MEMORY) {
        end = PMM_MAX_MEMORY;
    }

    for (uint32_t address = start; address < end; address += PMM_PAGE_SIZE) {
        uint32_t page_index = address / PMM_PAGE_SIZE;

        if (page_index < PMM_MAX_PAGES && !bitmap_test(page_index)) {
            bitmap_set(page_index);
            used_pages++;
        }
    }
}

void pmm_initialize(void) {
    managed_pages = 0;
    used_pages = PMM_MAX_PAGES;

    for (size_t i = 0; i < PMM_BITMAP_WORDS; i++) {
        page_bitmap[i] = 0xFFFFFFFF;
    }

    uint32_t count = memory_map_count();
    const struct memory_map_entry* entries = memory_map_entries();

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != 1 || entries[i].base > 0xFFFFFFFF) {
            continue;
        }

        uint32_t base = (uint32_t)entries[i].base;
        uint32_t length = entries[i].length > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32_t)entries[i].length;
        mark_region_free(base, length);
    }

    mark_region_used(0x00000000, 0x00100000);

    if ((uint32_t)&kernel_end > 0x00100000) {
        mark_region_used(0x00100000, align_up((uint32_t)&kernel_end, PMM_PAGE_SIZE) - 0x00100000);
    }

    managed_pages = PMM_MAX_PAGES - used_pages;
    used_pages = 0;

    for (uint32_t page_index = 0; page_index < PMM_MAX_PAGES; page_index++) {
        if (bitmap_test(page_index)) {
            used_pages++;
        }
    }
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t page_index = 0; page_index < PMM_MAX_PAGES; page_index++) {
        if (!bitmap_test(page_index)) {
            bitmap_set(page_index);
            used_pages++;
            return page_index * PMM_PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_page(uint32_t address) {
    if ((address % PMM_PAGE_SIZE) != 0 || address >= PMM_MAX_MEMORY) {
        return;
    }

    uint32_t page_index = address / PMM_PAGE_SIZE;
    if (bitmap_test(page_index)) {
        bitmap_clear(page_index);
        used_pages--;
    }
}

void pmm_print_stats(void) {
    terminal_write("PMM page size: ");
    terminal_write_dec(PMM_PAGE_SIZE);
    terminal_write(" bytes\n");

    terminal_write("PMM managed: ");
    terminal_write_dec(PMM_MAX_PAGES);
    terminal_write(" pages / ");
    terminal_write_dec((PMM_MAX_PAGES * PMM_PAGE_SIZE) / (1024 * 1024));
    terminal_write(" MiB\n");

    terminal_write("PMM usable after reserves: ");
    terminal_write_dec(PMM_MAX_PAGES - used_pages);
    terminal_write(" pages\n");

    terminal_write("PMM used: ");
    terminal_write_dec(used_pages);
    terminal_write(" pages\n");

    terminal_write("PMM initially free: ");
    terminal_write_dec(managed_pages);
    terminal_write(" pages\n");

    terminal_write("Kernel end: ");
    terminal_write_hex((uint32_t)&kernel_end);
    terminal_write("\n");
}
