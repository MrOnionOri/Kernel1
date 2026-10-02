#include "ui.h"
#include "framebuffer.h"
#include "ui_font.h"

static int pointer_x, pointer_y, pointer_enabled;
static const char* tooltip;
static struct ui_rect tooltip_source;

void ui_set_pointer(int x, int y, int enabled) {
    pointer_x = x;
    pointer_y = y;
    pointer_enabled = enabled;
    tooltip = 0;
}

int ui_is_hovered(struct ui_rect rect) {
    return pointer_enabled && ui_point_in_rect(pointer_x, pointer_y, rect);
}

int ui_text_width(const char* text) {
    int width = 0;
    for (; *text; text++) {
        unsigned ch = (unsigned char)*text;
        width += ui_font_advance[(ch >= 32 && ch <= 126 ? ch : '?') - 32];
    }
    return width;
}

void ui_write_text(int x, int y, const char* text, uint32_t color) {
    ui_write_text_clipped(x, y, ui_text_width(text), text, color);
}

void ui_draw_tooltip(struct ui_rect bounds) {
    if (!tooltip) return;
    int width = ui_text_width(tooltip) + 16;
    int x = tooltip_source.x;
    int y = tooltip_source.y + tooltip_source.height + 3;
    if (x + width > bounds.x + bounds.width - 4) x = bounds.x + bounds.width - width - 4;
    if (y + 24 > bounds.y + bounds.height) y = tooltip_source.y - 26;
    framebuffer_fill_rect(x, y, width, 24, 0x00272E38);
    ui_write_text(x + 8, y + 3, tooltip, 0x00FFFFFF);
}

int ui_point_in_rect(int x, int y, struct ui_rect rect) {
    return x >= rect.x && y >= rect.y &&
        x < rect.x + rect.width && y < rect.y + rect.height;
}

void ui_draw_button(const struct ui_button* button) {
    uint32_t background = button->disabled ? UI_SURFACE : button->background;
    uint32_t border = button->active ? UI_ACCENT : UI_BORDER;
    uint32_t foreground = button->disabled ? UI_MUTED : button->foreground;
    if (!button->disabled && ui_is_hovered(button->rect)) background = UI_HOVER;

    framebuffer_fill_rect((uint32_t)button->rect.x, (uint32_t)button->rect.y,
        (uint32_t)button->rect.width, (uint32_t)button->rect.height, background);
    framebuffer_draw_rect((uint32_t)button->rect.x, (uint32_t)button->rect.y,
        (uint32_t)button->rect.width, (uint32_t)button->rect.height, border);
    if (button->active) {
        framebuffer_fill_rect((uint32_t)button->rect.x + 2, (uint32_t)button->rect.y + 2,
            3, (uint32_t)button->rect.height - 4, UI_ACCENT);
    }
    ui_write_text_clipped(button->rect.x + 7, button->rect.y + (button->rect.height - 18) / 2,
        button->rect.width - 14, button->label, foreground);
}

void ui_write_text_clipped(int x, int y, int width, const char* text, uint32_t color) {
    int offset = 0;
    for (; *text; text++) {
        unsigned ch = (unsigned char)*text;
        unsigned index = (ch >= 32 && ch <= 126 ? ch : '?') - 32;
        int advance = ui_font_advance[index];
        if (offset + advance > width) break;
        for (int row = 0; row < 18; row++) {
            for (int col = 0; col < 16 && offset + col < width; col++) {
                unsigned pixel = row * 16 + col;
                unsigned coverage = ui_font_pixels[index][pixel / 2];
                coverage = (pixel & 1) ? coverage & 15 : coverage >> 4;
                if (coverage) framebuffer_blend_pixel(x + offset + col, y + row, color, coverage * 17);
            }
        }
        offset += advance;
    }
}

void ui_draw_icon(int x, int y, enum ui_icon icon, uint32_t color) {
    static const uint8_t pixels[][8] = {
        {0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81},
        {0,0,0,0,0,0,0xFF,0xFF},
        {0xFF,0xFF,0x81,0x81,0x81,0x81,0x81,0xFF},
        {0x3F,0x21,0xFD,0x85,0xBD,0xA0,0xA0,0xE0},
        {0x70,0x88,0xFF,0x81,0x81,0x81,0x81,0xFF},
        {0xFF,0x81,0xA9,0xA9,0xB9,0x81,0xFF,0x18},
        {0xE7,0xA5,0xE7,0,0,0xE7,0xA5,0xE7},
        {0x80,0xC0,0xE0,0xF0,0xF0,0xE0,0xC0,0x80},
        {0x3C,0x42,0x89,0x89,0x8D,0x81,0x42,0x3C},
        {0xFF,0x81,0xA1,0x91,0xA1,0x8D,0x81,0xFF},
        {0x18,0x7E,0x66,0xC3,0xC3,0x66,0x7E,0x18},
        {0x18,0x3C,0x7E,0xFF,0x42,0x5A,0x5A,0x7E},
        {0x18,0x3C,0x7E,0xDB,0x18,0x18,0x18,0x18},
        {0x10,0x30,0x70,0xFF,0xFF,0x70,0x30,0x10},
        {0x08,0x0C,0x0E,0xFF,0xFF,0x0E,0x0C,0x08},
        {0x7C,0x46,0x42,0x5A,0x42,0x5A,0x42,0x7E}
    };
    if ((unsigned)icon >= sizeof(pixels) / sizeof(pixels[0])) {
        return;
    }
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (pixels[icon][row] & (0x80 >> col)) {
                framebuffer_fill_rect(x + col * 2, y + row * 2, 2, 2, color);
            }
        }
    }
}

