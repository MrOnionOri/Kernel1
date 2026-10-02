#include "shell_system.h"

#include "arch.h"
#include "app.h"
#include "ata.h"
#include "framebuffer.h"
#include "heap.h"
#include "kfs.h"
#include "memory_map.h"
#include "mouse.h"
#include "pmm.h"
#include "shell_fs.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "ui.h"
#include "user_mode.h"
#include "vfs.h"

#include <stdint.h>

static int gfx_auto_dashboard;
static int gfx_dashboard_compact = 1;
static int gfx_cursor_enabled;
static int gfx_cursor_initialized;
static int gfx_cursor_x;
static int gfx_cursor_y;
static int gfx_cursor_last_mouse_x;
static int gfx_cursor_last_mouse_y;
static uint32_t gfx_cursor_last_left_clicks;
static uint32_t gfx_cursor_last_right_clicks;
static uint32_t gfx_cursor_last_middle_clicks;
static uint32_t gfx_cursor_last_draw_packets;
static uint32_t gfx_cursor_last_input_packets;
static uint8_t gfx_cursor_last_draw_buttons;
static uint32_t gfx_cursor_last_motion_tick;
static int gfx_windows_enabled;
static int gfx_windows_dragging;
static int gfx_windows_active = -1;
static int gfx_windows_drag_offset_x;
static int gfx_windows_drag_offset_y;
static uint8_t gfx_windows_last_buttons;
static int gfx_desktop_clean;
static uint32_t gfx_windows_last_fast_refresh_tick;
static uint32_t gfx_windows_last_slow_refresh_tick;
static int gfx_clock_app_started;
static int gfx_demo_app_started;
static int gfx_files_selected_index = -1;
static int gfx_apps_selected_index = -1;
static char gfx_files_selected_name[VFS_NAME_SIZE];
static uint32_t gfx_files_selected_type;
static uint32_t gfx_files_selected_size;
static char gfx_viewer_path[SHELL_FS_PATH_SIZE];
static char gfx_viewer_text[256];
static uint32_t gfx_viewer_size;
static int gfx_viewer_loaded;
static uint32_t gfx_files_page;
static int gfx_live_widgets = 1;
static int gfx_desktop_color_index;
static const uint32_t gfx_desktop_colors[] = {0x00436A62, 0x00515C66, 0x005C6354};
static int gfx_hover_id = -1;

enum shell_gfx_target {
    SHELL_GFX_TARGET_DESKTOP = 0,
    SHELL_GFX_TARGET_STATUS,
    SHELL_GFX_TARGET_TASKS,
    SHELL_GFX_TARGET_DISK,
    SHELL_GFX_TARGET_FILES,
    SHELL_GFX_TARGET_APPS,
    SHELL_GFX_TARGET_LAUNCHER,
    SHELL_GFX_TARGET_LAUNCHER_RUN_DEMO,
    SHELL_GFX_TARGET_LAUNCHER_RUN_CLOCK,
    SHELL_GFX_TARGET_LAUNCHER_FILES_DISK,
    SHELL_GFX_TARGET_LAUNCHER_APPS,
    SHELL_GFX_TARGET_LAUNCHER_GFX_AUTO,
    SHELL_GFX_TARGET_LAUNCHER_STATUS,
    SHELL_GFX_TARGET_DESKTOP_DOCK_FILES,
    SHELL_GFX_TARGET_DESKTOP_DOCK_TASKS,
    SHELL_GFX_TARGET_DESKTOP_DOCK_APPS,
    SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_DEMO,
    SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_CLOCK,
    SHELL_GFX_TARGET_DESKTOP_DOCK_SHELL,
    SHELL_GFX_TARGET_DESKTOP_DOCK_DASHBOARD,
    SHELL_GFX_TARGET_DESKTOP_DOCK_SETTINGS,
    SHELL_GFX_TARGET_STUB,
};

#define SHELL_GFX_WINDOW_COUNT 8
#define SHELL_GFX_WINDOW_TITLE_HEIGHT 26
#define SHELL_GFX_DOCK_X 24
#define SHELL_GFX_DOCK_Y 44
#define SHELL_GFX_DOCK_WIDTH 172
#define SHELL_GFX_DOCK_HEIGHT 284

enum shell_gfx_window_type {
    SHELL_GFX_WINDOW_FILES = 0,
    SHELL_GFX_WINDOW_TASKS,
    SHELL_GFX_WINDOW_APPS,
    SHELL_GFX_WINDOW_SHELL,
    SHELL_GFX_WINDOW_CLOCK,
    SHELL_GFX_WINDOW_DEMO,
    SHELL_GFX_WINDOW_VIEWER,
    SHELL_GFX_WINDOW_SETTINGS,
};

struct shell_gfx_window {
    int x;
    int y;
    int width;
    int height;
    int z;
    int visible;
    int minimized;
    int maximized;
    int restore_x;
    int restore_y;
    int restore_width;
    int restore_height;
    enum shell_gfx_window_type type;
    uint32_t title_color;
    uint32_t body_color;
    const char* title;
};

struct shell_gfx_rect {
    int x;
    int y;
    int width;
    int height;
};

static struct shell_gfx_window gfx_windows[SHELL_GFX_WINDOW_COUNT] = {
    {96, 78, 220, 118, 0, 1, 0, 0, 96, 78, 220, 118, SHELL_GFX_WINDOW_FILES, 0x0048D597, 0x0025302B, "FILES"},
    {356, 104, 250, 132, 1, 1, 0, 0, 356, 104, 250, 132, SHELL_GFX_WINDOW_TASKS, 0x0045A3FF, 0x001A2534, "TASKS"},
    {620, 88, 220, 126, 2, 1, 0, 0, 620, 88, 220, 126, SHELL_GFX_WINDOW_APPS, 0x00FF6B6B, 0x002B2530, "APPS"},
    {228, 442, 780, 190, 3, 1, 0, 0, 228, 442, 780, 190, SHELL_GFX_WINDOW_SHELL, 0x0018232F, 0x000B1018, "SHELL"},
    {238, 198, 210, 104, 4, 0, 0, 0, 238, 198, 210, 104, SHELL_GFX_WINDOW_CLOCK, 0x0045A3FF, 0x001A2534, "CLOCK"},
    {492, 198, 250, 118, 5, 0, 0, 0, 492, 198, 250, 118, SHELL_GFX_WINDOW_DEMO, 0x00FFD166, 0x002B2A1C, "DEMO"},
    {520, 310, 360, 160, 6, 0, 0, 0, 520, 310, 360, 160, SHELL_GFX_WINDOW_VIEWER, 0x009EA7B3, 0x001A2534, "VIEWER"},
    {400, 360, 380, 220, 7, 0, 0, 0, 400, 360, 380, 220, SHELL_GFX_WINDOW_SETTINGS, 0x0048D597, 0x00262A30, "SETTINGS"}
};

static const struct shell_gfx_rect gfx_window_layout[SHELL_GFX_WINDOW_COUNT] = {
    {220, 52, 364, 292}, {610, 52, 300, 192}, {610, 52, 360, 252},
    {220, 368, 760, 300}, {610, 326, 260, 152}, {610, 502, 300, 156},
    {284, 360, 440, 230}, {400, 360, 380, 220}
};

static const struct {
    const char* title;
    enum ui_icon icon;
    enum shell_gfx_target target;
    int window;
} gfx_dock_items[] = {
    {"Files", UI_ICON_FOLDER, SHELL_GFX_TARGET_DESKTOP_DOCK_FILES, SHELL_GFX_WINDOW_FILES},
    {"Apps", UI_ICON_APPS, SHELL_GFX_TARGET_DESKTOP_DOCK_APPS, SHELL_GFX_WINDOW_APPS},
    {"Tasks", UI_ICON_TASKS, SHELL_GFX_TARGET_DESKTOP_DOCK_TASKS, SHELL_GFX_WINDOW_TASKS},
    {"Demo", UI_ICON_PLAY, SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_DEMO, SHELL_GFX_WINDOW_DEMO},
    {"Clock", UI_ICON_CLOCK, SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_CLOCK, SHELL_GFX_WINDOW_CLOCK},
    {"Settings", UI_ICON_SETTINGS, SHELL_GFX_TARGET_DESKTOP_DOCK_SETTINGS, SHELL_GFX_WINDOW_SETTINGS},
    {"Terminal", UI_ICON_TERMINAL, SHELL_GFX_TARGET_DESKTOP_DOCK_SHELL, SHELL_GFX_WINDOW_SHELL},
    {"Dashboard", UI_ICON_TASKS, SHELL_GFX_TARGET_DESKTOP_DOCK_DASHBOARD, -1}
};

static void shell_gfx_draw_dashboard(void);
static void shell_gfx_draw_window_workspace(void);
static void shell_gfx_clamp_window(struct shell_gfx_window* window);
static void shell_gfx_toggle_maximize_window(int index);
static int shell_gfx_spawn_app(const char* name);
static int shell_gfx_show_window(enum shell_gfx_window_type type);
static int shell_gfx_top_window_at(int x, int y);
static void shell_gfx_arrange_windows(void);

static struct ui_rect shell_gfx_dock_item_rect(unsigned index) {
    return (struct ui_rect){SHELL_GFX_DOCK_X + 10, SHELL_GFX_DOCK_Y + 34 + (int)index * 30,
        SHELL_GFX_DOCK_WIDTH - 20, 26};
}

static struct ui_rect shell_gfx_window_control_rect(const struct shell_gfx_window* window, int control) {
    return (struct ui_rect){window->x + window->width - 24 * (control + 1), window->y + 3, 21, 20};
}

static const unsigned gfx_app_items[] = {0, 2, 4, 3, 5, 6};

static struct ui_rect shell_gfx_app_item_rect(const struct shell_gfx_window* window, unsigned index) {
    int width = (window->width - 36) / 2;
    return (struct ui_rect){window->x + 12 + (int)(index % 2) * (width + 12),
        window->y + 40 + (int)(index / 2) * 66, width, 56};
}

static int string_equals(const char* left, const char* right) {
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static char ascii_lower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return (char)(character - 'A' + 'a');
    }

    return character;
}

static int string_equals_ci(const char* left, const char* right) {
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static uint32_t string_to_uint(const char* text, int* ok) {
    uint32_t value = 0;
    size_t index = 0;

    *ok = 0;

    if (text[0] == '\0') {
        return 0;
    }

    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }

        value = value * 10 + (uint32_t)(text[index] - '0');
        index++;
    }

    *ok = 1;
    return value;
}

static void shell_gfx_usage(void) {
    terminal_write("gfx: usage gfx [info|status|files [path]|apps|storage|launcher|desktop|dashboard [compact|full]|auto <on|off|status|compact|full> [compact|full]|windows [on|off|status|reset|arrange]|cursor [on|off|status|center]|click|scene <desktop|test|clear>|shell <on|off|status|clear|demo>|mirror <on|off|status|clear>|preview]\n");
}

static void shell_gfx_restore_console_overlay(void) {
    if (gfx_desktop_clean) {
        return;
    }

    if (terminal_graphics_mirror_enabled()) {
        framebuffer_console_reset();
    }
}

static void shell_gfx_draw_apps_panel(void) {
    struct app_summary summary;

    app_get_summary(&summary);
    framebuffer_draw_apps_panel(summary.built_in_count, summary.kapp_count,
        summary.names[0], summary.names[1], summary.names[2]);
}

static void shell_gfx_copy_limited(char* output, size_t output_size, const char* input) {
    size_t index = 0;

    if (output_size == 0) {
        return;
    }

    while (input[index] != '\0' && index + 1 < output_size) {
        output[index] = input[index];
        index++;
    }

    output[index] = '\0';
}

