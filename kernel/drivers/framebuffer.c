#include "framebuffer.h"

#include "arch.h"
#include "terminal.h"

#define FB_STUB_WIDTH 320
#define FB_STUB_HEIGHT 200
#define FB_STUB_BPP 32
#define FB_STUB_PITCH (FB_STUB_WIDTH * 4)
#define FB_BOOT_INFO_ADDRESS 0x00008F00
#define FB_BOOT_INFO_MAGIC 0x4647314B
#define FB_BOOT_PROBE_MAGIC 0x5047314B
#define FB_CONSOLE_MAX_COLS 96
#define FB_CONSOLE_MAX_ROWS 48
#define FB_CONSOLE_CELL_WIDTH 6
#define FB_CONSOLE_CELL_HEIGHT 10
#define FB_CONSOLE_MARGIN_X 12
#define FB_CONSOLE_MARGIN_Y 24
#define FB_CONSOLE_OVERLAY_ROWS 18
#define FB_CONSOLE_OVERLAY_MARGIN 10

static uint32_t framebuffer_stub[FB_STUB_WIDTH * FB_STUB_HEIGHT];
static struct framebuffer_info framebuffer;
static char console_cells[FB_CONSOLE_MAX_ROWS][FB_CONSOLE_MAX_COLS];
static uint32_t console_cols;
static uint32_t console_rows;
static uint32_t console_cursor_col;
static uint32_t console_cursor_row;
static uint32_t console_origin_x;
static uint32_t console_origin_y;
static uint32_t console_panel_x;
static uint32_t console_panel_y;
static uint32_t console_panel_width;
static uint32_t console_panel_height;
static int console_overlay_mode;
static int console_active;

struct framebuffer_boot_info {
    uint32_t magic;
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
};

static void framebuffer_set_stub(void);

static uint32_t* framebuffer_pixel_address(uint32_t x, uint32_t y) {
    return (uint32_t*)(framebuffer.address + (y * framebuffer.pitch) + (x * 4));
}

static uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y) {
    if (!framebuffer_available() || x >= framebuffer.width || y >= framebuffer.height) {
        return 0;
    }

    return *framebuffer_pixel_address(x, y);
}

void framebuffer_initialize(void) {
    const struct framebuffer_boot_info* boot_info =
        (const struct framebuffer_boot_info*)FB_BOOT_INFO_ADDRESS;

    if (boot_info->magic == FB_BOOT_INFO_MAGIC && boot_info->address != 0 &&
            boot_info->width != 0 && boot_info->height != 0 &&
            boot_info->pitch != 0 && boot_info->bits_per_pixel == 32) {
        uint32_t bytes = boot_info->pitch * boot_info->height;
        if (arch_map_range(boot_info->address, boot_info->address, bytes, ARCH_PAGE_WRITABLE)) {
            framebuffer.width = boot_info->width;
            framebuffer.height = boot_info->height;
            framebuffer.pitch = boot_info->pitch;
            framebuffer.bits_per_pixel = boot_info->bits_per_pixel;
            framebuffer.address = boot_info->address;
            framebuffer.hardware_backed = 1;
            framebuffer.vbe_candidate_found = 1;
            framebuffer.candidate_address = boot_info->address;
            framebuffer.candidate_width = boot_info->width;
            framebuffer.candidate_height = boot_info->height;
            framebuffer.candidate_pitch = boot_info->pitch;
            framebuffer.candidate_bits_per_pixel = boot_info->bits_per_pixel;
            framebuffer_clear(0x00000000);
            return;
        }
    }

    framebuffer_set_stub();
    if (boot_info->magic == FB_BOOT_PROBE_MAGIC && boot_info->address != 0) {
        framebuffer.vbe_candidate_found = 1;
        framebuffer.candidate_address = boot_info->address;
        framebuffer.candidate_width = boot_info->width;
        framebuffer.candidate_height = boot_info->height;
        framebuffer.candidate_pitch = boot_info->pitch;
        framebuffer.candidate_bits_per_pixel = boot_info->bits_per_pixel;
    }
    framebuffer_clear(0x00000000);
}

