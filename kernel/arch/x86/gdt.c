#include "gdt.h"

#include "terminal.h"

#define GDT_ENTRIES 6
#define GDT_ACCESS_PRESENT 0x80
#define GDT_ACCESS_RING0 0x00
#define GDT_ACCESS_RING3 0x60
#define GDT_ACCESS_CODE_DATA 0x10
#define GDT_ACCESS_EXECUTABLE 0x08
#define GDT_ACCESS_DIRECTION_CONFORMING 0x04
#define GDT_ACCESS_READ_WRITE 0x02
#define GDT_GRANULARITY_4K 0x80
#define GDT_GRANULARITY_32BIT 0x40
#define TSS_ACCESS 0x89

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern void gdt_flush(uint32_t gdt_pointer_address);
extern void tss_flush(void);

extern uint8_t stack_top;

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_pointer gdt_ptr;
static struct tss_entry tss;

static void memory_zero(void* pointer, uint32_t size) {
    uint8_t* bytes = (uint8_t*)pointer;

    for (uint32_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static void gdt_set_entry(uint32_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);

    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= granularity & 0xF0;
    gdt[index].access = access;
}

static void tss_initialize(void) {
    memory_zero(&tss, sizeof(tss));

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = (uint32_t)&stack_top;
    tss.cs = GDT_USER_CODE;
    tss.ss = GDT_USER_DATA;
    tss.ds = GDT_USER_DATA;
    tss.es = GDT_USER_DATA;
    tss.fs = GDT_USER_DATA;
    tss.gs = GDT_USER_DATA;
    tss.iomap_base = sizeof(tss);

    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss) - 1, TSS_ACCESS, 0);
}

void gdt_initialize(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
            GDT_ACCESS_EXECUTABLE | GDT_ACCESS_READ_WRITE,
        GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);
    gdt_set_entry(2, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA |
            GDT_ACCESS_READ_WRITE,
        GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);
    gdt_set_entry(3, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
            GDT_ACCESS_EXECUTABLE | GDT_ACCESS_READ_WRITE,
        GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);
    gdt_set_entry(4, 0, 0xFFFFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA |
            GDT_ACCESS_READ_WRITE,
        GDT_GRANULARITY_4K | GDT_GRANULARITY_32BIT);

    tss_initialize();
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

void gdt_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

void gdt_print_status(void) {
    terminal_write("GDT base: ");
    terminal_write_hex(gdt_ptr.base);
    terminal_write(" limit: ");
    terminal_write_hex(gdt_ptr.limit);
    terminal_write("\n");

    terminal_write("Selectors kernel code/data: ");
    terminal_write_hex(GDT_KERNEL_CODE);
    terminal_putchar('/');
    terminal_write_hex(GDT_KERNEL_DATA);
    terminal_write("\n");

    terminal_write("Selectors user code/data: ");
    terminal_write_hex(GDT_USER_CODE);
    terminal_putchar('/');
    terminal_write_hex(GDT_USER_DATA);
    terminal_write("\n");

    terminal_write("TSS selector: ");
    terminal_write_hex(GDT_TSS);
    terminal_write(" esp0: ");
    terminal_write_hex(tss.esp0);
    terminal_write(" ss0: ");
    terminal_write_hex(tss.ss0);
    terminal_write("\n");
}