static int shell_gfx_draw_files_panel_for_path(const char* path) {
    struct vfs_stat_info info;
    struct vfs_dir_entry entry;
    char names[3][18];
    uint32_t types[3] = {0, 0, 0};
    uint32_t index = 0;

    names[0][0] = '\0';
    names[1][0] = '\0';
    names[2][0] = '\0';

    if (!vfs_stat_info(path, &info) || info.type != VFS_NODE_DIRECTORY) {
        framebuffer_draw_files_panel(0, path, 0, "", 0, "", 0, "", 0);
        return 0;
    }

    while (index < 3 && vfs_read_dir(path, index, &entry)) {
        shell_gfx_copy_limited(names[index], sizeof(names[index]), entry.name);
        types[index] = entry.type;
        index++;
    }

    framebuffer_draw_files_panel(1, path, info.children,
        names[0], types[0], names[1], types[1], names[2], types[2]);
    return 1;
}

static void shell_gfx_center_cursor(void) {
    const struct framebuffer_info* info = framebuffer_get_info();
    const struct mouse_state* state = mouse_get_state();

    gfx_cursor_x = (int)(info->width / 2);
    gfx_cursor_y = (int)(info->height / 2);
    gfx_cursor_last_mouse_x = state->x;
    gfx_cursor_last_mouse_y = state->y;
    gfx_cursor_initialized = 1;
}

static void shell_gfx_apply_cursor_delta(void) {
    const struct framebuffer_info* info = framebuffer_get_info();
    const struct mouse_state* state = mouse_get_state();
    int max_x;
    int max_y;

    if (!gfx_cursor_initialized) {
        shell_gfx_center_cursor();
    }

    gfx_cursor_x += state->x - gfx_cursor_last_mouse_x;
    gfx_cursor_y += state->y - gfx_cursor_last_mouse_y;
    gfx_cursor_last_mouse_x = state->x;
    gfx_cursor_last_mouse_y = state->y;

    max_x = info->width ? (int)info->width - 1 : 0;
    max_y = info->height ? (int)info->height - 1 : 0;

    if (gfx_cursor_x < 0) {
        gfx_cursor_x = 0;
    } else if (gfx_cursor_x > max_x) {
        gfx_cursor_x = max_x;
    }

    if (gfx_cursor_y < 0) {
        gfx_cursor_y = 0;
    } else if (gfx_cursor_y > max_y) {
        gfx_cursor_y = max_y;
    }
}

static void shell_gfx_draw_cursor(void) {
    const struct mouse_state* state = mouse_get_state();

    if (!state->initialized || !framebuffer_available()) {
        return;
    }

    shell_gfx_apply_cursor_delta();
    framebuffer_draw_mouse_cursor((uint32_t)gfx_cursor_x, (uint32_t)gfx_cursor_y, state->buttons);
    gfx_cursor_last_draw_packets = state->packets;
    gfx_cursor_last_draw_buttons = state->buttons;
    gfx_cursor_last_motion_tick = timer_ticks();
}

static int shell_gfx_cursor_needs_redraw(void) {
    const struct mouse_state* state = mouse_get_state();

    if (!state->initialized) {
        return 0;
    }

    return state->packets != gfx_cursor_last_input_packets ||
        state->buttons != gfx_windows_last_buttons ||
        state->left_clicks != gfx_cursor_last_left_clicks;
}

static void shell_gfx_print_cursor_status(void) {
    const struct mouse_state* state = mouse_get_state();

    terminal_write("gfx cursor: ");
    terminal_write(gfx_cursor_enabled ? "on\n" : "off\n");
    terminal_write("  x=");
    terminal_write_dec((uint32_t)(gfx_cursor_x < 0 ? 0 : gfx_cursor_x));
    terminal_write(" y=");
    terminal_write_dec((uint32_t)(gfx_cursor_y < 0 ? 0 : gfx_cursor_y));
    terminal_write(" buttons=");
    terminal_write_dec(state->buttons);
    terminal_write(" clicks L/R/M=");
    terminal_write_dec(state->left_clicks);
    terminal_write("/");
    terminal_write_dec(state->right_clicks);
    terminal_write("/");
    terminal_write_dec(state->middle_clicks);
    terminal_write("\n");
}

static int shell_gfx_point_in_rect(int x, int y, int rx, int ry, int width, int height) {
    return x >= rx && y >= ry && x < rx + width && y < ry + height;
}

static void shell_gfx_draw_desktop_dock(void) {
    ui_set_pointer(gfx_cursor_x, gfx_cursor_y, shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y) < 0);
    framebuffer_fill_rect(SHELL_GFX_DOCK_X + 4, SHELL_GFX_DOCK_Y + 4,
        SHELL_GFX_DOCK_WIDTH, SHELL_GFX_DOCK_HEIGHT, 0x00243D39);
    framebuffer_fill_rect(SHELL_GFX_DOCK_X, SHELL_GFX_DOCK_Y,
        SHELL_GFX_DOCK_WIDTH, SHELL_GFX_DOCK_HEIGHT, UI_SURFACE);
    ui_write_text(SHELL_GFX_DOCK_X + 14, SHELL_GFX_DOCK_Y + 8, "Applications", UI_MUTED);
    for (unsigned index = 0; index < sizeof(gfx_dock_items) / sizeof(gfx_dock_items[0]); index++) {
        struct ui_rect rect = shell_gfx_dock_item_rect(index);
        int window = gfx_dock_items[index].window;
        int active = window >= 0 && window == gfx_windows_active;
        framebuffer_fill_rect(rect.x, rect.y, rect.width, rect.height,
            active ? UI_SELECTION : (ui_is_hovered(rect) ? UI_HOVER : UI_SURFACE));
        ui_draw_icon(rect.x + 8, rect.y + 5, gfx_dock_items[index].icon,
            index == 0 ? 0x00B87917 : (index == 3 ? 0x00168265 : UI_ACCENT));
        ui_write_text(rect.x + 34, rect.y + 4, gfx_dock_items[index].title, UI_TEXT);
        if (window >= 0 && (gfx_windows[window].visible || gfx_windows[window].minimized)) {
            framebuffer_fill_rect(rect.x + rect.width - 9, rect.y + 11, 3, 5,
                gfx_windows[window].minimized ? 0x00B87917 : UI_ACCENT);
        }
    }
}

static void shell_gfx_draw_desktop_bars(void) {
    const struct framebuffer_info* info = framebuffer_get_info();
    framebuffer_fill_rect(0, 0, info->width, 30, UI_SURFACE);
    ui_write_text(16, 6, "Kernel1", UI_TEXT);
    ui_write_text(104, 6, "Desktop", UI_MUTED);
    ui_draw_icon(info->width - 144, 7, UI_ICON_TASKS, 0x00168265);
    ui_write_text(info->width - 120, 6, "System ready", UI_MUTED);
    framebuffer_fill_rect(0, info->height - 24, info->width, 24, UI_SURFACE);
    ui_write_text(16, info->height - 21, "Local session", UI_MUTED);
}

static void shell_gfx_draw_clean_desktop(void) {
    framebuffer_clear(gfx_desktop_colors[gfx_desktop_color_index]);
    shell_gfx_draw_desktop_bars();
    shell_gfx_draw_desktop_dock();
}

static enum shell_gfx_target shell_gfx_cursor_target(void) {
    const struct framebuffer_info* info = framebuffer_get_info();
    int x = gfx_cursor_x;
    int y = gfx_cursor_y;

    if (!info->hardware_backed) {
        return SHELL_GFX_TARGET_STUB;
    }

    if (gfx_desktop_clean) {
        for (unsigned index = 0; index < sizeof(gfx_dock_items) / sizeof(gfx_dock_items[0]); index++) {
            if (ui_point_in_rect(x, y, shell_gfx_dock_item_rect(index))) {
                return gfx_dock_items[index].target;
            }
        }
        return SHELL_GFX_TARGET_DESKTOP;
    }

    if (shell_gfx_point_in_rect(x, y, 360, 38, 290, 112)) {
        return SHELL_GFX_TARGET_STATUS;
    }

    if (shell_gfx_point_in_rect(x, y, 680, 38, 230, 112)) {
        return SHELL_GFX_TARGET_TASKS;
    }

    if (shell_gfx_point_in_rect(x, y, 680, 164, 230, 112)) {
        return SHELL_GFX_TARGET_DISK;
    }

    if (shell_gfx_point_in_rect(x, y, 172, 48, 140, 118)) {
        return SHELL_GFX_TARGET_FILES;
    }

    if (shell_gfx_point_in_rect(x, y, 360, 164, 290, 112)) {
        return SHELL_GFX_TARGET_APPS;
    }

