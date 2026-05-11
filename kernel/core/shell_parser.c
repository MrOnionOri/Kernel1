#include "shell_parser.h"

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

static void string_copy(char* destination, const char* source, size_t size) {
    size_t index = 0;

    if (size == 0) {
        return;
    }

    while (index < size - 1 && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

int shell_parse_line(const char* input, struct shell_line* line) {
    size_t read = 0;
    size_t write = 0;
    int in_quote = 0;
    char quote = '\0';
    int token_active = 0;

    line->count = 0;
    line->redirect = 0;
    line->redirect_append = 0;
    line->redirect_path[0] = '\0';

    while (input[read] != '\0' && write < sizeof(line->storage) - 1) {
        char character = input[read++];

        if (in_quote) {
            if (character == quote) {
                in_quote = 0;
                continue;
            }

            if (!token_active) {
                if (line->count >= SHELL_PARSER_MAX_ARGS) {
                    return 0;
                }

                line->args[line->count++] = &line->storage[write];
                token_active = 1;
            }

            line->storage[write++] = character;
            continue;
        }

        if (character == '"' || character == '\'') {
            in_quote = 1;
            quote = character;
            if (!token_active) {
                if (line->count >= SHELL_PARSER_MAX_ARGS) {
                    return 0;
                }

                line->args[line->count++] = &line->storage[write];
                token_active = 1;
            }
            continue;
        }

        if (character == ' ') {
            if (token_active) {
                line->storage[write++] = '\0';
                token_active = 0;
            }
            continue;
        }

        if (character == '>') {
            if (token_active) {
                line->storage[write++] = '\0';
                token_active = 0;
            }

            if (line->count >= SHELL_PARSER_MAX_ARGS || write >= sizeof(line->storage) - 3) {
                return 0;
            }

            line->args[line->count++] = &line->storage[write];
            line->storage[write++] = '>';
            if (input[read] == '>') {
                line->storage[write++] = '>';
                read++;
            }
            line->storage[write++] = '\0';
            continue;
        }

        if (!token_active) {
            if (line->count >= SHELL_PARSER_MAX_ARGS) {
                return 0;
            }

            line->args[line->count++] = &line->storage[write];
            token_active = 1;
        }

        line->storage[write++] = character;
    }

    if (in_quote) {
        return 0;
    }

    if (token_active && write < sizeof(line->storage) - 1) {
        line->storage[write++] = '\0';
    }

    line->storage[write] = '\0';

    for (size_t i = 0; i < line->count; i++) {
        if (string_equals(line->args[i], ">") || string_equals(line->args[i], ">>")) {
            if (i + 1 >= line->count) {
                return 0;
            }

            line->redirect = 1;
            line->redirect_append = string_equals(line->args[i], ">>");
            string_copy(line->redirect_path, line->args[i + 1], sizeof(line->redirect_path));
            line->count = i;
            return 1;
        }
    }

    return 1;
}

void shell_join_args(const struct shell_line* line, size_t start, char* output, size_t size) {
    size_t index = 0;

    if (size == 0) {
        return;
    }

    for (size_t arg = start; arg < line->count; arg++) {
        size_t source = 0;

        if (arg > start && index < size - 1) {
            output[index++] = ' ';
        }

        while (index < size - 1 && line->args[arg][source] != '\0') {
            output[index++] = line->args[arg][source++];
        }
    }

    output[index] = '\0';
}
