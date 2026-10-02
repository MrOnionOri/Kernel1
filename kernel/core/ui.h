#ifndef KERNEL_UI_H
#define KERNEL_UI_H

#include <stdint.h>

#define UI_TEXT 0x00272E38
#define UI_MUTED 0x00616C7A
#define UI_SURFACE 0x00F7F8FA
#define UI_BORDER 0x00C8CED6
#define UI_ACCENT 0x002266CC
#define UI_SELECTION 0x00DBE9FF
#define UI_HOVER 0x00E8EDF4

struct ui_rect {
    int x;
    int y;
    int width;
    int height;
};

struct ui_button {
    struct ui_rect rect;
    const char* label;
    uint32_t background;
    uint32_t foreground;
    int active;
    int disabled;
};

enum ui_icon {
    UI_ICON_CLOSE, UI_ICON_MINIMIZE, UI_ICON_MAXIMIZE, UI_ICON_RESTORE,
    UI_ICON_FOLDER, UI_ICON_TASKS, UI_ICON_APPS, UI_ICON_PLAY,
    UI_ICON_CLOCK, UI_ICON_TERMINAL, UI_ICON_SETTINGS, UI_ICON_HOME,
    UI_ICON_UP, UI_ICON_LEFT, UI_ICON_RIGHT, UI_ICON_FILE
};

int ui_point_in_rect(int x, int y, struct ui_rect rect);
void ui_set_pointer(int x, int y, int enabled);
int ui_is_hovered(struct ui_rect rect);
void ui_draw_tooltip(struct ui_rect bounds);
int ui_text_width(const char* text);
void ui_write_text(int x, int y, const char* text, uint32_t color);
void ui_draw_app_icon(int x, int y, enum ui_icon icon);
void ui_draw_button(const struct ui_button* button);
void ui_draw_panel(struct ui_rect rect, const char* title, uint32_t title_color,
    uint32_t body_color, uint32_t border_color);
void ui_draw_list_item(struct ui_rect rect, const char* label, uint32_t accent,
    int selected);
void ui_draw_icon(int x, int y, enum ui_icon icon, uint32_t color);
void ui_draw_icon_button(struct ui_rect rect, enum ui_icon icon, int active, int disabled);
void ui_draw_checkbox(struct ui_rect rect, const char* label, int checked);
void ui_write_text_clipped(int x, int y, int width, const char* text, uint32_t color);

#endif