    if (shell_gfx_point_in_rect(x, y, 46, 194, 270, 108)) {
        if (shell_gfx_point_in_rect(x, y, 58, 222, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_RUN_DEMO;
        }
        if (shell_gfx_point_in_rect(x, y, 184, 222, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_RUN_CLOCK;
        }
        if (shell_gfx_point_in_rect(x, y, 58, 244, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_FILES_DISK;
        }
        if (shell_gfx_point_in_rect(x, y, 184, 244, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_APPS;
        }
        if (shell_gfx_point_in_rect(x, y, 58, 266, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_GFX_AUTO;
        }
        if (shell_gfx_point_in_rect(x, y, 184, 266, 112, 15)) {
            return SHELL_GFX_TARGET_LAUNCHER_STATUS;
        }
        return SHELL_GFX_TARGET_LAUNCHER;
    }

    return SHELL_GFX_TARGET_DESKTOP;
}

static const char* shell_gfx_target_name(enum shell_gfx_target target) {
    switch (target) {
        case SHELL_GFX_TARGET_STATUS: return "kernel status panel";
        case SHELL_GFX_TARGET_TASKS: return "tasks panel";
        case SHELL_GFX_TARGET_DISK: return "disk/kfs panel";
        case SHELL_GFX_TARGET_FILES: return "files panel";
        case SHELL_GFX_TARGET_APPS: return "apps panel";
        case SHELL_GFX_TARGET_LAUNCHER: return "launcher panel";
        case SHELL_GFX_TARGET_LAUNCHER_RUN_DEMO: return "launcher: run demo";
        case SHELL_GFX_TARGET_LAUNCHER_RUN_CLOCK: return "launcher: run clock";
        case SHELL_GFX_TARGET_LAUNCHER_FILES_DISK: return "launcher: files /disk";
        case SHELL_GFX_TARGET_LAUNCHER_APPS: return "launcher: apps";
        case SHELL_GFX_TARGET_LAUNCHER_GFX_AUTO: return "launcher: gfx auto";
        case SHELL_GFX_TARGET_LAUNCHER_STATUS: return "launcher: status";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_FILES: return "dock: files";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_TASKS: return "dock: tasks";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_APPS: return "dock: apps";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_DEMO: return "dock: demo";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_CLOCK: return "dock: clock";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SHELL: return "dock: shell";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_DASHBOARD: return "dock: dashboard";
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SETTINGS: return "dock: settings";
        case SHELL_GFX_TARGET_STUB: return "stub framebuffer";
        case SHELL_GFX_TARGET_DESKTOP:
        default: return "desktop";
    }
}

static void shell_gfx_uint_to_text(uint32_t value, char* output, size_t output_size) {
    char reversed[12];
    size_t count = 0;
    size_t index = 0;

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

static void shell_gfx_write_label_uint(uint32_t x, uint32_t y, const char* label,
        uint32_t value, uint32_t color) {
    char number[12];
    ui_write_text(x, y, label, color);
    shell_gfx_uint_to_text(value, number, sizeof(number));
    ui_write_text(x + ui_text_width(label), y, number, color);
}

static uint32_t shell_gfx_files_rows(const struct shell_gfx_window* window) {
    int rows = (window->height - 122) / 22;
    return (uint32_t)(rows > 0 ? rows : 1);
}

static struct ui_rect shell_gfx_files_tool_rect(const struct shell_gfx_window* window, int tool) {
    return (struct ui_rect){window->x + 12 + tool * 32, window->y + 36, 26, 24};
}

static void shell_gfx_draw_window_files_content(const struct shell_gfx_window* window) {
    struct vfs_stat_info info;
    struct vfs_dir_entry entry;
    uint32_t row_y = (uint32_t)window->y + 88;
    uint32_t count = 0;
    uint32_t rows = shell_gfx_files_rows(window);
    const char* directory = shell_fs_current_directory();

    if (!vfs_stat_info(shell_fs_current_directory(), &info) || info.type != VFS_NODE_DIRECTORY) {
        ui_write_text(window->x + 12, row_y, "Folder not found", 0x00B83742);
        return;
    }

    if (gfx_files_page >= info.children) {
        gfx_files_page = 0;
    }
    ui_draw_icon_button(shell_gfx_files_tool_rect(window, 0), UI_ICON_HOME, 0, 0);
    ui_draw_icon_button(shell_gfx_files_tool_rect(window, 1), UI_ICON_UP, 0, directory[0] == '\0');
    ui_draw_icon_button(shell_gfx_files_tool_rect(window, 2), UI_ICON_LEFT, 0, gfx_files_page == 0);
    ui_draw_icon_button(shell_gfx_files_tool_rect(window, 3), UI_ICON_RIGHT, 0,
        gfx_files_page + rows >= info.children);
    ui_write_text_clipped(window->x + 12, window->y + 66, window->width - 24,
        directory[0] ? directory : "Home", UI_MUTED);
    framebuffer_fill_rect(window->x + 1, window->y + window->height - 26,
        window->width - 2, 25, 0x00EDF0F4);
    shell_gfx_write_label_uint(window->x + 12, window->y + window->height - 23,
        "Items: ", info.children, UI_MUTED);

    while (count < rows && vfs_read_dir(directory, gfx_files_page + count, &entry)) {
        uint32_t color = entry.type == VFS_NODE_DIRECTORY ? 0x00B87917 : UI_ACCENT;
        struct ui_rect rect = {window->x + 10, (int)(row_y + count * 22), window->width - 20, 21};
        framebuffer_fill_rect(rect.x, rect.y, rect.width, rect.height,
            gfx_files_selected_index == (int)(gfx_files_page + count) ? UI_SELECTION :
            (ui_is_hovered(rect) ? UI_HOVER : UI_SURFACE));
        ui_draw_icon(rect.x + 6, rect.y + 3,
            entry.type == VFS_NODE_DIRECTORY ? UI_ICON_FOLDER : UI_ICON_FILE, color);
        ui_write_text_clipped(rect.x + 32, rect.y + 2, rect.width - 40, entry.name, UI_TEXT);
        count++;
    }

    if (count == 0) {
        ui_write_text(window->x + 12, row_y + 8, "Empty folder", UI_MUTED);
    }
}

static void shell_gfx_draw_window_tasks_content(const struct shell_gfx_window* window) {
    struct task_summary summary;

    task_get_summary(&summary);
    const char* labels[] = {"Ready", "Running", "Sleeping", "Exited"};
    uint32_t values[] = {summary.ready, summary.running, summary.sleeping, summary.exited};
    for (int row = 0; row < 4; row++) {
        int y = window->y + 38 + row * 28;
        ui_write_text(window->x + 16, y, labels[row], UI_TEXT);
        shell_gfx_write_label_uint(window->x + window->width - 52, y, "", values[row], UI_ACCENT);
        framebuffer_fill_rect(window->x + 16, y + 23, window->width - 32, 1, UI_BORDER);
    }
    shell_gfx_write_label_uint(window->x + 16, window->y + 158, "Next process: ", summary.next_id, UI_MUTED);
}

static void shell_gfx_draw_window_apps_content(const struct shell_gfx_window* window) {
    for (unsigned index = 0; index < sizeof(gfx_app_items) / sizeof(gfx_app_items[0]); index++) {
        unsigned item = gfx_app_items[index];
        struct ui_rect rect = shell_gfx_app_item_rect(window, index);
        ui_draw_button(&(struct ui_button){rect, "", 0x00FFFFFF, UI_TEXT, 0, 0});
        ui_draw_app_icon(rect.x + 10, rect.y + 14, gfx_dock_items[item].icon);
        ui_write_text_clipped(rect.x + 48, rect.y + 19, rect.width - 56,
            gfx_dock_items[item].title, UI_TEXT);
    }
}

static void shell_gfx_draw_window_clock_content(const struct shell_gfx_window* window) {
    uint32_t ticks = timer_ticks();
    uint32_t seconds = ticks / 100;
    uint32_t minutes = seconds / 60;

    int cx = window->x + 50, cy = window->y + 80;
    framebuffer_draw_rect(cx - 30, cy - 30, 61, 61, UI_BORDER);
    framebuffer_fill_rect(cx - 1, cy - 26, 3, 6, UI_MUTED);
    framebuffer_fill_rect(cx - 1, cy + 21, 3, 6, UI_MUTED);
    framebuffer_fill_rect(cx - 26, cy - 1, 6, 3, UI_MUTED);
    framebuffer_fill_rect(cx + 21, cy - 1, 6, 3, UI_MUTED);
    static const int8_t hand[12][2] = {{0,-22},{11,-19},{19,-11},{22,0},{19,11},{11,19},
        {0,22},{-11,19},{-19,11},{-22,0},{-19,-11},{-11,-19}};
    unsigned minute = minutes % 12, second = (seconds % 60) / 5;
    framebuffer_draw_line(cx, cy, cx + hand[minute][0] / 2, cy + hand[minute][1] / 2, UI_TEXT);
    framebuffer_draw_line(cx, cy, cx + hand[second][0], cy + hand[second][1], UI_ACCENT);
    ui_write_text(window->x + 104, window->y + 48, "Uptime", UI_MUTED);
    char time[24];
    shell_gfx_uint_to_text(minutes, time, sizeof(time));
    size_t length = 0;
    while (time[length]) length++;
    time[length++] = ':';
    time[length++] = '0' + (seconds % 60) / 10;
    time[length++] = '0' + seconds % 10;
    time[length] = 0;
    ui_write_text(window->x + 104, window->y + 72, time, UI_TEXT);
    ui_write_text(window->x + 16, window->y + 124, "Since system start", UI_MUTED);
}

static void shell_gfx_draw_window_demo_content(const struct shell_gfx_window* window) {
    uint32_t ticks = timer_ticks();
    uint32_t phase = (ticks / 12) % 80;

    ui_draw_app_icon(window->x + 16, window->y + 42, UI_ICON_PLAY);
    ui_write_text(window->x + 56, window->y + 45, "Activity", UI_TEXT);
    framebuffer_fill_rect(window->x + 16, window->y + 92, window->width - 32, 12, UI_BORDER);
    framebuffer_fill_rect(window->x + 16, window->y + 92,
        (window->width - 32) * (phase + 1) / 80, 12, 0x00168265);
    ui_write_text(window->x + 16, window->y + 116, gfx_live_widgets ? "Running" : "Paused", UI_MUTED);
}

static void shell_gfx_draw_window_viewer_content(const struct shell_gfx_window* window) {
    uint32_t text_x = (uint32_t)window->x + 12;
    uint32_t text_y = (uint32_t)window->y + 36;
    uint32_t line = 0;
    uint32_t column = 0;
    char current[48];

    ui_write_text_clipped(text_x, text_y, window->width - 24,
        gfx_viewer_path[0] != '\0' ? gfx_viewer_path : "No file", UI_ACCENT);
    shell_gfx_write_label_uint(text_x, text_y + 22, "Bytes: ", gfx_viewer_size, UI_MUTED);

    if (!gfx_viewer_loaded) {
        ui_write_text(text_x, text_y + 50, "Unable to open file", 0x00B83742);
        return;
    }

    current[0] = '\0';
    for (uint32_t index = 0; gfx_viewer_text[index] != '\0' && line < 5; index++) {
        char character = gfx_viewer_text[index];

        if (character == '\n' || column >= sizeof(current) - 1 ||
                ui_text_width(current) + 16 > window->width - 24) {
            current[column] = '\0';
            ui_write_text_clipped(text_x, text_y + 50 + line * 24, window->width - 24, current, UI_TEXT);
            line++;
            column = 0;
            current[0] = '\0';
            if (character == '\n') {
                continue;
            }
        }

        if (character < 32 || character > 126) {
            character = '.';
        }
        current[column++] = character;
        current[column] = '\0';
    }

    if (line < 5 && column > 0) {
        ui_write_text_clipped(text_x, text_y + 50 + line * 24, window->width - 24, current, UI_TEXT);
    }
}

static void shell_gfx_draw_window_settings_content(const struct shell_gfx_window* window) {
    ui_write_text(window->x + 16, window->y + 35, "Desktop background", UI_TEXT);
    for (int index = 0; index < 3; index++) {
        struct ui_rect rect = {window->x + 16 + index * 76, window->y + 58, 64, 36};
        framebuffer_fill_rect(rect.x, rect.y, rect.width, rect.height, gfx_desktop_colors[index]);
        framebuffer_draw_rect(rect.x, rect.y, rect.width, rect.height,
            index == gfx_desktop_color_index ? 0x0048D597 : 0x00747C86);
        if (index == gfx_desktop_color_index) {
            framebuffer_draw_rect(rect.x + 2, rect.y + 2, rect.width - 4, rect.height - 4, 0x0048D597);
        }
    }
    ui_draw_checkbox((struct ui_rect){window->x + 16, window->y + 112, window->width - 32, 20},
        "Live widgets", gfx_live_widgets);
    ui_draw_button(&(struct ui_button){{window->x + 16, window->y + 154, 164, 28},
        "Arrange windows", 0x00FFFFFF, UI_TEXT, 0, 0});
    ui_draw_button(&(struct ui_button){{window->x + 192, window->y + 154, 164, 28},
        "Open terminal", 0x00FFFFFF, UI_TEXT, 0, 0});
}

static void shell_gfx_draw_window_content(const struct shell_gfx_window* window) {
    switch (window->type) {
        case SHELL_GFX_WINDOW_FILES:
            shell_gfx_draw_window_files_content(window);
            break;
        case SHELL_GFX_WINDOW_TASKS:
            shell_gfx_draw_window_tasks_content(window);
            break;
        case SHELL_GFX_WINDOW_APPS:
            shell_gfx_draw_window_apps_content(window);
            break;
        case SHELL_GFX_WINDOW_SHELL:
            framebuffer_console_set_window((uint32_t)window->x + 6, (uint32_t)window->y + 30,
                (uint32_t)window->width - 12, (uint32_t)window->height - 36);
            break;
        case SHELL_GFX_WINDOW_CLOCK:
            shell_gfx_draw_window_clock_content(window);
            break;
        case SHELL_GFX_WINDOW_DEMO:
            shell_gfx_draw_window_demo_content(window);
            break;
        case SHELL_GFX_WINDOW_VIEWER:
            shell_gfx_draw_window_viewer_content(window);
            break;
        case SHELL_GFX_WINDOW_SETTINGS:
            shell_gfx_draw_window_settings_content(window);
            break;
        default:
            break;
    }
}

static void shell_gfx_draw_window(const struct shell_gfx_window* window, int active) {
    uint32_t border = active ? UI_ACCENT : UI_BORDER;
    static const char* titles[] = {"Files", "Tasks", "Applications", "Terminal",
        "Clock", "Demo", "Preview", "Settings"};

    if (!window->visible) {
        return;
    }

    ui_set_pointer(gfx_cursor_x, gfx_cursor_y,
        shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y) == (int)(window - gfx_windows));
    framebuffer_fill_rect((uint32_t)window->x + 4, (uint32_t)window->y + 4,
        (uint32_t)window->width, (uint32_t)window->height, 0x002C403D);
    framebuffer_fill_rect((uint32_t)window->x, (uint32_t)window->y,
        (uint32_t)window->width, (uint32_t)window->height, UI_SURFACE);
    framebuffer_fill_rect((uint32_t)window->x, (uint32_t)window->y,
        (uint32_t)window->width, SHELL_GFX_WINDOW_TITLE_HEIGHT, active ? 0x00DBE9FF : 0x00E9ECF0);
    framebuffer_draw_rect((uint32_t)window->x, (uint32_t)window->y,
        (uint32_t)window->width, (uint32_t)window->height, border);
    ui_write_text_clipped(window->x + 12, window->y + 4, window->width - 94,
        titles[window->type], UI_TEXT);
    ui_draw_icon_button(shell_gfx_window_control_rect(window, 0), UI_ICON_CLOSE, 0, 0);
    ui_draw_icon_button(shell_gfx_window_control_rect(window, 1),
        window->maximized ? UI_ICON_RESTORE : UI_ICON_MAXIMIZE, 0, 0);
    ui_draw_icon_button(shell_gfx_window_control_rect(window, 2), UI_ICON_MINIMIZE, 0, 0);
    shell_gfx_draw_window_content(window);
    ui_draw_tooltip((struct ui_rect){window->x, window->y, window->width, window->height});
}

static void shell_gfx_draw_windows(void) {
    for (int z = 0; z < SHELL_GFX_WINDOW_COUNT; z++) {
        for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
            if (gfx_windows[index].visible && gfx_windows[index].z == z) {
                shell_gfx_draw_window(&gfx_windows[index], index == gfx_windows_active);
            }
        }
    }
}

static void shell_gfx_redraw_windows_desktop(void) {
    framebuffer_reset_mouse_cursor();
    shell_gfx_draw_window_workspace();
    shell_gfx_draw_windows();
    if (gfx_cursor_enabled) {
        shell_gfx_draw_cursor();
    }
}

static int shell_gfx_rect_intersects(struct shell_gfx_rect a, struct shell_gfx_rect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
        a.y < b.y + b.height && a.y + a.height > b.y;
}

static struct shell_gfx_rect shell_gfx_window_dirty_rect(const struct shell_gfx_window* window) {
    struct shell_gfx_rect rect;

    rect.x = window->x;
    rect.y = window->y;
    rect.width = window->width + 4;
    rect.height = window->height + 4;
    return rect;
}

static void shell_gfx_restore_desktop_rect(struct shell_gfx_rect dirty) {
    const struct framebuffer_info* info = framebuffer_get_info();
    struct task_summary summary;
    struct kfs_usage usage;

    if (dirty.width <= 0 || dirty.height <= 0) {
        return;
    }

    if (dirty.x < 0) {
        dirty.width += dirty.x;
        dirty.x = 0;
    }
    if (dirty.y < 0) {
        dirty.height += dirty.y;
        dirty.y = 0;
    }
    if (dirty.x + dirty.width > (int)info->width) {
        dirty.width = (int)info->width - dirty.x;
    }
    if (dirty.y + dirty.height > (int)info->height) {
        dirty.height = (int)info->height - dirty.y;
    }
    if (dirty.width <= 0 || dirty.height <= 0) {
        return;
    }

    framebuffer_fill_rect((uint32_t)dirty.x, (uint32_t)dirty.y,
        (uint32_t)dirty.width, (uint32_t)dirty.height,
        gfx_desktop_clean ? gfx_desktop_colors[gfx_desktop_color_index] : 0x00181A20);
    if (dirty.y < 18) {
        framebuffer_fill_rect(0, 0, info->width, 18, 0x00262A33);
        framebuffer_write_text(8, 6, gfx_desktop_clean ? "KERNEL1 DESKTOP" : "KERNEL1", 0x00E8EAED);
        if (gfx_desktop_clean) {
            framebuffer_write_text(info->width > 190 ? info->width - 182 : 8, 6,
                "GFX DESKTOP", 0x0048D597);
        }
    }
    if (dirty.y + dirty.height > (int)info->height - 22) {
        framebuffer_fill_rect(0, info->height - 22, info->width, 22, 0x00262A33);
    }

    if (gfx_desktop_clean) {
        if (dirty.y < 30 || dirty.y + dirty.height > (int)info->height - 24) {
            shell_gfx_draw_desktop_bars();
        }
        if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){SHELL_GFX_DOCK_X,
                SHELL_GFX_DOCK_Y, SHELL_GFX_DOCK_WIDTH + 4, SHELL_GFX_DOCK_HEIGHT + 4})) {
            shell_gfx_draw_desktop_dock();
        }
        return;
    }

    task_get_summary(&summary);
    if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){360, 38, 294, 116})) {
        framebuffer_draw_status_panel(timer_ticks(), pmm_used_pages(), pmm_free_pages(),
            scheduler_mode_name(scheduler_get_mode()), scheduler_preemption_count());
    }
    if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){680, 38, 234, 116})) {
        framebuffer_draw_task_panel(summary.ready, summary.running, summary.sleeping,
            summary.exited, summary.unused, summary.next_id);
    }
    if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){680, 164, 234, 116})) {
        if (kfs_get_usage(&usage)) {
            framebuffer_draw_storage_panel(1, usage.used_sectors, usage.free_sectors,
                usage.used_data_sectors, usage.data_sectors - usage.used_data_sectors,
                usage.percent_used);
        } else {
            framebuffer_draw_storage_panel(0, 0, 0, 0, 0, 0);
        }
    }
    if (!gfx_dashboard_compact) {
        if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){172, 48, 144, 122})) {
            shell_gfx_draw_files_panel_for_path(shell_fs_current_directory());
        }
        if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){360, 164, 294, 116})) {
            shell_gfx_draw_apps_panel();
        }
        if (shell_gfx_rect_intersects(dirty, (struct shell_gfx_rect){46, 194, 274, 112})) {
            framebuffer_draw_launcher_panel();
        }
    }
}

