#include "framebuffer.h"

#include "arch.h"
#include "pmm.h"
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
#define FB_MOUSE_CURSOR_WIDTH 12
#define FB_MOUSE_CURSOR_HEIGHT 18
#define FB_MOUSE_CURSOR_SAVE_WIDTH 13
#define FB_MOUSE_CURSOR_SAVE_HEIGHT 19

static uint32_t framebuffer_stub[FB_STUB_WIDTH * FB_STUB_HEIGHT];
static struct framebuffer_info framebuffer;
/* Supervisor-only virtual range, separate from the heap and application mappings. */
#define FB_BACKBUFFER_BASE 0x01400000
#define FB_BACKBUFFER_LIMIT 0x00400000
#define FB_MAX_ROWS 2048
static uint32_t framebuffer_backbuffer;
static uint32_t framebuffer_present_count;
static uint16_t dirty_left[FB_MAX_ROWS];
static uint16_t dirty_right[FB_MAX_ROWS];
static int mouse_cursor_saved;
static uint32_t mouse_cursor_x;
static uint32_t mouse_cursor_y;
static uint8_t mouse_cursor_buttons;
static uint32_t mouse_cursor_pixels[FB_MOUSE_CURSOR_SAVE_HEIGHT][FB_MOUSE_CURSOR_SAVE_WIDTH];
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
static int console_deferred;
static volatile int console_dirty;

struct framebuffer_boot_info {
    uint32_t magic;
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
};

static void framebuffer_set_stub(void);
static void framebuffer_console_paint(void);

static uint32_t* framebuffer_pixel_address(uint32_t x, uint32_t y) {
    uint32_t base = framebuffer_backbuffer ? framebuffer_backbuffer : framebuffer.address;
    return (uint32_t*)(base + (y * framebuffer.pitch) + (x * 4));
}

static void framebuffer_initialize_backbuffer(uint32_t bytes) {
    uint32_t offset = 0;
    if (bytes > FB_BACKBUFFER_LIMIT || framebuffer.height > FB_MAX_ROWS ||
            framebuffer.width > 65535) {
        return;
    }
    for (; offset < bytes; offset += PMM_PAGE_SIZE) {
        uint32_t page = pmm_alloc_page();
        if (!page) {
            break;
        }
        if (!arch_map_page(FB_BACKBUFFER_BASE + offset, page, ARCH_PAGE_WRITABLE)) {
            pmm_free_page(page);
            break;
        }
    }
    if (offset < bytes) {
        while (offset) {
            offset -= PMM_PAGE_SIZE;
            uint32_t address = FB_BACKBUFFER_BASE + offset;
            uint32_t page = arch_get_physical(address);
            arch_unmap_page(address);
            pmm_free_page(page);
        }
        return;
    }
    framebuffer_backbuffer = FB_BACKBUFFER_BASE;
    for (uint32_t y = 0; y < framebuffer.height; y++) {
        dirty_left[y] = (uint16_t)framebuffer.width;
        dirty_right[y] = 0;
    }
}

int framebuffer_is_buffered(void) {
    return framebuffer_backbuffer != 0;
}

void framebuffer_present(void) {
    /* IRQ keyboard output only updates cells; all framebuffer writes occur here or in the main loop. */
    if (console_dirty && !console_deferred) {
        int cursor_visible = mouse_cursor_saved;
        framebuffer_erase_mouse_cursor();
        framebuffer_console_paint();
        if (cursor_visible) framebuffer_draw_mouse_cursor(mouse_cursor_x, mouse_cursor_y, mouse_cursor_buttons);
    }
    if (!framebuffer_backbuffer) {
        return;
    }
    int changed = 0;
    for (uint32_t y = 0; y < framebuffer.height; y++) {
        uint32_t left = dirty_left[y];
        uint32_t right = dirty_right[y];
        if (left >= right) {
            continue;
        }
        const uint32_t* source = framebuffer_pixel_address(left, y);
        volatile uint32_t* target = (volatile uint32_t*)(framebuffer.address + y * framebuffer.pitch);
        for (uint32_t x = left; x < right; x++) {
            target[x] = *source++;
        }
        dirty_left[y] = (uint16_t)framebuffer.width;
        dirty_right[y] = 0;
        changed = 1;
    }
    framebuffer_present_count += changed;
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
            framebuffer_initialize_backbuffer(bytes);
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

    framebuffer_reset_mouse_cursor();
    framebuffer_fill_rect(0, 0, framebuffer.width, framebuffer.height, color);
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer_available() || x >= framebuffer.width || y >= framebuffer.height) {
        return;
    }

    *framebuffer_pixel_address(x, y) = color;
    if (framebuffer_backbuffer) {
        if (x < dirty_left[y]) dirty_left[y] = (uint16_t)x;
        if (x + 1 > dirty_right[y]) dirty_right[y] = (uint16_t)(x + 1);
    }
}

