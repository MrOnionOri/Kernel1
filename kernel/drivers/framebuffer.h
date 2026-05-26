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
};

void framebuffer_initialize(void);
int framebuffer_available(void);
const struct framebuffer_info* framebuffer_get_info(void);
void framebuffer_clear(uint32_t color);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void framebuffer_test_pattern(void);
void framebuffer_demo_desktop(void);
void framebuffer_print_info(void);
void framebuffer_print_preview(void);

#endif