static void shell_gfx_redraw_windows_dirty(struct shell_gfx_rect old_rect, struct shell_gfx_rect new_rect) {
    framebuffer_erase_mouse_cursor();
    shell_gfx_restore_desktop_rect(old_rect);
    shell_gfx_restore_desktop_rect(new_rect);
    if (gfx_desktop_clean) {
        shell_gfx_draw_desktop_dock();
    }
    shell_gfx_draw_windows();
    if (gfx_cursor_enabled) {
        shell_gfx_draw_cursor();
    }
}

static int shell_gfx_window_type_visible(enum shell_gfx_window_type type) {
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        if (gfx_windows[index].type == type && gfx_windows[index].visible) {
            return 1;
        }
    }

    return 0;
}

static void shell_gfx_refresh_window_type(enum shell_gfx_window_type type) {
    struct shell_gfx_rect dirty = {0, 0, 0, 0};
    int found = 0;

    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        if (gfx_windows[index].type == type && gfx_windows[index].visible) {
            dirty = shell_gfx_window_dirty_rect(&gfx_windows[index]);
            found = 1;
            break;
        }
    }

    if (!found) {
        return;
    }

    framebuffer_erase_mouse_cursor();
    shell_gfx_restore_desktop_rect(dirty);
    shell_gfx_draw_windows();
    if (gfx_cursor_enabled) {
        shell_gfx_draw_cursor();
    }
}

static void shell_gfx_refresh_live_windows(void) {
    const struct mouse_state* state = mouse_get_state();
    uint32_t ticks = timer_ticks();

    if (!gfx_windows_enabled || !gfx_live_widgets || gfx_windows_dragging || !framebuffer_get_info()->hardware_backed) {
        return;
    }

    if (state->buttons != 0 || ticks - gfx_cursor_last_motion_tick < 12) {
        return;
    }

    if (ticks - gfx_windows_last_fast_refresh_tick >= 18) {
        gfx_windows_last_fast_refresh_tick = ticks;
        if (shell_gfx_window_type_visible(SHELL_GFX_WINDOW_TASKS)) {
            shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_TASKS);
        }
        if (shell_gfx_window_type_visible(SHELL_GFX_WINDOW_CLOCK)) {
            shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_CLOCK);
        }
        if (shell_gfx_window_type_visible(SHELL_GFX_WINDOW_DEMO)) {
            shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_DEMO);
        }
    }

    if (ticks - gfx_windows_last_slow_refresh_tick >= 90) {
        gfx_windows_last_slow_refresh_tick = ticks;
        if (shell_gfx_window_type_visible(SHELL_GFX_WINDOW_FILES)) {
            shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_FILES);
        }
        if (shell_gfx_window_type_visible(SHELL_GFX_WINDOW_APPS)) {
            shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_APPS);
        }
    }
}

static int shell_gfx_window_contains(const struct shell_gfx_window* window, int x, int y) {
    if (!window->visible) {
        return 0;
    }

    return shell_gfx_point_in_rect(x, y, window->x, window->y, window->width, window->height);
}

static int shell_gfx_window_title_contains(const struct shell_gfx_window* window, int x, int y) {
    if (!window->visible) {
        return 0;
    }

    return shell_gfx_point_in_rect(x, y, window->x, window->y, window->width, SHELL_GFX_WINDOW_TITLE_HEIGHT);
}

static int shell_gfx_window_close_contains(const struct shell_gfx_window* window, int x, int y) {
    if (!window->visible) {
        return 0;
    }

    return ui_point_in_rect(x, y, shell_gfx_window_control_rect(window, 0));
}

static int shell_gfx_window_minimize_contains(const struct shell_gfx_window* window, int x, int y) {
    if (!window->visible) {
        return 0;
    }

    return ui_point_in_rect(x, y, shell_gfx_window_control_rect(window, 2));
}

static int shell_gfx_window_maximize_contains(const struct shell_gfx_window* window, int x, int y) {
    if (!window->visible) {
        return 0;
    }

    return ui_point_in_rect(x, y, shell_gfx_window_control_rect(window, 1));
}

static int shell_gfx_top_window_at(int x, int y) {
    for (int z = SHELL_GFX_WINDOW_COUNT - 1; z >= 0; z--) {
        for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
            if (gfx_windows[index].z == z && shell_gfx_window_contains(&gfx_windows[index], x, y)) {
                return index;
            }
        }
    }

    return -1;
}

static void shell_gfx_bring_window_to_front(int active_index) {
    int old_z;

    if (active_index < 0 || active_index >= SHELL_GFX_WINDOW_COUNT || !gfx_windows[active_index].visible) {
        return;
    }

    old_z = gfx_windows[active_index].z;
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        if (gfx_windows[index].z > old_z) {
            gfx_windows[index].z--;
        }
    }

    gfx_windows[active_index].z = SHELL_GFX_WINDOW_COUNT - 1;
    gfx_windows_active = active_index;
}

static void shell_gfx_reset_windows(void) {
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        gfx_windows[index].z = index;
        gfx_windows[index].visible = index == SHELL_GFX_WINDOW_FILES || index == SHELL_GFX_WINDOW_APPS;
        gfx_windows[index].minimized = 0;
    }
    shell_gfx_arrange_windows();
    gfx_clock_app_started = 0;
    gfx_demo_app_started = 0;
    gfx_files_selected_index = -1;
    gfx_files_page = 0;
    gfx_files_selected_name[0] = '\0';
    gfx_apps_selected_index = -1;
    gfx_viewer_path[0] = '\0';
    gfx_viewer_text[0] = '\0';
    gfx_viewer_size = 0;
    gfx_viewer_loaded = 0;
    shell_gfx_bring_window_to_front(SHELL_GFX_WINDOW_FILES);
    gfx_windows_dragging = 0;
}