static void framebuffer_set_stub(void) {
    framebuffer.width = FB_STUB_WIDTH;
    framebuffer.height = FB_STUB_HEIGHT;
    framebuffer.pitch = FB_STUB_PITCH;
    framebuffer.bits_per_pixel = FB_STUB_BPP;
    framebuffer.address = (uint32_t)framebuffer_stub;
    framebuffer.hardware_backed = 0;
    framebuffer.vbe_candidate_found = 0;
    framebuffer.candidate_address = 0;
    framebuffer.candidate_width = 0;
    framebuffer.candidate_height = 0;
    framebuffer.candidate_pitch = 0;
    framebuffer.candidate_bits_per_pixel = 0;
    console_active = 0;
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
            framebuffer_put_pixel(x, y, color);
        }
    }
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer_available() || x >= framebuffer.width || y >= framebuffer.height) {
        return;
    }

    *framebuffer_pixel_address(x, y) = color;
}

void framebuffer_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t dx = x0 < x1 ? (int32_t)(x1 - x0) : (int32_t)(x0 - x1);
    int32_t dy = y0 < y1 ? (int32_t)(y1 - y0) : (int32_t)(y0 - y1);
    int32_t error = dx - dy;
    int32_t x = (int32_t)x0;
    int32_t y = (int32_t)y0;

    for (;;) {
        if (x >= 0 && y >= 0) {
            framebuffer_put_pixel((uint32_t)x, (uint32_t)y, color);
        }

        if (x == (int32_t)x1 && y == (int32_t)y1) {
            break;
        }

        int32_t error2 = error * 2;
        if (error2 > -dy) {
            error -= dy;
            x += sx;
        }
        if (error2 < dx) {
            error += dx;
            y += sy;
        }
    }
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

void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (width == 0 || height == 0) {
        return;
    }

    framebuffer_draw_line(x, y, x + width - 1, y, color);
    framebuffer_draw_line(x, y, x, y + height - 1, color);
    framebuffer_draw_line(x + width - 1, y, x + width - 1, y + height - 1, color);
    framebuffer_draw_line(x, y + height - 1, x + width - 1, y + height - 1, color);
}

static uint8_t framebuffer_glyph_row(char character, uint32_t row) {
    static const uint8_t unknown[7] = {0x0E, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04};

    switch (character) {
        case ' ': { static const uint8_t g[7] = {0, 0, 0, 0, 0, 0, 0}; return g[row]; }
        case '.': { static const uint8_t g[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C}; return g[row]; }
        case ':': { static const uint8_t g[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}; return g[row]; }
        case '-': { static const uint8_t g[7] = {0, 0, 0, 0x1F, 0, 0, 0}; return g[row]; }
        case '/': { static const uint8_t g[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0, 0}; return g[row]; }
        case '0': { static const uint8_t g[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}; return g[row]; }
        case '1': { static const uint8_t g[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}; return g[row]; }
        case '2': { static const uint8_t g[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}; return g[row]; }
        case '3': { static const uint8_t g[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}; return g[row]; }
        case '4': { static const uint8_t g[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}; return g[row]; }
        case '5': { static const uint8_t g[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}; return g[row]; }
        case '6': { static const uint8_t g[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}; return g[row]; }
        case '7': { static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}; return g[row]; }
        case '8': { static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}; return g[row]; }
        case '9': { static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}; return g[row]; }
        default:
            break;
    }

    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }

    switch (character) {
        case 'A': { static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}; return g[row]; }
        case 'B': { static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}; return g[row]; }
        case 'C': { static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}; return g[row]; }
        case 'D': { static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}; return g[row]; }
        case 'E': { static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}; return g[row]; }
        case 'F': { static const uint8_t g[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}; return g[row]; }
        case 'G': { static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}; return g[row]; }
        case 'H': { static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}; return g[row]; }
        case 'I': { static const uint8_t g[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}; return g[row]; }
        case 'J': { static const uint8_t g[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}; return g[row]; }
        case 'K': { static const uint8_t g[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}; return g[row]; }
        case 'L': { static const uint8_t g[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}; return g[row]; }
        case 'M': { static const uint8_t g[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}; return g[row]; }
        case 'N': { static const uint8_t g[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}; return g[row]; }
        case 'O': { static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}; return g[row]; }
        case 'P': { static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}; return g[row]; }
        case 'Q': { static const uint8_t g[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}; return g[row]; }
        case 'R': { static const uint8_t g[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}; return g[row]; }
        case 'S': { static const uint8_t g[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}; return g[row]; }
        case 'T': { static const uint8_t g[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}; return g[row]; }
        case 'U': { static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}; return g[row]; }
        case 'V': { static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}; return g[row]; }
        case 'W': { static const uint8_t g[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}; return g[row]; }
        case 'X': { static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}; return g[row]; }
        case 'Y': { static const uint8_t g[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}; return g[row]; }
        case 'Z': { static const uint8_t g[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}; return g[row]; }
        default: return unknown[row];
    }
}

