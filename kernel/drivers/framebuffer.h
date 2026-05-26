#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdint.h>

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t address;
    int hardware_backed;
    int vbe_candidate_found;
    uint32_t candidate_address;
    uint32_t candidate_width;
    uint32_t candidate_height;
    uint32_t candidate_pitch;
    uint32_t candidate_bits_per_pixel;
};

void framebuffer_initialize(void);
int framebuffer_available(void);
const struct framebuffer_info* framebuffer_get_info(void);
void framebuffer_clear(uint32_t color);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void framebuffer_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void framebuffer_write_text(uint32_t x, uint32_t y, const char* text, uint32_t color);
void framebuffer_test_pattern(void);
void framebuffer_demo_desktop(void);
void framebuffer_draw_status_panel(uint32_t ticks, uint32_t used_pages, uint32_t free_pages,
    const char* scheduler_mode, uint32_t preemptions);
void framebuffer_console_reset(void);
void framebuffer_console_write(const char* text);
void framebuffer_console_cursor_left(void);
void framebuffer_console_cursor_right(void);
void framebuffer_console_backspace(void);
void framebuffer_demo_console(void);
void framebuffer_print_info(void);
void framebuffer_print_preview(void);

#endif