static void shell_gfx_arrange_windows(void) {
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        const struct shell_gfx_rect* rect = &gfx_window_layout[index];
        gfx_windows[index].x = rect->x;
        gfx_windows[index].y = rect->y;
        gfx_windows[index].width = rect->width;
        gfx_windows[index].height = rect->height;
        gfx_windows[index].maximized = 0;
        shell_gfx_clamp_window(&gfx_windows[index]);
    }
    gfx_windows_dragging = 0;
}

static int shell_gfx_show_window(enum shell_gfx_window_type type) {
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        if (gfx_windows[index].type == type) {
            gfx_windows[index].visible = 1;
            gfx_windows[index].minimized = 0;
            shell_gfx_clamp_window(&gfx_windows[index]);
            shell_gfx_bring_window_to_front(index);
            shell_gfx_redraw_windows_desktop();
            return 1;
        }
    }

    return 0;
}

static int shell_gfx_open_app_window(enum shell_gfx_window_type type, const char* app_name) {
    int* started = 0;

    if (type == SHELL_GFX_WINDOW_CLOCK) {
        started = &gfx_clock_app_started;
    } else if (type == SHELL_GFX_WINDOW_DEMO) {
        started = &gfx_demo_app_started;
    }

    if (started == 0 || !*started) {
        if (!shell_gfx_spawn_app(app_name)) {
            return 0;
        }
        if (started != 0) {
            *started = 1;
        }
    }

    return shell_gfx_show_window(type);
}

void shell_system_note_app_spawn(const char* name) {
    if (!gfx_windows_enabled || !gfx_desktop_clean || !framebuffer_get_info()->hardware_backed) {
        return;
    }

    if (string_equals_ci(name, "clock")) {
        gfx_clock_app_started = 1;
        shell_gfx_show_window(SHELL_GFX_WINDOW_CLOCK);
    } else if (string_equals_ci(name, "demo")) {
        gfx_demo_app_started = 1;
        shell_gfx_show_window(SHELL_GFX_WINDOW_DEMO);
    }
}

static int shell_gfx_open_viewer_file(const char* path, uint32_t size) {
    int fd;
    int32_t bytes;

    shell_gfx_copy_limited(gfx_viewer_path, sizeof(gfx_viewer_path), path);
    gfx_viewer_size = size;
    gfx_viewer_text[0] = '\0';
    gfx_viewer_loaded = 0;

    fd = vfs_open_flags(path, VFS_O_READ);
    if (fd == VFS_INVALID_FD) {
        return shell_gfx_show_window(SHELL_GFX_WINDOW_VIEWER);
    }

    bytes = vfs_read(fd, gfx_viewer_text, sizeof(gfx_viewer_text) - 1);
    vfs_close(fd);
    if (bytes < 0) {
        return shell_gfx_show_window(SHELL_GFX_WINDOW_VIEWER);
    }

    gfx_viewer_text[(uint32_t)bytes] = '\0';
    gfx_viewer_loaded = 1;
    return shell_gfx_show_window(SHELL_GFX_WINDOW_VIEWER);
}

static int shell_gfx_activate_files_window_item(const struct shell_gfx_window* window, int x, int y) {
    struct vfs_dir_entry entry;
    char path[SHELL_FS_PATH_SIZE];
    uint32_t rows = shell_gfx_files_rows(window);

    if (window->type != SHELL_GFX_WINDOW_FILES) {
        return 0;
    }

    for (int tool = 0; tool < 4; tool++) {
        if (!ui_point_in_rect(x, y, shell_gfx_files_tool_rect(window, tool))) {
            continue;
        }
        if (tool == 0) {
            shell_fs_change_directory("/");
            gfx_files_page = 0;
        } else if (tool == 1) {
            const char* directory = shell_fs_current_directory();
            size_t last_slash = 0;
            path[0] = '/';
            shell_gfx_copy_limited(path + 1, sizeof(path) - 1, directory);
            for (size_t index = 1; path[index] != '\0'; index++) {
                if (path[index] == '/') {
                    last_slash = index;
                }
            }
            path[last_slash ? last_slash : 1] = '\0';
            shell_fs_change_directory(path);
            gfx_files_page = 0;
        } else if (tool == 2) {
            gfx_files_page = gfx_files_page > rows ? gfx_files_page - rows : 0;
        } else if (vfs_read_dir(shell_fs_current_directory(), gfx_files_page + rows, &entry)) {
            gfx_files_page += rows;
        }
        gfx_files_selected_index = -1;
        return 1;
    }
    for (uint32_t row = 0; row < rows; row++) {
        uint32_t index = gfx_files_page + row;
        if (!shell_gfx_point_in_rect(x, y, window->x + 10, window->y + 88 + (int)row * 22,
                window->width - 20, 21)) {
            continue;
        }
        if (!vfs_read_dir(shell_fs_current_directory(), index, &entry)) {
            return 0;
        }

        gfx_files_selected_index = (int)index;
        shell_gfx_copy_limited(gfx_files_selected_name, sizeof(gfx_files_selected_name), entry.name);
        gfx_files_selected_type = entry.type;
        gfx_files_selected_size = entry.size;

        shell_fs_resolve_path(entry.name, path, sizeof(path));
        if (entry.type == VFS_NODE_DIRECTORY) {
            shell_fs_change_directory(entry.name);
            gfx_files_selected_index = -1;
            gfx_files_selected_name[0] = '\0';
            gfx_files_page = 0;
            return 1;
        }

        return shell_gfx_open_viewer_file(path, entry.size);
    }

    return 0;
}

static int shell_gfx_activate_apps_window_item(const struct shell_gfx_window* window, int x, int y) {
    if (window->type != SHELL_GFX_WINDOW_APPS) {
        return 0;
    }

    for (unsigned index = 0; index < sizeof(gfx_app_items) / sizeof(gfx_app_items[0]); index++) {
        if (ui_point_in_rect(x, y, shell_gfx_app_item_rect(window, index))) {
            return shell_gfx_show_window(gfx_dock_items[gfx_app_items[index]].window);
        }
    }

    return 0;
}

static int shell_gfx_activate_settings_window_item(const struct shell_gfx_window* window, int x, int y) {
    if (window->type != SHELL_GFX_WINDOW_SETTINGS) {
        return 0;
    }
    for (int index = 0; index < 3; index++) {
        if (ui_point_in_rect(x, y, (struct ui_rect){window->x + 16 + index * 76,
                window->y + 58, 64, 36})) {
            gfx_desktop_color_index = index;
            return 1;
        }
    }
    if (ui_point_in_rect(x, y, (struct ui_rect){window->x + 16, window->y + 112, window->width - 32, 20})) {
        gfx_live_widgets = !gfx_live_widgets;
        return 1;
    }
    if (ui_point_in_rect(x, y, (struct ui_rect){window->x + 16, window->y + 154, 164, 28})) {
        shell_gfx_arrange_windows();
        return 1;
    }
    if (ui_point_in_rect(x, y, (struct ui_rect){window->x + 192, window->y + 154, 164, 28})) {
        return shell_gfx_show_window(SHELL_GFX_WINDOW_SHELL);
    }
    return 0;
}

static int shell_gfx_hover_target(void) {
    int target = shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y);
    if (target < 0) {
        if (gfx_desktop_clean) {
            for (unsigned item = 0; item < sizeof(gfx_dock_items) / sizeof(gfx_dock_items[0]); item++) {
                if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, shell_gfx_dock_item_rect(item))) return item + 1;
            }
        }
        return 0;
    }
    const struct shell_gfx_window* window = &gfx_windows[target];
    int id = 100 + target * 100;
    for (int control = 0; control < 3; control++) {
        if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, shell_gfx_window_control_rect(window, control))) {
            return id + control;
        }
    }
    if (window->type == SHELL_GFX_WINDOW_FILES) {
        for (int tool = 0; tool < 4; tool++) {
            if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, shell_gfx_files_tool_rect(window, tool))) return id + 10 + tool;
        }
        for (unsigned row = 0; row < shell_gfx_files_rows(window); row++) {
            struct ui_rect rect = {window->x + 10, window->y + 88 + (int)row * 22, window->width - 20, 21};
            if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, rect)) return id + 20 + row;
        }
    } else if (window->type == SHELL_GFX_WINDOW_APPS) {
        for (unsigned item = 0; item < sizeof(gfx_app_items) / sizeof(gfx_app_items[0]); item++) {
            if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, shell_gfx_app_item_rect(window, item))) return id + 10 + item;
        }
    } else if (window->type == SHELL_GFX_WINDOW_SETTINGS) {
        for (int item = 0; item < 2; item++) {
            struct ui_rect rect = {window->x + 16 + item * 176, window->y + 154, 164, 28};
            if (ui_point_in_rect(gfx_cursor_x, gfx_cursor_y, rect)) return id + 10 + item;
        }
    }
    return 0;
}

static void shell_gfx_toggle_maximize_window(int index) {
    const struct framebuffer_info* info = framebuffer_get_info();
    struct shell_gfx_window* window;

    if (index < 0 || index >= SHELL_GFX_WINDOW_COUNT) {
        return;
    }

    window = &gfx_windows[index];
    if (!window->maximized) {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
        window->x = gfx_desktop_clean ? 220 : 42;
        window->y = 34;
        window->width = info->width > (uint32_t)(window->x + 36) ?
            (int)info->width - window->x - 36 : window->width;
        if (gfx_desktop_clean || window->type == SHELL_GFX_WINDOW_SHELL) {
            window->height = info->height > 90 ? (int)info->height - 110 : window->height;
        } else {
            window->height = info->height > 294 ? (int)info->height - 294 : window->height;
        }
        if (window->height < 120) {
            window->height = 120;
        }
        window->maximized = 1;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->maximized = 0;
        shell_gfx_clamp_window(window);
    }
}

static void shell_gfx_clamp_window(struct shell_gfx_window* window) {
    const struct framebuffer_info* info = framebuffer_get_info();
    int max_x = info->width > (uint32_t)window->width ? (int)info->width - window->width : 0;
    int bottom_margin = gfx_desktop_clean ? 28 : 210;
    int max_y = (int)info->height - window->height - bottom_margin;
    if (max_y < 34) {
        max_y = 34;
    }

    if (window->x < 0) {
        window->x = 0;
    } else if (window->x > max_x) {
        window->x = max_x;
    }

    if (window->y < 34) {
        window->y = 34;
    } else if (window->y > max_y) {
        window->y = max_y;
    }
}

