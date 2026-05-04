#include "idt.h"

#include "keyboard.h"
#include "pic.h"
#include "syscall.h"
#include "terminal.h"
#include "timer.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SEGMENT 0x08
#define IDT_INTERRUPT_GATE 0x8E
#define IDT_USER_INTERRUPT_GATE 0xEE

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_pointer idt_ptr;

#define ISR_DECL(n) extern void isr##n(void)
ISR_DECL(0);  ISR_DECL(1);  ISR_DECL(2);  ISR_DECL(3);
ISR_DECL(4);  ISR_DECL(5);  ISR_DECL(6);  ISR_DECL(7);
ISR_DECL(8);  ISR_DECL(9);  ISR_DECL(10); ISR_DECL(11);
ISR_DECL(12); ISR_DECL(13); ISR_DECL(14); ISR_DECL(15);
ISR_DECL(16); ISR_DECL(17); ISR_DECL(18); ISR_DECL(19);
ISR_DECL(20); ISR_DECL(21); ISR_DECL(22); ISR_DECL(23);
ISR_DECL(24); ISR_DECL(25); ISR_DECL(26); ISR_DECL(27);
ISR_DECL(28); ISR_DECL(29); ISR_DECL(30); ISR_DECL(31);
ISR_DECL(32); ISR_DECL(33); ISR_DECL(34); ISR_DECL(35);
ISR_DECL(36); ISR_DECL(37); ISR_DECL(38); ISR_DECL(39);
ISR_DECL(40); ISR_DECL(41); ISR_DECL(42); ISR_DECL(43);
ISR_DECL(44); ISR_DECL(45); ISR_DECL(46); ISR_DECL(47);
ISR_DECL(128);

static const char* exception_messages[] = {
    "Divide by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception",
    "Reserved",
};

static void idt_set_gate(uint8_t index, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[index].base_low = (uint16_t)(base & 0xFFFF);
    idt[index].selector = selector;
    idt[index].zero = 0;
    idt[index].flags = flags;
    idt[index].base_high = (uint16_t)((base >> 16) & 0xFFFF);
}

static void idt_load(void) {
    __asm__ volatile("lidt (%0)" : : "r"(&idt_ptr));
}

void idt_initialize(void) {
    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base = (uint32_t)&idt;

    for (uint16_t i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

#define SET_ISR(n) idt_set_gate(n, (uint32_t)isr##n, KERNEL_CODE_SEGMENT, IDT_INTERRUPT_GATE)
    SET_ISR(0);  SET_ISR(1);  SET_ISR(2);  SET_ISR(3);
    SET_ISR(4);  SET_ISR(5);  SET_ISR(6);  SET_ISR(7);
    SET_ISR(8);  SET_ISR(9);  SET_ISR(10); SET_ISR(11);
    SET_ISR(12); SET_ISR(13); SET_ISR(14); SET_ISR(15);
    SET_ISR(16); SET_ISR(17); SET_ISR(18); SET_ISR(19);
    SET_ISR(20); SET_ISR(21); SET_ISR(22); SET_ISR(23);
    SET_ISR(24); SET_ISR(25); SET_ISR(26); SET_ISR(27);
    SET_ISR(28); SET_ISR(29); SET_ISR(30); SET_ISR(31);
    SET_ISR(32); SET_ISR(33); SET_ISR(34); SET_ISR(35);
    SET_ISR(36); SET_ISR(37); SET_ISR(38); SET_ISR(39);
    SET_ISR(40); SET_ISR(41); SET_ISR(42); SET_ISR(43);
    SET_ISR(44); SET_ISR(45); SET_ISR(46); SET_ISR(47);
    idt_set_gate(128, (uint32_t)isr128, KERNEL_CODE_SEGMENT, IDT_USER_INTERRUPT_GATE);

    idt_load();
}

static void exception_handler(struct interrupt_frame* frame) {
    terminal_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_write("\nCPU exception: ");

    if (frame->int_no < 32) {
        terminal_write(exception_messages[frame->int_no]);
    } else {
        terminal_write("Unknown interrupt");
    }

    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_write("\nVector: ");
    terminal_write_hex(frame->int_no);
    terminal_write("  Error: ");
    terminal_write_hex(frame->err_code);
    terminal_write("  EIP: ");
    terminal_write_hex(frame->eip);

    if (frame->int_no == 14) {
        uint32_t fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
        terminal_write("  CR2: ");
        terminal_write_hex(fault_address);
    }

    terminal_write("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void irq_handler(struct interrupt_frame* frame) {
    uint8_t irq = (uint8_t)(frame->int_no - PIC_REMAP_OFFSET);

    if (irq == 0) {
        timer_tick();
    } else if (irq == 1) {
        keyboard_handle_irq();
    }

    pic_send_eoi(irq);
}

static void syscall_handler(struct interrupt_frame* frame) {
    syscall_dispatch(frame);
}

void interrupt_handler(struct interrupt_frame* frame) {
    if (frame->int_no < 32) {
        exception_handler(frame);
    } else if (frame->int_no >= PIC_REMAP_OFFSET && frame->int_no < PIC_REMAP_OFFSET + 16) {
        irq_handler(frame);
    } else if (frame->int_no == 128) {
        syscall_handler(frame);
    }
}
