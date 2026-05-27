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
void framebuffer_draw_task_panel(uint32_t ready, uint32_t running, uint32_t sleeping,
    uint32_t exited, uint32_t unused, uint32_t next_id);
void framebuffer_draw_storage_panel(int formatted, uint32_t used_sectors,
    uint32_t free_sectors, uint32_t used_data_sectors, uint32_t free_data_sectors,
    uint32_t percent_used);
void framebuffer_draw_apps_panel(uint32_t built_in_count, uint32_t kapp_count,
    const char* first_name, const char* second_name, const char* third_name);
void framebuffer_draw_files_panel(int valid, const char* path, uint32_t children,
    const char* first_name, uint32_t first_type,
    const char* second_name, uint32_t second_type,
    const char* third_name, uint32_t third_type);
void framebuffer_draw_launcher_panel(void);
void framebuffer_console_reset(void);
void framebuffer_console_write(const char* text);
void framebuffer_console_cursor_left(void);
void framebuffer_console_cursor_right(void);
void framebuffer_console_backspace(void);
void framebuffer_demo_console(void);
void framebuffer_print_info(void);
void framebuffer_print_preview(void);

#endif
