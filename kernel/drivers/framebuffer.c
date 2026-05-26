#include "framebuffer.h"

#include "terminal.h"

#define FB_STUB_WIDTH 320
#define FB_STUB_HEIGHT 200
#define FB_STUB_BPP 32
#define FB_STUB_PITCH (FB_STUB_WIDTH * 4)

static uint32_t framebuffer_stub[FB_STUB_WIDTH * FB_STUB_HEIGHT];
static struct framebuffer_info framebuffer;

void framebuffer_initialize(void) {
    framebuffer.width = FB_STUB_WIDTH;
    framebuffer.height = FB_STUB_HEIGHT;
    framebuffer.pitch = FB_STUB_PITCH;
    framebuffer.bits_per_pixel = FB_STUB_BPP;
    framebuffer.address = (uint32_t)framebuffer_stub;
    framebuffer.hardware_backed = 0;
    framebuffer_clear(0x00000000);
}

int framebuffer_available(void) {
    return framebuffer.address != 0;
}

const struct framebuffer_info* framebuffer_get_info(void) {
    return &framebuffer;
}

void framebuffer_clear(uint32_t color) {
    if (!framebuffer_available()) {
        return;
    }

    for (uint32_t y = 0; y < framebuffer.height; y++) {
        for (uint32_t x = 0; x < framebuffer.width; x++) {
            framebuffer_stub[y * framebuffer.width + x] = color;
        }
    }
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer_available() || x >= framebuffer.width || y >= framebuffer.height) {
        return;
    }

    framebuffer_stub[y * framebuffer.width + x] = color;
}

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!framebuffer_available()) {
        return;
    }

    for (uint32_t row = 0; row < height; row++) {
        uint32_t py = y + row;
        if (py >= framebuffer.height) {
            break;
        }

        for (uint32_t col = 0; col < width; col++) {
            uint32_t px = x + col;
            if (px >= framebuffer.width) {
                break;
            }

            framebuffer_put_pixel(px, py, color);
        }
    }
}

void framebuffer_test_pattern(void) {
    if (!framebuffer_available()) {
        return;
    }

    framebuffer_clear(0x00101018);
    framebuffer_fill_rect(12, 12, 296, 28, 0x0045A3FF);
    framebuffer_fill_rect(12, 52, 90, 70, 0x00FF6B6B);
    framebuffer_fill_rect(114, 52, 90, 70, 0x0048D597);
    framebuffer_fill_rect(216, 52, 92, 70, 0x00FFD166);
    framebuffer_fill_rect(12, 136, 296, 52, 0x00252535);

    for (uint32_t i = 0; i < 128; i++) {
        framebuffer_put_pixel(32 + i, 152 + (i % 24), 0x00FFFFFF);
        framebuffer_put_pixel(184 + i, 176 - (i % 24), 0x00B8C0FF);
    }
}

static void framebuffer_draw_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
        uint32_t title_color, uint32_t body_color) {
    framebuffer_fill_rect(x + 3, y + 3, width, height, 0x00000000);
    framebuffer_fill_rect(x, y, width, height, body_color);
    framebuffer_fill_rect(x, y, width, 12, title_color);
    framebuffer_fill_rect(x + 4, y + 4, 4, 4, 0x00FF5F57);
    framebuffer_fill_rect(x + 12, y + 4, 4, 4, 0x00FFBD2E);
    framebuffer_fill_rect(x + 20, y + 4, 4, 4, 0x0028C840);
}

void framebuffer_demo_desktop(void) {
    framebuffer_clear(0x00181A20);
    framebuffer_fill_rect(0, 0, framebuffer.width, 18, 0x00262A33);
    framebuffer_fill_rect(0, framebuffer.height - 22, framebuffer.width, 22, 0x00262A33);
    framebuffer_draw_window(20, 34, 128, 86, 0x0045A3FF, 0x00333A46);
    framebuffer_draw_window(172, 48, 116, 104, 0x0048D597, 0x00343F38);
    framebuffer_fill_rect(36, 58, 92, 8, 0x00E8EAED);
    framebuffer_fill_rect(36, 74, 72, 8, 0x009EA7B3);
    framebuffer_fill_rect(188, 74, 76, 10, 0x00FFD166);
    framebuffer_fill_rect(188, 94, 56, 10, 0x00FF6B6B);
    framebuffer_fill_rect(130, 164, 60, 12, 0x0045A3FF);
}

void framebuffer_print_info(void) {
    terminal_write("Framebuffer:\n  mode=");
    terminal_write(framebuffer.hardware_backed ? "hardware" : "stub");
    terminal_write("\n  size=");
    terminal_write_dec(framebuffer.width);
    terminal_putchar('x');
    terminal_write_dec(framebuffer.height);
    terminal_write("x");
    terminal_write_dec(framebuffer.bits_per_pixel);
    terminal_write("\n  pitch=");
    terminal_write_dec(framebuffer.pitch);
    terminal_write("\n  address=");
    terminal_write_hex(framebuffer.address);
    terminal_write("\n");
}

void framebuffer_print_preview(void) {
    static const char ramp[] = " .:-=+*#%@";

    if (!framebuffer_available()) {
        terminal_write("Framebuffer unavailable\n");
        return;
    }

    terminal_write("Framebuffer preview:\n");
    for (uint32_t y = 0; y < 25; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            uint32_t source_x = (x * framebuffer.width) / 64;
            uint32_t source_y = (y * framebuffer.height) / 25;
            uint32_t color = framebuffer_stub[source_y * framebuffer.width + source_x];
            uint32_t red = (color >> 16) & 0xFF;
            uint32_t green = (color >> 8) & 0xFF;
            uint32_t blue = color & 0xFF;
            uint32_t luma = (red * 30 + green * 59 + blue * 11) / 100;
            terminal_putchar(ramp[(luma * 9) / 255]);
        }
        terminal_putchar('\n');
    }
}