void ui_draw_app_icon(int x, int y, enum ui_icon icon) {
    uint32_t color = UI_ACCENT;
    if (icon == UI_ICON_FOLDER) color = 0x00B87917;
    else if (icon == UI_ICON_PLAY) color = 0x00168265;
    else if (icon == UI_ICON_APPS) color = 0x00C05D63;
    else if (icon == UI_ICON_TERMINAL || icon == UI_ICON_SETTINGS) color = UI_MUTED;
    framebuffer_fill_rect(x, y, 28, 28, 0x00FFFFFF);
    ui_draw_icon(x + 6, y + 6, icon, color);
}

void ui_draw_icon_button(struct ui_rect rect, enum ui_icon icon, int active, int disabled) {
    framebuffer_fill_rect(rect.x, rect.y, rect.width, rect.height,
        active ? UI_SELECTION : (!disabled && ui_is_hovered(rect) ? UI_HOVER : UI_SURFACE));
    framebuffer_draw_rect(rect.x, rect.y, rect.width, rect.height,
        active ? UI_ACCENT : UI_BORDER);
    ui_draw_icon(rect.x + (rect.width - 16) / 2, rect.y + (rect.height - 16) / 2,
        icon, disabled ? 0x00ACB3BC : UI_TEXT);
    static const char* names[] = {"Close", "Minimize", "Maximize", "Restore", "Files",
        "Tasks", "Applications", "Demo", "Clock", "Terminal", "Settings", "Home",
        "Parent folder", "Previous page", "Next page", "File"};
    if (ui_is_hovered(rect) && (unsigned)icon < sizeof(names) / sizeof(names[0])) {
        tooltip = names[icon];
        tooltip_source = rect;
    }
}

void ui_draw_checkbox(struct ui_rect rect, const char* label, int checked) {
    framebuffer_fill_rect(rect.x, rect.y, 18, 18, checked ? UI_ACCENT : 0x00FFFFFF);
    framebuffer_draw_rect(rect.x, rect.y, 18, 18, 0x00889098);
    if (checked) {
        framebuffer_draw_line(rect.x + 4, rect.y + 9, rect.x + 7, rect.y + 12, 0x00FFFFFF);
        framebuffer_draw_line(rect.x + 7, rect.y + 12, rect.x + 14, rect.y + 5, 0x00FFFFFF);
    }
    ui_write_text_clipped(rect.x + 28, rect.y, rect.width - 28, label, UI_TEXT);
}

void ui_draw_panel(struct ui_rect rect, const char* title, uint32_t title_color,
        uint32_t body_color, uint32_t border_color) {
    framebuffer_fill_rect((uint32_t)rect.x + 4, (uint32_t)rect.y + 4,
        (uint32_t)rect.width, (uint32_t)rect.height, 0x00000000);
    framebuffer_fill_rect((uint32_t)rect.x, (uint32_t)rect.y,
        (uint32_t)rect.width, (uint32_t)rect.height, body_color);
    framebuffer_fill_rect((uint32_t)rect.x, (uint32_t)rect.y,
        (uint32_t)rect.width, 15, title_color);
    framebuffer_draw_rect((uint32_t)rect.x, (uint32_t)rect.y,
        (uint32_t)rect.width, (uint32_t)rect.height, border_color);
    framebuffer_write_text((uint32_t)rect.x + 8, (uint32_t)rect.y + 5, title, 0x00000000);
}

void ui_draw_list_item(struct ui_rect rect, const char* label, uint32_t accent,
        int selected) {
    uint32_t background = selected ? 0x002A3440 : 0x0018232F;

    framebuffer_fill_rect((uint32_t)rect.x, (uint32_t)rect.y,
        (uint32_t)rect.width, (uint32_t)rect.height, background);
    framebuffer_fill_rect((uint32_t)rect.x + 2, (uint32_t)rect.y + 4,
        4, 4, accent);
    framebuffer_write_text((uint32_t)rect.x + 12, (uint32_t)rect.y + 5,
        label, 0x00E8EAED);
}