void framebuffer_write_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t cursor_x = x;

    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            cursor_x = x;
            y += 8;
            continue;
        }

        for (uint32_t row = 0; row < 7; row++) {
            uint8_t bits = framebuffer_glyph_row(text[i], row);
            for (uint32_t col = 0; col < 5; col++) {
                if (bits & (1 << (4 - col))) {
                    framebuffer_put_pixel(cursor_x + col, y + row, color);
                }
            }
        }

        cursor_x += 6;
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
    framebuffer_draw_rect(12, 12, 296, 176, 0x00FFFFFF);
    framebuffer_write_text(24, 22, "KERNEL1 GFX", 0x00FFFFFF);
    framebuffer_write_text(28, 146, "LINES / RECTS / TEXT", 0x00E8EAED);

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
    framebuffer_draw_rect(x, y, width, height, 0x00E8EAED);
    framebuffer_fill_rect(x + 4, y + 4, 4, 4, 0x00FF5F57);
    framebuffer_fill_rect(x + 12, y + 4, 4, 4, 0x00FFBD2E);
    framebuffer_fill_rect(x + 20, y + 4, 4, 4, 0x0028C840);
}

static void framebuffer_uint_to_text(uint32_t value, char* output, uint32_t output_size) {
    char reversed[12];
    uint32_t count = 0;
    uint32_t index = 0;

    if (output_size == 0) {
        return;
    }

    if (value == 0) {
        if (output_size > 1) {
            output[0] = '0';
            output[1] = '\0';
        } else {
            output[0] = '\0';
        }
        return;
    }

    while (value != 0 && count < sizeof(reversed)) {
        reversed[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count > 0 && index + 1 < output_size) {
        output[index++] = reversed[--count];
    }
    output[index] = '\0';
}

static void framebuffer_write_label_uint(uint32_t x, uint32_t y, const char* label,
        uint32_t value, uint32_t color) {
    char number[12];
    uint32_t label_width = 0;

    framebuffer_write_text(x, y, label, color);
    while (label[label_width] != '\0') {
        label_width++;
    }

    framebuffer_uint_to_text(value, number, sizeof(number));
    framebuffer_write_text(x + label_width * 6, y, number, color);
}

void framebuffer_demo_desktop(void) {
    framebuffer_clear(0x00181A20);
    framebuffer_fill_rect(0, 0, framebuffer.width, 18, 0x00262A33);
    framebuffer_fill_rect(0, framebuffer.height - 22, framebuffer.width, 22, 0x00262A33);
    framebuffer_draw_window(20, 34, 128, 86, 0x0045A3FF, 0x00333A46);
    framebuffer_draw_window(172, 48, 116, 104, 0x0048D597, 0x00343F38);
    framebuffer_write_text(8, 6, "KERNEL1", 0x00E8EAED);
    framebuffer_write_text(36, 51, "SHELL", 0x00FFFFFF);
    framebuffer_write_text(188, 65, "FILES", 0x00FFFFFF);
    framebuffer_fill_rect(36, 58, 92, 8, 0x00E8EAED);
    framebuffer_fill_rect(36, 74, 72, 8, 0x009EA7B3);
    framebuffer_fill_rect(188, 74, 76, 10, 0x00FFD166);
    framebuffer_fill_rect(188, 94, 56, 10, 0x00FF6B6B);
    framebuffer_fill_rect(130, 164, 60, 12, 0x0045A3FF);
}

void framebuffer_draw_status_panel(uint32_t ticks, uint32_t used_pages, uint32_t free_pages,
        const char* scheduler_mode, uint32_t preemptions) {
    uint32_t panel_x = framebuffer.hardware_backed ? 360 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 38 : 22;
    uint32_t panel_width = framebuffer.hardware_backed ? 290 : 250;
    uint32_t panel_height = 112;
    uint32_t total_pages = used_pages + free_pages;
    uint32_t bar_width = panel_width - 28;
    uint32_t used_width = 0;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_x + panel_width + 8 > framebuffer.width) {
        panel_x = framebuffer.width > panel_width + 12 ? framebuffer.width - panel_width - 12 : 4;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x001C2430);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x0045A3FF);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "KERNEL STATUS", 0x00FFFFFF);

    framebuffer_write_label_uint(panel_x + 12, panel_y + 26, "TICKS ", ticks, 0x00E8EAED);
    framebuffer_write_text(panel_x + 12, panel_y + 38, "SCHED ", 0x00E8EAED);
    framebuffer_write_text(panel_x + 48, panel_y + 38, scheduler_mode, 0x0048D597);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 50, "PREEMPT ", preemptions, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 62, "PMM USED ", used_pages, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 74, "PMM FREE ", free_pages, 0x00E8EAED);

    framebuffer_draw_rect(panel_x + 12, panel_y + 91, bar_width, 10, 0x009EA7B3);
    if (total_pages != 0) {
        used_width = (used_pages * (bar_width - 2)) / total_pages;
    }
    framebuffer_fill_rect(panel_x + 13, panel_y + 92, used_width, 8, 0x00FF6B6B);
    if (used_width + 2 < bar_width) {
        framebuffer_fill_rect(panel_x + 13 + used_width, panel_y + 92,
            bar_width - used_width - 2, 8, 0x0048D597);
    }
}