static int shell_gfx_handle_live_windows(void) {
    const struct mouse_state* state = mouse_get_state();
    int left_down = (state->buttons & 0x01) != 0;
    int left_was_down = (gfx_windows_last_buttons & 0x01) != 0;
    int consumed = 0;

    if (!gfx_windows_enabled) {
        gfx_windows_dragging = 0;
        gfx_windows_last_buttons = state->buttons;
        return 0;
    }

    if ((left_down && !left_was_down) || state->left_clicks != gfx_cursor_last_left_clicks) {
        int target = shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y);

        if (target >= 0) {
            shell_gfx_bring_window_to_front(target);
            gfx_cursor_last_left_clicks = state->left_clicks;

            if (shell_gfx_window_close_contains(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                struct shell_gfx_rect old_rect = shell_gfx_window_dirty_rect(&gfx_windows[target]);

                gfx_windows[target].visible = 0;
                gfx_windows[target].minimized = 0;
                gfx_windows_dragging = 0;
                gfx_windows_active = -1;
                shell_gfx_redraw_windows_dirty(old_rect, old_rect);
            } else if (shell_gfx_window_minimize_contains(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                struct shell_gfx_rect old_rect = shell_gfx_window_dirty_rect(&gfx_windows[target]);

                gfx_windows[target].visible = 0;
                gfx_windows[target].minimized = 1;
                gfx_windows_dragging = 0;
                gfx_windows_active = -1;
                shell_gfx_redraw_windows_dirty(old_rect, old_rect);
            } else if (shell_gfx_window_maximize_contains(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                struct shell_gfx_rect old_rect = shell_gfx_window_dirty_rect(&gfx_windows[target]);

                shell_gfx_toggle_maximize_window(target);
                shell_gfx_redraw_windows_dirty(old_rect, shell_gfx_window_dirty_rect(&gfx_windows[target]));
            } else if (shell_gfx_window_title_contains(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                if (left_down && !gfx_windows[target].maximized) {
                    gfx_windows_dragging = 1;
                    gfx_windows_drag_offset_x = gfx_cursor_x - gfx_windows[target].x;
                    gfx_windows_drag_offset_y = gfx_cursor_y - gfx_windows[target].y;
                }
                shell_gfx_redraw_windows_desktop();
            } else if (shell_gfx_activate_files_window_item(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                shell_gfx_redraw_windows_desktop();
            } else if (shell_gfx_activate_apps_window_item(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                shell_gfx_redraw_windows_desktop();
            } else if (shell_gfx_activate_settings_window_item(&gfx_windows[target], gfx_cursor_x, gfx_cursor_y)) {
                shell_gfx_redraw_windows_desktop();
            } else {
                shell_gfx_redraw_windows_desktop();
            }
            consumed = 1;
        }
    } else if (left_down && gfx_windows_dragging && gfx_windows_active >= 0) {
        struct shell_gfx_rect old_rect = shell_gfx_window_dirty_rect(&gfx_windows[gfx_windows_active]);

        gfx_windows[gfx_windows_active].x = gfx_cursor_x - gfx_windows_drag_offset_x;
        gfx_windows[gfx_windows_active].y = gfx_cursor_y - gfx_windows_drag_offset_y;
        shell_gfx_clamp_window(&gfx_windows[gfx_windows_active]);
        shell_gfx_redraw_windows_dirty(old_rect, shell_gfx_window_dirty_rect(&gfx_windows[gfx_windows_active]));
        consumed = 1;
    } else if (!left_down && left_was_down) {
        gfx_windows_dragging = 0;
    }

    gfx_windows_last_buttons = state->buttons;
    return consumed;
}

static void shell_gfx_print_windows_status(void) {
    terminal_write("gfx windows: ");
    terminal_write(gfx_windows_enabled ? "on\n" : "off\n");
    terminal_write("  active: ");
    if (gfx_windows_active >= 0) {
        terminal_write(gfx_windows[gfx_windows_active].title);
    } else {
        terminal_write("none");
    }
    terminal_write("\n");
    for (int index = 0; index < SHELL_GFX_WINDOW_COUNT; index++) {
        terminal_write("  ");
        terminal_write(gfx_windows[index].title);
        if (gfx_windows[index].visible) {
            terminal_write(gfx_windows[index].maximized ? ": maximized\n" : ": visible\n");
        } else {
            terminal_write(gfx_windows[index].minimized ? ": minimized\n" : ": hidden\n");
        }
    }
}

static int shell_gfx_spawn_app(const char* name) {
    const struct app_descriptor* app = app_find(name);
    struct task* task;

    if (app == 0) {
        terminal_write("gfx click: app not found: ");
        terminal_write(name);
        terminal_write("\n");
        return 0;
    }

    task = user_mode_spawn_app_with_args(app->name, app->entry, "");
    if (task == 0) {
        terminal_write("gfx click: spawn failed: ");
        terminal_write(name);
        terminal_write("\n");
        return 0;
    }

    terminal_write("gfx click: spawned ");
    terminal_write(name);
    terminal_write("\n");
    return 1;
}

static int shell_gfx_activate_target(enum shell_gfx_target target) {
    struct kfs_usage usage;

    switch (target) {
        case SHELL_GFX_TARGET_LAUNCHER_RUN_DEMO:
            return shell_gfx_open_app_window(SHELL_GFX_WINDOW_DEMO, "demo");
        case SHELL_GFX_TARGET_LAUNCHER_RUN_CLOCK:
            return shell_gfx_open_app_window(SHELL_GFX_WINDOW_CLOCK, "clock");
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_DEMO:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_DEMO);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_CLOCK:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_CLOCK);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_FILES:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_FILES);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_TASKS:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_TASKS);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_APPS:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_APPS);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SHELL:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_SHELL);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SETTINGS:
            return shell_gfx_show_window(SHELL_GFX_WINDOW_SETTINGS);
        case SHELL_GFX_TARGET_DESKTOP_DOCK_DASHBOARD:
            gfx_desktop_clean = 0;
            shell_gfx_redraw_windows_desktop();
            terminal_write("gfx: dashboard workspace\n");
            return 1;
        case SHELL_GFX_TARGET_LAUNCHER_FILES_DISK:
            shell_gfx_draw_files_panel_for_path("disk");
            shell_gfx_draw_cursor();
            terminal_write("gfx click: opened /disk files panel\n");
            return 1;
        case SHELL_GFX_TARGET_LAUNCHER_APPS:
            shell_gfx_draw_apps_panel();
            shell_gfx_draw_cursor();
            terminal_write("gfx click: opened apps panel\n");
            return 1;
        case SHELL_GFX_TARGET_LAUNCHER_GFX_AUTO:
            gfx_auto_dashboard = !gfx_auto_dashboard;
            terminal_write("gfx click: auto dashboard ");
            terminal_write(gfx_auto_dashboard ? "on\n" : "off\n");
            return 1;
        case SHELL_GFX_TARGET_LAUNCHER_STATUS:
        case SHELL_GFX_TARGET_STATUS:
            framebuffer_draw_status_panel(timer_ticks(), pmm_used_pages(), pmm_free_pages(),
                scheduler_mode_name(scheduler_get_mode()), scheduler_preemption_count());
            shell_gfx_draw_cursor();
            terminal_write("gfx click: refreshed status panel\n");
            return 1;
        case SHELL_GFX_TARGET_TASKS:
            task_print_summary();
            return 1;
        case SHELL_GFX_TARGET_DISK:
            if (kfs_get_usage(&usage)) {
                framebuffer_draw_storage_panel(1, usage.used_sectors, usage.free_sectors,
                    usage.used_data_sectors, usage.data_sectors - usage.used_data_sectors,
                    usage.percent_used);
            } else {
                framebuffer_draw_storage_panel(0, 0, 0, 0, 0, 0);
            }
            shell_gfx_draw_cursor();
            terminal_write("gfx click: refreshed disk panel\n");
            return 1;
        case SHELL_GFX_TARGET_FILES:
            shell_gfx_draw_files_panel_for_path(shell_fs_current_directory());
            shell_gfx_draw_cursor();
            terminal_write("gfx click: refreshed files panel\n");
            return 1;
        case SHELL_GFX_TARGET_APPS:
            app_print_all();
            return 1;
        default:
            terminal_write("gfx click: no action for target\n");
            return 0;
    }
}

static int shell_gfx_target_has_click_action(enum shell_gfx_target target) {
    switch (target) {
        case SHELL_GFX_TARGET_STATUS:
        case SHELL_GFX_TARGET_TASKS:
        case SHELL_GFX_TARGET_DISK:
        case SHELL_GFX_TARGET_FILES:
        case SHELL_GFX_TARGET_APPS:
        case SHELL_GFX_TARGET_LAUNCHER_RUN_DEMO:
        case SHELL_GFX_TARGET_LAUNCHER_RUN_CLOCK:
        case SHELL_GFX_TARGET_LAUNCHER_FILES_DISK:
        case SHELL_GFX_TARGET_LAUNCHER_APPS:
        case SHELL_GFX_TARGET_LAUNCHER_GFX_AUTO:
        case SHELL_GFX_TARGET_LAUNCHER_STATUS:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_FILES:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_TASKS:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_APPS:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_DEMO:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_RUN_CLOCK:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SHELL:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_DASHBOARD:
        case SHELL_GFX_TARGET_DESKTOP_DOCK_SETTINGS:
            return 1;
        default:
            return 0;
    }
}

static void shell_gfx_handle_live_clicks(void) {
    const struct mouse_state* state = mouse_get_state();
    enum shell_gfx_target target;

    if (state->left_clicks == gfx_cursor_last_left_clicks) {
        return;
    }

    if (gfx_windows_enabled && gfx_desktop_clean &&
            shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y) >= 0) {
        gfx_cursor_last_left_clicks = state->left_clicks;
        return;
    }

    target = shell_gfx_cursor_target();
    gfx_cursor_last_left_clicks = state->left_clicks;

    if (shell_gfx_target_has_click_action(target)) {
        shell_gfx_activate_target(target);
    }
}

static void shell_gfx_print_click_status(void) {
    const struct mouse_state* state = mouse_get_state();
    enum shell_gfx_target target;
    uint32_t new_left = state->left_clicks - gfx_cursor_last_left_clicks;
    uint32_t new_right = state->right_clicks - gfx_cursor_last_right_clicks;
    uint32_t new_middle = state->middle_clicks - gfx_cursor_last_middle_clicks;

    shell_gfx_apply_cursor_delta();
    if (gfx_windows_enabled && gfx_desktop_clean &&
            shell_gfx_top_window_at(gfx_cursor_x, gfx_cursor_y) >= 0) {
        terminal_write("gfx click: window\n");
        terminal_write("  x=");
        terminal_write_dec((uint32_t)(gfx_cursor_x < 0 ? 0 : gfx_cursor_x));
        terminal_write(" y=");
        terminal_write_dec((uint32_t)(gfx_cursor_y < 0 ? 0 : gfx_cursor_y));
        terminal_write(" buttons=");
        terminal_write_dec(state->buttons);
        terminal_write(" new L/R/M=");
        terminal_write_dec(new_left);
        terminal_write("/");
        terminal_write_dec(new_right);
        terminal_write("/");
        terminal_write_dec(new_middle);
        terminal_write("\n");
        gfx_cursor_last_left_clicks = state->left_clicks;
        gfx_cursor_last_right_clicks = state->right_clicks;
        gfx_cursor_last_middle_clicks = state->middle_clicks;
        return;
    }

    target = shell_gfx_cursor_target();
    terminal_write("gfx click: ");
    terminal_write(shell_gfx_target_name(target));
    terminal_write("\n  x=");
    terminal_write_dec((uint32_t)(gfx_cursor_x < 0 ? 0 : gfx_cursor_x));
    terminal_write(" y=");
    terminal_write_dec((uint32_t)(gfx_cursor_y < 0 ? 0 : gfx_cursor_y));
    terminal_write(" buttons=");
    terminal_write_dec(state->buttons);
    terminal_write(" new L/R/M=");
    terminal_write_dec(new_left);
    terminal_write("/");
    terminal_write_dec(new_right);
    terminal_write("/");
    terminal_write_dec(new_middle);
    terminal_write("\n");

    if (new_left > 0) {
        shell_gfx_activate_target(target);
    }

    gfx_cursor_last_left_clicks = state->left_clicks;
    gfx_cursor_last_right_clicks = state->right_clicks;
    gfx_cursor_last_middle_clicks = state->middle_clicks;
}

