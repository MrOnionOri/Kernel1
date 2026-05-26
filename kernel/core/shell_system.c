#include "shell_system.h"

#include "arch.h"
#include "ata.h"
#include "framebuffer.h"
#include "heap.h"
#include "kfs.h"
#include "memory_map.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"
#include "timer.h"
#include "user_mode.h"

#include <stdint.h>

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
    terminal_write("gfx: usage gfx [info|status|scene <desktop|test|clear>|shell <on|off|status|clear|demo>|mirror <on|off|status|clear>|preview]\n");
}

static void shell_gfx_restore_console_overlay(void) {
    if (terminal_graphics_mirror_enabled()) {
        framebuffer_console_reset();
    }
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