void framebuffer_draw_task_panel(uint32_t ready, uint32_t running, uint32_t sleeping,
        uint32_t exited, uint32_t unused, uint32_t next_id) {
    uint32_t panel_x = framebuffer.hardware_backed ? 680 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 38 : 146;
    uint32_t panel_width = framebuffer.hardware_backed ? 230 : 250;
    uint32_t panel_height = 112;
    uint32_t total = ready + running + sleeping + exited + unused;
    uint32_t active = ready + running + sleeping;
    uint32_t bar_width = panel_width - 28;
    uint32_t active_width = 0;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_x + panel_width + 8 > framebuffer.width) {
        panel_x = framebuffer.width > panel_width + 12 ? framebuffer.width - panel_width - 12 : 4;
        panel_y += 124;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x001C2430);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x0048D597);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "TASKS", 0x00FFFFFF);

    framebuffer_write_label_uint(panel_x + 12, panel_y + 26, "READY ", ready, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 38, "RUNNING ", running, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 50, "SLEEPING ", sleeping, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 62, "EXITED ", exited, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 74, "NEXT ID ", next_id, 0x00E8EAED);

    framebuffer_draw_rect(panel_x + 12, panel_y + 91, bar_width, 10, 0x009EA7B3);
    if (total != 0) {
        active_width = (active * (bar_width - 2)) / total;
    }
    framebuffer_fill_rect(panel_x + 13, panel_y + 92, active_width, 8, 0x0048D597);
    if (active_width + 2 < bar_width) {
        framebuffer_fill_rect(panel_x + 13 + active_width, panel_y + 92,
            bar_width - active_width - 2, 8, 0x00343F38);
    }
}