static void shell_gfx_draw_dashboard(void) {
    struct task_summary summary;
    struct kfs_usage usage;

    task_get_summary(&summary);
    framebuffer_demo_desktop();
    framebuffer_draw_status_panel(timer_ticks(), pmm_used_pages(), pmm_free_pages(),
        scheduler_mode_name(scheduler_get_mode()), scheduler_preemption_count());
    framebuffer_draw_task_panel(summary.ready, summary.running, summary.sleeping,
        summary.exited, summary.unused, summary.next_id);
    if (!gfx_dashboard_compact) {
        shell_gfx_draw_files_panel_for_path(shell_fs_current_directory());
        shell_gfx_draw_apps_panel();
        framebuffer_draw_launcher_panel();
    }
    if (kfs_get_usage(&usage)) {
        framebuffer_draw_storage_panel(1, usage.used_sectors, usage.free_sectors,
            usage.used_data_sectors, usage.data_sectors - usage.used_data_sectors,
            usage.percent_used);
    } else {
        framebuffer_draw_storage_panel(0, 0, 0, 0, 0, 0);
    }
    shell_gfx_restore_console_overlay();
}

static void shell_gfx_draw_window_workspace(void) {
    if (gfx_desktop_clean) {
        shell_gfx_draw_clean_desktop();
    } else {
        shell_gfx_draw_dashboard();
        shell_gfx_restore_console_overlay();
    }
}

static void shell_gfx_refresh_dashboard_panels(void) {
    struct task_summary summary;
    struct kfs_usage usage;

    task_get_summary(&summary);
    if (!gfx_dashboard_compact) {
        shell_gfx_draw_files_panel_for_path(shell_fs_current_directory());
        shell_gfx_draw_apps_panel();
        framebuffer_draw_launcher_panel();
    }
    framebuffer_draw_status_panel(timer_ticks(), pmm_used_pages(), pmm_free_pages(),
        scheduler_mode_name(scheduler_get_mode()), scheduler_preemption_count());
    framebuffer_draw_task_panel(summary.ready, summary.running, summary.sleeping,
        summary.exited, summary.unused, summary.next_id);
    if (kfs_get_usage(&usage)) {
        framebuffer_draw_storage_panel(1, usage.used_sectors, usage.free_sectors,
            usage.used_data_sectors, usage.data_sectors - usage.used_data_sectors,
            usage.percent_used);
    } else {
        framebuffer_draw_storage_panel(0, 0, 0, 0, 0, 0);
    }
    shell_gfx_restore_console_overlay();
}

static int shell_gfx_set_dashboard_layout(const char* layout) {
    if (string_equals_ci(layout, "compact")) {
        gfx_dashboard_compact = 1;
        return 1;
    }

    if (string_equals_ci(layout, "full")) {
        gfx_dashboard_compact = 0;
        return 1;
    }

    return 0;
}

void shell_system_after_command(void) {
    if (gfx_auto_dashboard && !gfx_desktop_clean && framebuffer_get_info()->hardware_backed) {
        framebuffer_reset_mouse_cursor();
        shell_gfx_refresh_dashboard_panels();
    }

    if (gfx_windows_enabled && framebuffer_get_info()->hardware_backed) {
        if (gfx_desktop_clean) {
            shell_gfx_redraw_windows_desktop();
            return;
        }
        shell_gfx_draw_windows();
    }

    if (gfx_cursor_enabled && framebuffer_get_info()->hardware_backed) {
        shell_gfx_draw_cursor();
    }
}

void shell_system_tick(void) {
    if (!framebuffer_get_info()->hardware_backed) {
        return;
    }

    if (gfx_cursor_enabled && shell_gfx_cursor_needs_redraw()) {
        /* Repainting the cursor must not acknowledge unprocessed input packets. */
        gfx_cursor_last_input_packets = mouse_get_state()->packets;
        shell_gfx_draw_cursor();
        if (gfx_windows_enabled && !gfx_windows_dragging) {
            int hover = shell_gfx_hover_target();
            if (hover != gfx_hover_id) {
                gfx_hover_id = hover;
                framebuffer_erase_mouse_cursor();
                if (gfx_desktop_clean) shell_gfx_draw_desktop_dock();
                shell_gfx_draw_windows();
                shell_gfx_draw_cursor();
            }
        }
        if (shell_gfx_handle_live_windows()) {
            return;
        }
        shell_gfx_handle_live_clicks();
    }

    if (gfx_windows_enabled && framebuffer_console_needs_redraw() &&
            shell_gfx_window_type_visible(SHELL_GFX_WINDOW_SHELL)) {
        shell_gfx_refresh_window_type(SHELL_GFX_WINDOW_SHELL);
    }
    shell_gfx_refresh_live_windows();
}

void shell_system_initialize_desktop(void) {
    if (!framebuffer_get_info()->hardware_backed) {
        return;
    }
    gfx_desktop_clean = 1;
    gfx_windows_enabled = 1;
    gfx_cursor_enabled = 1;
    framebuffer_console_set_deferred(1);
    shell_gfx_reset_windows();
    shell_gfx_center_cursor();
    shell_gfx_redraw_windows_desktop();
}

int shell_system_accepts_keyboard(void) {
    return !gfx_desktop_clean || !gfx_windows_enabled ||
        (gfx_windows_active >= 0 && gfx_windows[gfx_windows_active].visible &&
         gfx_windows[gfx_windows_active].type == SHELL_GFX_WINDOW_SHELL);
}

