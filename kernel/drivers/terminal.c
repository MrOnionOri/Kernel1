#include "terminal.h"

#include "io.h"

#include <stddef.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

static size_t cursor_row;
static size_t cursor_col;
static uint8_t terminal_color;

static void terminal_update_cursor(void) {
    uint16_t position = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);

    outb(VGA_CTRL_REGISTER, 0x0F);
    outb(VGA_DATA_REGISTER, (uint8_t)(position & 0xFF));
    outb(VGA_CTRL_REGISTER, 0x0E);
    outb(VGA_DATA_REGISTER, (uint8_t)((position >> 8) & 0xFF));
}

static void terminal_enable_cursor(void) {
    outb(VGA_CTRL_REGISTER, 0x0A);
    outb(VGA_DATA_REGISTER, 0x0E);
    outb(VGA_CTRL_REGISTER, 0x0B);
    outb(VGA_DATA_REGISTER, 0x0F);
}

static uint8_t vga_entry_color(enum vga_color foreground, enum vga_color background) {
    return (uint8_t)(foreground | background << 4);
}

static uint16_t vga_entry(unsigned char character, uint8_t color) {
    return (uint16_t)character | (uint16_t)color << 8;
}

static void terminal_scroll_lines(size_t lines) {
    if (lines == 0) {
        return;
    }

    if (lines >= VGA_HEIGHT) {
        lines = VGA_HEIGHT - 1;
    }

    for (size_t y = lines; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - lines) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (size_t y = VGA_HEIGHT - lines; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }

    if (cursor_row >= lines) {
        cursor_row -= lines;
    } else {
        cursor_row = 0;
    }
}

static void terminal_scroll(void) {
    terminal_scroll_lines(1);
}

static void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }

    cursor_row = 0;
    cursor_col = 0;
}

void terminal_initialize(void) {
    terminal_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_clear();
    terminal_enable_cursor();
    terminal_update_cursor();
}

void terminal_set_color(enum vga_color foreground, enum vga_color background) {
    terminal_color = vga_entry_color(foreground, background);
}

void terminal_ensure_rows(uint32_t rows) {
    if (rows >= VGA_HEIGHT) {
        rows = VGA_HEIGHT - 1;
    }

    if (cursor_row + rows >= VGA_HEIGHT) {
        terminal_scroll_lines((cursor_row + rows) - VGA_HEIGHT + 1);
    }
}

void terminal_putchar(char character) {
    if (character == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else {
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
            vga_entry((unsigned char)character, terminal_color);
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    if (cursor_row >= VGA_HEIGHT) {
        terminal_scroll();
    }

    terminal_update_cursor();
}

void terminal_backspace(void) {
    if (cursor_col == 0) {
        if (cursor_row == 0) {
            return;
        }

        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    } else {
        cursor_col--;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', terminal_color);
    terminal_update_cursor();
}

void terminal_write(const char* text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        terminal_putchar(text[i]);
    }
}

void terminal_write_dec(uint32_t value) {
    char buffer[11];
    size_t index = 0;

    if (value == 0) {
        terminal_putchar('0');
        return;
    }

    while (value > 0) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        terminal_putchar(buffer[--index]);
    }
}

void terminal_write_hex(uint32_t value) {
    const char* digits = "0123456789ABCDEF";

    terminal_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        terminal_putchar(digits[(value >> shift) & 0xF]);
    }
}

void terminal_write_hex64(uint64_t value) {
    terminal_write_hex((uint32_t)(value >> 32));
    terminal_putchar('_');
    terminal_write_hex((uint32_t)(value & 0xFFFFFFFF));
}