void framebuffer_blend_pixel(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha) {
    if (!alpha) return;
    uint32_t background = framebuffer_get_pixel(x, y);
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        uint32_t channel = (((color >> shift) & 255) * alpha +
            ((background >> shift) & 255) * (255 - alpha) + 127) / 255;
        result |= channel << shift;
    }
    framebuffer_put_pixel(x, y, result);
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
    if (!framebuffer_available() || x >= framebuffer.width || y >= framebuffer.height) {
        return;
    }
    if (width > framebuffer.width - x) width = framebuffer.width - x;
    if (height > framebuffer.height - y) height = framebuffer.height - y;
    if (!width || !height) return;
    for (uint32_t row = 0; row < height; row++) {
        uint32_t py = y + row;
        uint32_t* pixels = framebuffer_pixel_address(x, py);
        for (uint32_t col = 0; col < width; col++) {
            pixels[col] = color;
        }
        if (framebuffer_backbuffer) {
            if (x < dirty_left[py]) dirty_left[py] = (uint16_t)x;
            if (x + width > dirty_right[py]) dirty_right[py] = (uint16_t)(x + width);
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

void framebuffer_reset_mouse_cursor(void) {
    mouse_cursor_saved = 0;
}

static void framebuffer_restore_mouse_cursor(void) {
    if (!mouse_cursor_saved) {
        return;
    }

    for (uint32_t row = 0; row < FB_MOUSE_CURSOR_SAVE_HEIGHT; row++) {
        for (uint32_t col = 0; col < FB_MOUSE_CURSOR_SAVE_WIDTH; col++) {
            uint32_t x = mouse_cursor_x + col;
            uint32_t y = mouse_cursor_y + row;

            if (x < framebuffer.width && y < framebuffer.height) {
                framebuffer_put_pixel(x, y, mouse_cursor_pixels[row][col]);
            }
        }
    }

    mouse_cursor_saved = 0;
}

void framebuffer_erase_mouse_cursor(void) {
    framebuffer_restore_mouse_cursor();
}

static void framebuffer_save_mouse_cursor(uint32_t x, uint32_t y) {
    mouse_cursor_x = x;
    mouse_cursor_y = y;

    for (uint32_t row = 0; row < FB_MOUSE_CURSOR_SAVE_HEIGHT; row++) {
        for (uint32_t col = 0; col < FB_MOUSE_CURSOR_SAVE_WIDTH; col++) {
            uint32_t px = x + col;
            uint32_t py = y + row;

            mouse_cursor_pixels[row][col] =
                (px < framebuffer.width && py < framebuffer.height) ? framebuffer_get_pixel(px, py) : 0;
        }
    }

    mouse_cursor_saved = 1;
}

static int framebuffer_mouse_cursor_pixel(uint32_t col, uint32_t row) {
    static const uint16_t arrow[FB_MOUSE_CURSOR_HEIGHT] = {
        0x800,0xC00,0xE00,0xF00,0xF80,0xFC0,0xFE0,0xFF0,0xFF8,
        0xFFC,0xFFE,0xFC0,0xDC0,0x8E0,0x060,0x070,0x030,0x010
    };
    return (arrow[row] & (0x800 >> col)) != 0;
}

static void framebuffer_draw_mouse_cursor_pixels(uint32_t x, uint32_t y, uint32_t color,
        uint32_t offset_x, uint32_t offset_y) {
    for (uint32_t row = 0; row < FB_MOUSE_CURSOR_HEIGHT; row++) {
        for (uint32_t col = 0; col < FB_MOUSE_CURSOR_WIDTH; col++) {
            if (framebuffer_mouse_cursor_pixel(col, row)) {
                framebuffer_put_pixel(x + col + offset_x, y + row + offset_y, color);
            }
        }
    }
}

void framebuffer_draw_mouse_cursor(uint32_t x, uint32_t y, uint8_t buttons) {
    uint32_t color = buttons ? 0x00FFD166 : 0x00FFFFFF;

    if (!framebuffer_available()) {
        return;
    }

    framebuffer_restore_mouse_cursor();

    framebuffer_save_mouse_cursor(x, y);
    mouse_cursor_buttons = buttons;
    framebuffer_draw_mouse_cursor_pixels(x, y, 0x00000000, 1, 1);
    framebuffer_draw_mouse_cursor_pixels(x, y, color, 0, 0);
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
    framebuffer_draw_window(172, 48, 116, 104, 0x0048D597, 0x00343F38);
    framebuffer_write_text(8, 6, "KERNEL1", 0x00E8EAED);
    framebuffer_write_text(188, 65, "FILES", 0x00FFFFFF);
    framebuffer_fill_rect(188, 74, 76, 10, 0x00FFD166);
    framebuffer_fill_rect(188, 94, 56, 10, 0x00FF6B6B);
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

void framebuffer_draw_storage_panel(int formatted, uint32_t used_sectors,
        uint32_t free_sectors, uint32_t used_data_sectors, uint32_t free_data_sectors,
        uint32_t percent_used) {
    uint32_t panel_x = framebuffer.hardware_backed ? 680 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 164 : 270;
    uint32_t panel_width = framebuffer.hardware_backed ? 230 : 250;
    uint32_t panel_height = 112;
    uint32_t total_sectors = used_sectors + free_sectors;
    uint32_t bar_width = panel_width - 28;
    uint32_t used_width = 0;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_x + panel_width + 8 > framebuffer.width) {
        panel_x = framebuffer.width > panel_width + 12 ? framebuffer.width - panel_width - 12 : 4;
    }

    if (panel_y + panel_height + 28 > framebuffer.height) {
        panel_y = framebuffer.height > panel_height + 150 ? 150 : 24;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x001C2430);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x00FFD166);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "DISK / KFS", 0x00000000);

    if (!formatted) {
        framebuffer_write_text(panel_x + 12, panel_y + 34, "NOT FORMATTED", 0x00FF6B6B);
        framebuffer_write_text(panel_x + 12, panel_y + 50, "RUN KFSFORMAT", 0x00E8EAED);
        return;
    }

    framebuffer_write_label_uint(panel_x + 12, panel_y + 26, "USED ", used_sectors, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 38, "FREE ", free_sectors, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 50, "DATA USED ", used_data_sectors, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 62, "DATA FREE ", free_data_sectors, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 74, "USE PCT ", percent_used, 0x00E8EAED);

    framebuffer_draw_rect(panel_x + 12, panel_y + 91, bar_width, 10, 0x009EA7B3);
    if (total_sectors != 0) {
        used_width = (used_sectors * (bar_width - 2)) / total_sectors;
    }
    framebuffer_fill_rect(panel_x + 13, panel_y + 92, used_width, 8, 0x00FFD166);
    if (used_width + 2 < bar_width) {
        framebuffer_fill_rect(panel_x + 13 + used_width, panel_y + 92,
            bar_width - used_width - 2, 8, 0x0048D597);
    }
}

