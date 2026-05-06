#include "heap.h"

#include "paging.h"
#include "pmm.h"
#include "terminal.h"

#define KERNEL_HEAP_BASE 0x01000000
#define KERNEL_HEAP_LIMIT 0x01400000
#define HEAP_ALLOCATION_MAGIC 0x4B314850

static uint32_t current_virtual_page;
static uint32_t next_virtual_page;
static uint32_t current_offset;
static uint32_t pages_used;
static uint32_t bytes_allocated;
static uint32_t bytes_freed;
static uint32_t free_blocks_count;

struct heap_header {
    uint32_t magic;
    uint32_t total_size;
    uint32_t requested_size;
};

struct heap_free_block {
    uint32_t size;
    struct heap_free_block* next;
};

static struct heap_free_block* free_list;

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
    bytes_freed = 0;
    free_blocks_count = 0;
    free_list = 0;
}

static void* heap_alloc_raw(size_t size, uint32_t alignment) {
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

static void* heap_alloc_from_free_list(size_t size, uint32_t alignment) {
    struct heap_free_block* previous = 0;
    struct heap_free_block* block = free_list;

    while (block != 0) {
        uint32_t block_start = (uint32_t)block;
        uint32_t payload = align_up(block_start + sizeof(struct heap_header), alignment);
        uint32_t header_address = payload - sizeof(struct heap_header);
        uint32_t total_size = (payload - block_start) + (uint32_t)size;

        if (block->size >= total_size) {
            uint32_t reusable_size = block->size - (header_address - block_start);

            if (previous == 0) {
                free_list = block->next;
            } else {
                previous->next = block->next;
            }

            free_blocks_count--;

            struct heap_header* header = (struct heap_header*)header_address;
            header->magic = HEAP_ALLOCATION_MAGIC;
            header->total_size = reusable_size;
            header->requested_size = (uint32_t)size;
            return (void*)payload;
        }

        previous = block;
        block = block->next;
    }

    return 0;
}

void* kmalloc_aligned(size_t size, uint32_t alignment) {
    if (alignment == 0) {
        alignment = 1;
    }

    if (size + sizeof(struct heap_header) + alignment <= PMM_PAGE_SIZE) {
        void* reused = heap_alloc_from_free_list(size, alignment);
        if (reused != 0) {
            return reused;
        }

        uint32_t total_size = (uint32_t)size + sizeof(struct heap_header) + alignment;
        uint32_t raw = (uint32_t)heap_alloc_raw(total_size, 4);

        if (raw == 0) {
            return 0;
        }

        uint32_t payload = align_up(raw + sizeof(struct heap_header), alignment);
        struct heap_header* header = (struct heap_header*)(payload - sizeof(struct heap_header));
        header->magic = HEAP_ALLOCATION_MAGIC;
        header->total_size = total_size;
        header->requested_size = (uint32_t)size;
        return (void*)payload;
    }

    return heap_alloc_raw(size, alignment);
}

void* kmalloc(size_t size) {
    return kmalloc_aligned(size, 4);
}

void kfree(void* pointer) {
    if (pointer == 0) {
        return;
    }

    struct heap_header* header = (struct heap_header*)((uint32_t)pointer - sizeof(struct heap_header));
    if (header->magic != HEAP_ALLOCATION_MAGIC || header->total_size < sizeof(struct heap_free_block)) {
        return;
    }

    header->magic = 0;

    struct heap_free_block* block = (struct heap_free_block*)header;
    block->size = header->total_size;
    block->next = free_list;
    free_list = block;

    bytes_freed += header->requested_size;
    free_blocks_count++;
}

void heap_print_stats(void) {
    terminal_write("Heap pages used: ");
    terminal_write_dec(pages_used);
    terminal_write("\n");

    terminal_write("Heap bytes requested: ");
    terminal_write_dec(bytes_allocated);
    terminal_write("\n");

    terminal_write("Heap bytes freed/reusable: ");
    terminal_write_dec(bytes_freed);
    terminal_write("\n");

    terminal_write("Heap free blocks: ");
    terminal_write_dec(free_blocks_count);
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