static uint32_t framebuffer_console_cols(void) {
    uint32_t cols = 1;
    uint32_t left_margin = framebuffer.hardware_backed ? FB_CONSOLE_OVERLAY_MARGIN : FB_CONSOLE_MARGIN_X;
    uint32_t right_margin = left_margin;

    if (framebuffer.width > left_margin + right_margin) {
        cols = (framebuffer.width - left_margin - right_margin) / FB_CONSOLE_CELL_WIDTH;
    }

    return cols > FB_CONSOLE_MAX_COLS ? FB_CONSOLE_MAX_COLS : cols;
}

static uint32_t framebuffer_console_rows(void) {
    uint32_t rows = 1;

    if (framebuffer.hardware_backed && framebuffer.height >= 360) {
        return FB_CONSOLE_OVERLAY_ROWS;
    }

    if (framebuffer.height > FB_CONSOLE_MARGIN_Y + 12) {
        rows = (framebuffer.height - FB_CONSOLE_MARGIN_Y - 12) / FB_CONSOLE_CELL_HEIGHT;
    }

    return rows > FB_CONSOLE_MAX_ROWS ? FB_CONSOLE_MAX_ROWS : rows;
}

static void framebuffer_console_configure_layout(void) {
    console_cols = framebuffer_console_cols();
    console_rows = framebuffer_console_rows();
    console_overlay_mode = framebuffer.hardware_backed && framebuffer.height >= 360;

    if (console_overlay_mode) {
        console_panel_x = FB_CONSOLE_OVERLAY_MARGIN;
        console_panel_width = framebuffer.width - FB_CONSOLE_OVERLAY_MARGIN * 2;
        console_panel_height = console_rows * FB_CONSOLE_CELL_HEIGHT + 22;
        console_panel_y = framebuffer.height - console_panel_height - FB_CONSOLE_OVERLAY_MARGIN;
        console_origin_x = console_panel_x + 6;
        console_origin_y = console_panel_y + 16;
        return;
    }

    console_panel_x = 10;
    console_panel_y = 24;
    console_panel_width = framebuffer.width > 20 ? framebuffer.width - 20 : framebuffer.width;
    console_panel_height = framebuffer.height > 38 ? framebuffer.height - 38 : framebuffer.height;
    console_origin_x = FB_CONSOLE_MARGIN_X;
    console_origin_y = FB_CONSOLE_MARGIN_Y;
}

static void framebuffer_console_draw_frame(void) {
    if (!console_overlay_mode) {
        framebuffer_clear(0x00060A0F);
        framebuffer_fill_rect(0, 0, framebuffer.width, 14, 0x0018232F);
        framebuffer_write_text(8, 4, "KERNEL1 GRAPHICAL CONSOLE", 0x00E8EAED);
    }

    framebuffer_fill_rect(console_panel_x, console_panel_y, console_panel_width, console_panel_height, 0x000B1018);
    framebuffer_fill_rect(console_panel_x, console_panel_y, console_panel_width, 13, 0x0018232F);
    framebuffer_write_text(console_panel_x + 6, console_panel_y + 4,
        console_overlay_mode ? "KERNEL1 GFX SHELL" : "KERNEL1 GRAPHICAL CONSOLE", 0x00E8EAED);
    framebuffer_draw_rect(console_panel_x, console_panel_y, console_panel_width, console_panel_height, 0x003A4A5C);
}