void framebuffer_draw_apps_panel(uint32_t built_in_count, uint32_t kapp_count,
        const char* first_name, const char* second_name, const char* third_name) {
    uint32_t panel_x = framebuffer.hardware_backed ? 360 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 164 : 394;
    uint32_t panel_width = framebuffer.hardware_backed ? 290 : 250;
    uint32_t panel_height = 112;
    uint32_t total = built_in_count + kapp_count;
    uint32_t bar_width = panel_width - 28;
    uint32_t kapp_width = 0;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_x + panel_width + 8 > framebuffer.width) {
        panel_x = framebuffer.width > panel_width + 12 ? framebuffer.width - panel_width - 12 : 4;
    }

    if (panel_y + panel_height + 28 > framebuffer.height) {
        panel_y = framebuffer.height > panel_height + 150 ? 150 : 24;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x001C2430);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x00FF6B6B);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "APPS", 0x00FFFFFF);

    framebuffer_write_label_uint(panel_x + 12, panel_y + 26, "BUILT-IN ", built_in_count, 0x00E8EAED);
    framebuffer_write_label_uint(panel_x + 12, panel_y + 38, "KAPP ", kapp_count, 0x00E8EAED);
    if (first_name != 0 && first_name[0] != '\0') {
        framebuffer_write_text(panel_x + 12, panel_y + 52, first_name, 0x0048D597);
    }
    if (second_name != 0 && second_name[0] != '\0') {
        framebuffer_write_text(panel_x + 12, panel_y + 64, second_name, 0x0048D597);
    }
    if (third_name != 0 && third_name[0] != '\0') {
        framebuffer_write_text(panel_x + 12, panel_y + 76, third_name, 0x0048D597);
    }

    framebuffer_draw_rect(panel_x + 12, panel_y + 91, bar_width, 10, 0x009EA7B3);
    if (total != 0) {
        kapp_width = (kapp_count * (bar_width - 2)) / total;
    }
    framebuffer_fill_rect(panel_x + 13, panel_y + 92, kapp_width, 8, 0x00FFD166);
    if (kapp_width + 2 < bar_width) {
        framebuffer_fill_rect(panel_x + 13 + kapp_width, panel_y + 92,
            bar_width - kapp_width - 2, 8, 0x00FF6B6B);
    }
}

