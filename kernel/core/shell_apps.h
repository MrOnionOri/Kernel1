#ifndef KERNEL_SHELL_APPS_H
#define KERNEL_SHELL_APPS_H

#include "shell_parser.h"

#include <stddef.h>

int shell_apps_handle_line(const struct shell_line* line, int* last_status);
int shell_apps_complete(const char* command_buffer, char* output, size_t size);

#endif