static void framebuffer_console_render_cell(uint32_t col, uint32_t row) {
    char text[2];
    uint32_t x = console_origin_x + col * FB_CONSOLE_CELL_WIDTH;
    uint32_t y = console_origin_y + row * FB_CONSOLE_CELL_HEIGHT;

    framebuffer_fill_rect(x, y, FB_CONSOLE_CELL_WIDTH, FB_CONSOLE_CELL_HEIGHT, 0x000B1018);
    if (console_cells[row][col] == '\0' || console_cells[row][col] == ' ') {
        return;
    }

    text[0] = console_cells[row][col];
    text[1] = '\0';
    framebuffer_write_text(x, y, text, 0x00E8EAED);
}

static void framebuffer_console_render_cursor(void) {
    uint32_t x = console_origin_x + console_cursor_col * FB_CONSOLE_CELL_WIDTH;
    uint32_t y = console_origin_y + console_cursor_row * FB_CONSOLE_CELL_HEIGHT + 8;
    framebuffer_fill_rect(x, y, 5, 1, 0x0048D597);
}

static void framebuffer_console_erase_cursor(void) {
    framebuffer_console_render_cell(console_cursor_col, console_cursor_row);
}

static void framebuffer_console_redraw(void) {
    framebuffer_console_draw_frame();
    for (uint32_t row = 0; row < console_rows; row++) {
        for (uint32_t col = 0; col < console_cols; col++) {
            framebuffer_console_render_cell(col, row);
        }
    }
    framebuffer_console_render_cursor();
}

static void framebuffer_console_scroll(void) {
    for (uint32_t row = 1; row < console_rows; row++) {
        for (uint32_t col = 0; col < console_cols; col++) {
            console_cells[row - 1][col] = console_cells[row][col];
        }
    }

    for (uint32_t col = 0; col < console_cols; col++) {
        console_cells[console_rows - 1][col] = ' ';
    }

    if (console_cursor_row > 0) {
        console_cursor_row--;
    }

    framebuffer_console_redraw();
}

static void framebuffer_console_newline(void) {
    framebuffer_console_render_cell(console_cursor_col, console_cursor_row);
    console_cursor_col = 0;
    console_cursor_row++;
    if (console_cursor_row >= console_rows) {
        framebuffer_console_scroll();
    }
}

static void framebuffer_console_putchar(char character) {
    if (!console_active) {
        framebuffer_console_reset();
    }

    if (character == '\n') {
        framebuffer_console_newline();
        framebuffer_console_render_cursor();
        return;
    }

    console_cells[console_cursor_row][console_cursor_col] = character;
    framebuffer_console_render_cell(console_cursor_col, console_cursor_row);

    console_cursor_col++;
    if (console_cursor_col >= console_cols) {
        framebuffer_console_newline();
    }

    framebuffer_console_render_cursor();
}

void framebuffer_console_reset(void) {
    framebuffer_console_configure_layout();
    console_cursor_col = 0;
    console_cursor_row = 0;
    console_active = 1;

    for (uint32_t row = 0; row < FB_CONSOLE_MAX_ROWS; row++) {
        for (uint32_t col = 0; col < FB_CONSOLE_MAX_COLS; col++) {
            console_cells[row][col] = ' ';
        }
    }

    framebuffer_console_redraw();
}

void framebuffer_console_write(const char* text) {
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        framebuffer_console_putchar(text[i]);
    }
}

void framebuffer_console_cursor_left(void) {
    if (!console_active) {
        framebuffer_console_reset();
    }

    framebuffer_console_erase_cursor();
    if (console_cursor_col == 0) {
        if (console_cursor_row > 0) {
            console_cursor_row--;
            console_cursor_col = console_cols - 1;
        }
    } else {
        console_cursor_col--;
    }
    framebuffer_console_render_cursor();
}

void framebuffer_console_cursor_right(void) {
    if (!console_active) {
        framebuffer_console_reset();
    }

    framebuffer_console_erase_cursor();
    console_cursor_col++;
    if (console_cursor_col >= console_cols) {
        console_cursor_col = 0;
        console_cursor_row++;
    }
    if (console_cursor_row >= console_rows) {
        framebuffer_console_scroll();
        return;
    }
    framebuffer_console_render_cursor();
}

