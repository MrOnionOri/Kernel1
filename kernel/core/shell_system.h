#ifndef KERNEL_SHELL_SYSTEM_H
#define KERNEL_SHELL_SYSTEM_H

#include "shell_parser.h"

int shell_system_handle_line(const struct shell_line* line, int* last_status);
void shell_system_note_app_spawn(const char* name);
void shell_system_after_command(void);
void shell_system_tick(void);
void shell_system_initialize_desktop(void);
int shell_system_accepts_keyboard(void);

#endif