static int shell_gfx_scene_command(const struct shell_line* line, int* last_status, size_t command_index) {
    if (line->count <= command_index) {
        terminal_write("gfx scene: usage gfx scene <desktop|test|clear>\n");
        *last_status = 1;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "desktop")) {
        framebuffer_demo_desktop();
        shell_gfx_restore_console_overlay();
        terminal_write("gfx: scene desktop drawn\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "test")) {
        framebuffer_test_pattern();
        shell_gfx_restore_console_overlay();
        terminal_write("gfx: scene test drawn\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "clear")) {
        framebuffer_clear(0x00000000);
        shell_gfx_restore_console_overlay();
        terminal_write("gfx: scene cleared\n");
        *last_status = 0;
        return 1;
    }

    terminal_write("gfx scene: usage gfx scene <desktop|test|clear>\n");
    *last_status = 1;
    return 1;
}

static int shell_gfx_shell_command(const struct shell_line* line, int* last_status, size_t command_index) {
    if (line->count <= command_index ||
            string_equals_ci(line->args[command_index], "status")) {
        if (framebuffer_get_info()->hardware_backed && !terminal_graphics_mirror_enabled()) {
            framebuffer_console_reset();
            terminal_set_graphics_mirror(1);
        }
        terminal_write("gfx shell: ");
        terminal_write(terminal_graphics_mirror_enabled() ? "on\n" : "off\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "on")) {
        framebuffer_console_reset();
        terminal_set_graphics_mirror(1);
        terminal_write("gfx: shell overlay on\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "off")) {
        if (framebuffer_get_info()->hardware_backed) {
            if (!terminal_graphics_mirror_enabled()) {
                framebuffer_console_reset();
                terminal_set_graphics_mirror(1);
            }
            terminal_write("gfx: shell overlay stays on in hardware graphics mode\n");
            *last_status = 0;
            return 1;
        }

        terminal_set_graphics_mirror(0);
        terminal_write("gfx: shell overlay off\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "clear")) {
        int mirror_was_enabled = terminal_graphics_mirror_enabled();
        terminal_set_graphics_mirror(0);
        framebuffer_console_reset();
        if (mirror_was_enabled) {
            terminal_set_graphics_mirror(1);
        }
        terminal_write("gfx: shell overlay cleared\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[command_index], "demo")) {
        framebuffer_demo_console();
        terminal_write("gfx: shell demo drawn\n");
        *last_status = 0;
        return 1;
    }

    terminal_write("gfx shell: usage gfx shell <on|off|status|clear|demo>\n");
    *last_status = 1;
    return 1;
}

int shell_system_handle_line(const struct shell_line* line, int* last_status) {
    if (string_equals(line->args[0], "sched")) {
        if (line->count == 1) {
            scheduler_print_status();
            *last_status = 0;
        } else if (line->count == 2 && string_equals(line->args[1], "cooperative")) {
            scheduler_set_mode(SCHEDULER_COOPERATIVE);
            terminal_write("Scheduler mode: cooperative\n");
            *last_status = 0;
        } else if (line->count == 2 && string_equals(line->args[1], "auto")) {
            scheduler_set_mode(SCHEDULER_AUTO);
            terminal_write("Scheduler mode: auto (irq0 user-mode preemption)\n");
            *last_status = 0;
        } else if (line->count == 2 && string_equals(line->args[1], "reset")) {
            scheduler_reset_stats();
            terminal_write("Scheduler stats reset\n");
            *last_status = 0;
        } else {
            terminal_write("sched: usage sched [cooperative|auto|reset]\n");
            *last_status = 1;
        }
        return 1;
    }

    if (string_equals(line->args[0], "ticks")) {
        terminal_write("Timer ticks: ");
        terminal_write_dec(timer_ticks());
        terminal_write("\n");
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "mouse")) {
        mouse_print_status();
        *last_status = 0;
        return 1;
    }

    if (string_equals_ci(line->args[0], "gfx")) {
        if (line->count == 1 || (line->count == 2 && string_equals_ci(line->args[1], "info"))) {
            framebuffer_print_info();
            if (framebuffer_get_info()->hardware_backed) {
                terminal_write("  note: hardware framebuffer is active\n");
            } else {
                terminal_write("  note: framebuffer is a RAM stub until VBE boot support lands\n");
            }
            *last_status = 0;
        } else if (line->count == 2 && string_equals_ci(line->args[1], "status")) {
            framebuffer_draw_status_panel(timer_ticks(), pmm_used_pages(), pmm_free_pages(),
                scheduler_mode_name(scheduler_get_mode()), scheduler_preemption_count());
            shell_gfx_restore_console_overlay();
            terminal_write("gfx: status panel drawn\n");
            *last_status = 0;
        } else if (line->count == 2 && string_equals_ci(line->args[1], "apps")) {
            shell_gfx_draw_apps_panel();
            shell_gfx_restore_console_overlay();
            terminal_write("gfx: apps panel drawn\n");
            *last_status = 0;
        } else if (line->count == 2 && string_equals_ci(line->args[1], "launcher")) {
            framebuffer_draw_launcher_panel();
            shell_gfx_restore_console_overlay();
            terminal_write("gfx: launcher panel drawn\n");
            *last_status = 0;
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "files")) {
            char path[SHELL_FS_PATH_SIZE];
            if (line->count >= 3) {
                shell_fs_resolve_path(line->args[2], path, sizeof(path));
            } else {
                shell_gfx_copy_limited(path, sizeof(path), shell_fs_current_directory());
            }

            if (shell_gfx_draw_files_panel_for_path(path)) {
                terminal_write("gfx: files panel drawn\n");
                *last_status = 0;
            } else {
                terminal_write("gfx files: directory not found\n");
                *last_status = 1;
            }
            shell_gfx_restore_console_overlay();
        } else if (line->count == 2 && string_equals_ci(line->args[1], "storage")) {
            struct kfs_usage usage;
            if (kfs_get_usage(&usage)) {
                framebuffer_draw_storage_panel(1, usage.used_sectors, usage.free_sectors,
                    usage.used_data_sectors, usage.data_sectors - usage.used_data_sectors,
                    usage.percent_used);
                terminal_write("gfx: storage panel drawn\n");
                *last_status = 0;
            } else {
                framebuffer_draw_storage_panel(0, 0, 0, 0, 0, 0);
                terminal_write("gfx: storage panel drawn (disk not formatted)\n");
                *last_status = 1;
            }
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "dashboard")) {
            if (line->count == 3 && !shell_gfx_set_dashboard_layout(line->args[2])) {
                terminal_write("gfx dashboard: usage gfx dashboard [compact|full]\n");
                *last_status = 1;
                return 1;
            } else if (line->count > 3) {
                terminal_write("gfx dashboard: usage gfx dashboard [compact|full]\n");
                *last_status = 1;
                return 1;
            }
            gfx_desktop_clean = 0;
            shell_gfx_draw_dashboard();
            terminal_write(gfx_dashboard_compact ? "gfx: compact dashboard drawn\n" : "gfx: full dashboard drawn\n");
            *last_status = 0;
        } else if (line->count == 2 && string_equals_ci(line->args[1], "desktop")) {
            shell_system_initialize_desktop();
            terminal_write("gfx: clean desktop drawn\n");
            *last_status = 0;
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "auto")) {
            if (line->count == 2 || (line->count == 3 && string_equals_ci(line->args[2], "status"))) {
                terminal_write("gfx auto dashboard: ");
                terminal_write(gfx_auto_dashboard ? "on\n" : "off\n");
                terminal_write("gfx dashboard layout: ");
                terminal_write(gfx_dashboard_compact ? "compact\n" : "full\n");
                *last_status = 0;
            } else if ((line->count == 3 || line->count == 4) && string_equals_ci(line->args[2], "on")) {
                if (line->count == 4 && !shell_gfx_set_dashboard_layout(line->args[3])) {
                    terminal_write("gfx auto: usage gfx auto <on|off|status|compact|full> [compact|full]\n");
                    *last_status = 1;
                    return 1;
                }
                gfx_auto_dashboard = 1;
                terminal_write("gfx: auto dashboard on\n");
                shell_gfx_draw_dashboard();
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "off")) {
                gfx_auto_dashboard = 0;
                terminal_write("gfx: auto dashboard off\n");
                *last_status = 0;
            } else if (line->count == 3 && shell_gfx_set_dashboard_layout(line->args[2])) {
                terminal_write(gfx_dashboard_compact ? "gfx: auto dashboard layout compact\n" : "gfx: auto dashboard layout full\n");
                if (gfx_auto_dashboard) {
                    shell_gfx_draw_dashboard();
                }
                *last_status = 0;
            } else {
                terminal_write("gfx auto: usage gfx auto <on|off|status|compact|full> [compact|full]\n");
                *last_status = 1;
            }
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "windows")) {
            if (line->count == 2 || (line->count == 3 && string_equals_ci(line->args[2], "on"))) {
                gfx_windows_enabled = 1;
                framebuffer_console_set_deferred(1);
                gfx_dashboard_compact = 0;
                gfx_cursor_enabled = 1;
                shell_gfx_redraw_windows_desktop();
                terminal_write("gfx: windows on\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "off")) {
                gfx_windows_enabled = 0;
                gfx_windows_dragging = 0;
                gfx_desktop_clean = 0;
                framebuffer_console_set_deferred(0);
                shell_gfx_draw_dashboard();
                shell_gfx_draw_cursor();
                terminal_write("gfx: windows off\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "status")) {
                shell_gfx_print_windows_status();
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "reset")) {
                gfx_windows_enabled = 1;
                gfx_dashboard_compact = 0;
                gfx_cursor_enabled = 1;
                shell_gfx_reset_windows();
                framebuffer_console_set_deferred(1);
                shell_gfx_redraw_windows_desktop();
                terminal_write("gfx: windows reset\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "arrange")) {
                gfx_windows_enabled = 1;
                gfx_cursor_enabled = 1;
                shell_gfx_arrange_windows();
                framebuffer_console_set_deferred(1);
                shell_gfx_redraw_windows_desktop();
                terminal_write("gfx: windows arranged\n");
                *last_status = 0;
            } else {
                terminal_write("gfx windows: usage gfx windows [on|off|status|reset|arrange]\n");
                *last_status = 1;
            }
        } else if (line->count == 2 && string_equals_ci(line->args[1], "click")) {
            shell_gfx_print_click_status();
            shell_gfx_draw_cursor();
            *last_status = 0;
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "cursor")) {
            if (line->count == 2) {
                shell_gfx_draw_cursor();
                terminal_write("gfx: cursor drawn\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "on")) {
                gfx_cursor_enabled = 1;
                shell_gfx_draw_cursor();
                terminal_write("gfx: cursor on\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "off")) {
                gfx_cursor_enabled = 0;
                framebuffer_erase_mouse_cursor();
                terminal_write("gfx: cursor off\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "status")) {
                shell_gfx_print_cursor_status();
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "center")) {
                shell_gfx_center_cursor();
                shell_gfx_draw_cursor();
                terminal_write("gfx: cursor centered\n");
                *last_status = 0;
            } else {
                terminal_write("gfx cursor: usage gfx cursor [on|off|status|center]\n");
                *last_status = 1;
            }
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "scene")) {
            shell_gfx_scene_command(line, last_status, 2);
        } else if (line->count == 2 && string_equals_ci(line->args[1], "test")) {
            shell_gfx_scene_command(line, last_status, 1);
        } else if (line->count == 2 && string_equals_ci(line->args[1], "desktop")) {
            shell_gfx_scene_command(line, last_status, 1);
        } else if (line->count == 2 && string_equals_ci(line->args[1], "console")) {
            framebuffer_demo_console();
            terminal_write("gfx: drew graphical console mockup into framebuffer stub\n");
            *last_status = 0;
        } else if (line->count > 2 && string_equals_ci(line->args[1], "console")) {
            char text[128];
            shell_join_args(line, 2, text, sizeof(text));
            framebuffer_console_write(text);
            framebuffer_console_write("\n");
            terminal_write("gfx: wrote line into graphical console buffer\n");
            *last_status = 0;
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "shell")) {
            shell_gfx_shell_command(line, last_status, 2);
        } else if (line->count >= 2 && string_equals_ci(line->args[1], "mirror")) {
            if (line->count == 2 || (line->count == 3 && string_equals_ci(line->args[2], "status"))) {
                terminal_write("gfx: mirror ");
                terminal_write(terminal_graphics_mirror_enabled() ? "on\n" : "off\n");
                *last_status = 0;
            } else if (line->count == 3 && string_equals_ci(line->args[2], "on")) {
                shell_gfx_shell_command(line, last_status, 2);
            } else if (line->count == 3 && string_equals_ci(line->args[2], "off")) {
                shell_gfx_shell_command(line, last_status, 2);
            } else if (line->count == 3 && string_equals_ci(line->args[2], "clear")) {
                shell_gfx_shell_command(line, last_status, 2);
            } else {
                shell_gfx_usage();
                *last_status = 1;
            }
        } else if (line->count == 2 && string_equals_ci(line->args[1], "preview")) {
            terminal_ensure_rows(28);
            framebuffer_print_preview();
            *last_status = 0;
        } else if (line->count == 2 && string_equals_ci(line->args[1], "clear")) {
            shell_gfx_scene_command(line, last_status, 1);
        } else {
            shell_gfx_usage();
            *last_status = 1;
        }
        return 1;
    }

    if (string_equals(line->args[0], "mem")) {
        memory_map_print();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "diskinfo")) {
        ata_print_data_disk_info();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "df") || string_equals(line->args[0], "diskfree")) {
        *last_status = kfs_print_usage() ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "diskread")) {
        int ok = 0;

        if (line->count < 2) {
            terminal_write("diskread: usage diskread <lba>\n");
            *last_status = 1;
            return 1;
        }

        uint32_t lba = string_to_uint(line->args[1], &ok);
        if (!ok) {
            terminal_write("diskread: invalid lba\n");
            *last_status = 1;
            return 1;
        }

        *last_status = ata_print_data_sector(lba) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "diskwrite")) {
        int ok = 0;
        char text[128];

        if (line->count < 3) {
            terminal_write("diskwrite: usage diskwrite <lba> <text>\n");
            *last_status = 1;
            return 1;
        }

        uint32_t lba = string_to_uint(line->args[1], &ok);
        if (!ok) {
            terminal_write("diskwrite: invalid lba\n");
            *last_status = 1;
            return 1;
        }

        shell_join_args(line, 2, text, sizeof(text));
        *last_status = ata_write_text_sector(lba, text) ? 0 : 1;
        if (*last_status == 0) {
            terminal_write("Wrote sector ");
            terminal_write_dec(lba);
            terminal_write("\n");
        } else {
            terminal_write("diskwrite: unable to write sector\n");
        }
        return 1;
    }

    if (string_equals(line->args[0], "kfsformat")) {
        *last_status = kfs_format() ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfscheck")) {
        if (line->count > 2 || (line->count == 2 && !string_equals(line->args[1], "-v"))) {
            terminal_write("kfscheck: usage kfscheck [-v]\n");
            *last_status = 1;
            return 1;
        }

        *last_status = kfs_check(line->count == 2) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfsinfo")) {
        *last_status = kfs_print_info() ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfsls")) {
        *last_status = kfs_list() ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfssave")) {
        char text[128];

        if (line->count < 3) {
            terminal_write("kfssave: usage kfssave <name> <text>\n");
            *last_status = 1;
            return 1;
        }

        shell_join_args(line, 2, text, sizeof(text));
        *last_status = kfs_save_text(line->args[1], text) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfscat")) {
        if (line->count < 2) {
            terminal_write("kfscat: usage kfscat <name>\n");
            *last_status = 1;
            return 1;
        }

        *last_status = kfs_cat(line->args[1]) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfsstat")) {
        if (line->count < 2) {
            terminal_write("kfsstat: usage kfsstat <name>\n");
            *last_status = 1;
            return 1;
        }

        *last_status = kfs_stat(line->args[1]) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "kfsrm")) {
        if (line->count < 2) {
            terminal_write("kfsrm: usage kfsrm <name>\n");
            *last_status = 1;
            return 1;
        }

        *last_status = kfs_remove(line->args[1]) ? 0 : 1;
        return 1;
    }

    if (string_equals(line->args[0], "pmm")) {
        pmm_print_stats();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "alloc")) {
        uint32_t address = pmm_alloc_page();

        if (address == 0) {
            terminal_write("PMM allocation failed\n");
            *last_status = 1;
        } else {
            terminal_write("Allocated page: ");
            terminal_write_hex(address);
            terminal_write("\n");
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "heap")) {
        heap_print_stats();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "kmalloc")) {
        void* pointer = kmalloc(64);

        if (pointer == 0) {
            terminal_write("kmalloc failed\n");
            *last_status = 1;
        } else {
            uint8_t* bytes = (uint8_t*)pointer;
            bytes[0] = 0x4B;
            bytes[63] = 0x31;

            terminal_write("kmalloc(64): ");
            terminal_write_hex((uint32_t)pointer);
            terminal_write(" phys=");
            terminal_write_hex(arch_get_physical((uint32_t)pointer));
            terminal_write(" test=");
            terminal_write_hex(bytes[0]);
            terminal_putchar('/');
            terminal_write_hex(bytes[63]);
            terminal_write("\n");
            *last_status = 0;
        }
        return 1;
    }

    if (string_equals(line->args[0], "paging") || string_equals(line->args[0], "gdt")) {
        arch_print_status();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "vmmtest")) {
        arch_test_mapping();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "ring3")) {
        user_mode_enter_test();
        *last_status = 0;
        return 1;
    }

    if (string_equals(line->args[0], "about")) {
        terminal_write("Kernel1: 32-bit educational kernel in ASM + C.\n");
        *last_status = 0;
        return 1;
    }

    return 0;
}
