#ifndef KERNEL_SHELL_FS_H
#define KERNEL_SHELL_FS_H

#include "shell_parser.h"

#include <stddef.h>

#define SHELL_FS_PATH_SIZE 64

void shell_fs_initialize(void);
const char* shell_fs_current_directory(void);
void shell_fs_resolve_path(const char* input, char* output, size_t size);
int shell_fs_write_redirect(const struct shell_line* line, const char* text);
int shell_fs_handle_line(const struct shell_line* line, int* last_status);

#endif