static void framebuffer_write_file_entry(uint32_t x, uint32_t y, const char* name,
        uint32_t type) {
    if (name == 0 || name[0] == '\0') {
        return;
    }

    framebuffer_write_text(x, y, type == 2 ? "D " : "F ", type == 2 ? 0x00FFD166 : 0x0048D597);
    framebuffer_write_text(x + 12, y, name, 0x00E8EAED);
}

void framebuffer_draw_files_panel(int valid, const char* path, uint32_t children,
        const char* first_name, uint32_t first_type,
        const char* second_name, uint32_t second_type,
        const char* third_name, uint32_t third_type) {
    uint32_t panel_x = framebuffer.hardware_backed ? 172 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 48 : 518;
    uint32_t panel_width = framebuffer.hardware_backed ? 140 : 250;
    uint32_t panel_height = 118;
    uint32_t bar_width = panel_width - 20;
    uint32_t fill_width = children > 10 ? bar_width - 2 : (children * (bar_width - 2)) / 10;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_x + panel_width + 8 > framebuffer.width) {
        panel_x = 4;
    }

    if (panel_y + panel_height + 28 > framebuffer.height) {
        panel_y = framebuffer.height > panel_height + 28 ? framebuffer.height - panel_height - 28 : 4;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x00343F38);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x0048D597);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "FILES", 0x00FFFFFF);

    if (!valid) {
        framebuffer_write_text(panel_x + 10, panel_y + 30, "NOT FOUND", 0x00FF6B6B);
        if (path != 0 && path[0] != '\0') {
            framebuffer_write_text(panel_x + 10, panel_y + 44, path, 0x00E8EAED);
        }
        return;
    }

    framebuffer_write_text(panel_x + 10, panel_y + 25, path, 0x00FFD166);
    framebuffer_write_label_uint(panel_x + 10, panel_y + 39, "ITEMS ", children, 0x00E8EAED);
    framebuffer_write_file_entry(panel_x + 10, panel_y + 54, first_name, first_type);
    framebuffer_write_file_entry(panel_x + 10, panel_y + 68, second_name, second_type);
    framebuffer_write_file_entry(panel_x + 10, panel_y + 82, third_name, third_type);

    framebuffer_draw_rect(panel_x + 10, panel_y + 101, bar_width, 8, 0x009EA7B3);
    framebuffer_fill_rect(panel_x + 11, panel_y + 102, fill_width, 6, 0x00FFD166);
}

static void framebuffer_draw_launcher_button(uint32_t x, uint32_t y, uint32_t width,
        const char* label, uint32_t color) {
    framebuffer_fill_rect(x, y, width, 15, color);
    framebuffer_draw_rect(x, y, width, 15, 0x00DCE7F3);
    framebuffer_write_text(x + 6, y + 5, label, 0x00000000);
}