void framebuffer_console_backspace(void) {
    if (!console_active) {
        framebuffer_console_reset();
    }

    framebuffer_console_erase_cursor();
    if (console_cursor_col == 0) {
        if (console_cursor_row == 0) {
            framebuffer_console_render_cursor();
            return;
        }
        console_cursor_row--;
        console_cursor_col = console_cols - 1;
    } else {
        console_cursor_col--;
    }

    console_cells[console_cursor_row][console_cursor_col] = ' ';
    framebuffer_console_render_cell(console_cursor_col, console_cursor_row);
    framebuffer_console_render_cursor();
}

void framebuffer_demo_console(void) {
    framebuffer_console_reset();

    framebuffer_console_write("Kernel1\n");
    framebuffer_console_write("ASM bootloader + C kernel running in graphics-ready mode.\n");
    framebuffer_console_write("Architecture ready. IRQ0 timer and IRQ1 keyboard enabled.\n");
    framebuffer_console_write("\n");
    framebuffer_console_write("kernel1> gfx console\n");
    framebuffer_console_write("Framebuffer text renderer active.\n");
    framebuffer_console_write("Next: route terminal_putchar through this backend.\n");
    framebuffer_console_write("kernel1> ");
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
    if (framebuffer.vbe_candidate_found && !framebuffer.hardware_backed) {
        terminal_write("\n  vbe candidate=");
        terminal_write_dec(framebuffer.candidate_width);
        terminal_putchar('x');
        terminal_write_dec(framebuffer.candidate_height);
        terminal_putchar('x');
        terminal_write_dec(framebuffer.candidate_bits_per_pixel);
        terminal_write(" pitch=");
        terminal_write_dec(framebuffer.candidate_pitch);
        terminal_write(" lfb=");
        terminal_write_hex(framebuffer.candidate_address);
        terminal_write("\n  note: VBE was detected but text shell is still in VGA mode");
    }
    terminal_write("\n");
}

void framebuffer_print_preview(void) {
    static const char ramp[] = " .:-=+*#%@";
    int mirror_was_enabled = terminal_graphics_mirror_enabled();

    if (!framebuffer_available()) {
        terminal_write("Framebuffer unavailable\n");
        return;
    }

    if (mirror_was_enabled) {
        terminal_set_graphics_mirror(0);
    }

    terminal_write("Framebuffer preview:\n");
    for (uint32_t y = 0; y < 25; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            uint32_t start_x = (x * framebuffer.width) / 64;
            uint32_t end_x = ((x + 1) * framebuffer.width) / 64;
            uint32_t start_y = (y * framebuffer.height) / 25;
            uint32_t end_y = ((y + 1) * framebuffer.height) / 25;
            uint32_t lit_pixels = 0;
            uint32_t samples = 0;
            uint32_t luma_total = 0;

            if (end_x <= start_x) {
                end_x = start_x + 1;
            }
            if (end_y <= start_y) {
                end_y = start_y + 1;
            }

            for (uint32_t py = start_y; py < end_y; py++) {
                for (uint32_t px = start_x; px < end_x; px++) {
                    uint32_t color = framebuffer_get_pixel(px, py);
                    uint32_t red = (color >> 16) & 0xFF;
                    uint32_t green = (color >> 8) & 0xFF;
                    uint32_t blue = color & 0xFF;
                    uint32_t luma = (red * 30 + green * 59 + blue * 11) / 100;

                    if (luma > 30) {
                        lit_pixels++;
                    }
                    luma_total += luma;
                    samples++;
                }
            }

            uint32_t density = samples == 0 ? 0 : (lit_pixels * 255) / samples;
            uint32_t average = samples == 0 ? 0 : luma_total / samples;
            uint32_t value = density > average ? density : average;
            terminal_putchar(ramp[(value * 9) / 255]);
        }
        terminal_putchar('\n');
    }

    if (mirror_was_enabled) {
        terminal_set_graphics_mirror(1);
    }
}
