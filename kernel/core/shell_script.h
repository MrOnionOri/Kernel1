#ifndef KERNEL_SHELL_SCRIPT_H
#define KERNEL_SHELL_SCRIPT_H

#include "shell_env.h"
#include "shell_parser.h"

#include <stddef.h>

typedef void (*shell_script_run_command_fn)(const char* command);
typedef void (*shell_script_resolve_path_fn)(const char* input, char* output, size_t size);

void shell_script_initialize(void);
char (*shell_script_args(void))[SHELL_SCRIPT_ARG_SIZE];
int shell_script_handle_foreach(const char* input, int* last_status,
    shell_script_run_command_fn run_command);
int shell_script_handle_control_line(const struct shell_line* line, int* last_status,
    shell_script_run_command_fn run_command, shell_script_resolve_path_fn resolve_path);

#endif