void framebuffer_draw_launcher_panel(void) {
    uint32_t panel_x = framebuffer.hardware_backed ? 46 : 24;
    uint32_t panel_y = framebuffer.hardware_backed ? 194 : 642;
    uint32_t panel_width = framebuffer.hardware_backed ? 270 : 250;
    uint32_t panel_height = 108;

    if (!framebuffer_available()) {
        return;
    }

    if (panel_y + panel_height + 28 > framebuffer.height) {
        panel_y = framebuffer.height > panel_height + 28 ? framebuffer.height - panel_height - 28 : 4;
    }

    framebuffer_fill_rect(panel_x + 4, panel_y + 4, panel_width, panel_height, 0x00000000);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, panel_height, 0x001C2430);
    framebuffer_fill_rect(panel_x, panel_y, panel_width, 15, 0x00FFD166);
    framebuffer_draw_rect(panel_x, panel_y, panel_width, panel_height, 0x00DCE7F3);
    framebuffer_write_text(panel_x + 8, panel_y + 5, "LAUNCHER", 0x00000000);

    framebuffer_draw_launcher_button(panel_x + 12, panel_y + 28, 112, "RUN DEMO", 0x0048D597);
    framebuffer_draw_launcher_button(panel_x + 138, panel_y + 28, 112, "RUN CLOCK", 0x0045A3FF);
    framebuffer_draw_launcher_button(panel_x + 12, panel_y + 50, 112, "FILES /DISK", 0x00FFD166);
    framebuffer_draw_launcher_button(panel_x + 138, panel_y + 50, 112, "APPS", 0x00FF6B6B);
    framebuffer_draw_launcher_button(panel_x + 12, panel_y + 72, 112, "GFX AUTO", 0x009EA7B3);
    framebuffer_draw_launcher_button(panel_x + 138, panel_y + 72, 112, "STATUS", 0x009EA7B3);
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
    if (console_overlay_mode) {
        framebuffer_draw_rect(console_panel_x, console_panel_y, console_panel_width, console_panel_height, 0x003A4A5C);
    } else {
        framebuffer_fill_rect(console_panel_x, console_panel_y, console_panel_width, 13, 0x0018232F);
        framebuffer_write_text(console_panel_x + 6, console_panel_y + 4,
            "KERNEL1 GRAPHICAL CONSOLE", 0x00E8EAED);
        framebuffer_draw_rect(console_panel_x, console_panel_y, console_panel_width, console_panel_height, 0x003A4A5C);
    }
}

static void framebuffer_console_paint_cell(uint32_t col, uint32_t row) {
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

static void framebuffer_console_render_cell(uint32_t col, uint32_t row) {
    (void)col;
    (void)row;
    console_dirty = 1;
}

static void framebuffer_console_paint_cursor(void) {
    uint32_t x = console_origin_x + console_cursor_col * FB_CONSOLE_CELL_WIDTH;
    uint32_t y = console_origin_y + console_cursor_row * FB_CONSOLE_CELL_HEIGHT + 8;
    framebuffer_fill_rect(x, y, 5, 1, 0x0048D597);
}

static void framebuffer_console_render_cursor(void) {
    console_dirty = 1;
}

static void framebuffer_console_erase_cursor(void) {
    framebuffer_console_render_cell(console_cursor_col, console_cursor_row);
}

static void framebuffer_console_paint(void) {
    console_dirty = 0;
    framebuffer_console_draw_frame();
    for (uint32_t row = 0; row < console_rows; row++) {
        for (uint32_t col = 0; col < console_cols; col++) {
            framebuffer_console_paint_cell(col, row);
        }
    }
    framebuffer_console_paint_cursor();
}

static void framebuffer_console_redraw(void) {
    console_dirty = 1;
}

void framebuffer_console_set_deferred(int deferred) {
    console_deferred = deferred;
}

int framebuffer_console_needs_redraw(void) {
    return console_dirty;
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

void framebuffer_console_set_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (width < 48 || height < 24) {
        return;
    }

    console_panel_x = x;
    console_panel_y = y;
    console_panel_width = width;
    console_panel_height = height;
    console_origin_x = x + 6;
    console_origin_y = y + 6;
    console_cols = (width - 12) / FB_CONSOLE_CELL_WIDTH;
    console_rows = (height - 12) / FB_CONSOLE_CELL_HEIGHT;
    if (console_cols > FB_CONSOLE_MAX_COLS) {
        console_cols = FB_CONSOLE_MAX_COLS;
    }
    if (console_rows > FB_CONSOLE_MAX_ROWS) {
        console_rows = FB_CONSOLE_MAX_ROWS;
    }
    if (console_cols == 0) {
        console_cols = 1;
    }
    if (console_rows == 0) {
        console_rows = 1;
    }
    if (console_cursor_col >= console_cols) {
        console_cursor_col = console_cols - 1;
    }
    if (console_cursor_row >= console_rows) {
        console_cursor_row = console_rows - 1;
    }

    console_overlay_mode = 1;
    console_active = 1;
    /* Desktop composition owns painting; IRQ output only updates console cells. */
    framebuffer_console_paint();
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
    terminal_write("\n  double buffer=");
    terminal_write(framebuffer_is_buffered() ? "on" : "off (direct fallback)");
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
