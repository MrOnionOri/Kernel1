#ifndef KERNEL_SHELL_PARSER_H
#define KERNEL_SHELL_PARSER_H

#include <stddef.h>

#define SHELL_PARSER_BUFFER_SIZE 80
#define SHELL_PARSER_MAX_ARGS 12
#define SHELL_PARSER_PATH_SIZE 64

struct shell_line {
    char storage[SHELL_PARSER_BUFFER_SIZE];
    const char* args[SHELL_PARSER_MAX_ARGS];
    size_t count;
    int redirect;
    int redirect_append;
    char redirect_path[SHELL_PARSER_PATH_SIZE];
};

int shell_parse_line(const char* input, struct shell_line* line);
void shell_join_args(const struct shell_line* line, size_t start, char* output, size_t size);

#endif
