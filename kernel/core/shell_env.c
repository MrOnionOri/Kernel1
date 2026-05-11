#include "shell_env.h"

#include "terminal.h"

#define SHELL_MAX_VARS 8
#define SHELL_MAX_ALIASES 8

struct shell_var {
    int used;
    char name[SHELL_VAR_NAME_SIZE];
    char value[SHELL_VAR_VALUE_SIZE];
};

struct shell_alias {
    int used;
    char name[SHELL_ALIAS_NAME_SIZE];
    char value[SHELL_ALIAS_VALUE_SIZE];
};

static struct shell_var shell_vars[SHELL_MAX_VARS];
static struct shell_alias shell_aliases[SHELL_MAX_ALIASES];

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

static void string_append_char(char* destination, size_t size, size_t* index, char character) {
    if (*index < size - 1) {
        destination[*index] = character;
        (*index)++;
        destination[*index] = '\0';
    }
}

static void string_append(char* destination, size_t size, size_t* index, const char* source) {
    size_t source_index = 0;

    while (source[source_index] != '\0' && *index < size - 1) {
        destination[*index] = source[source_index++];
        (*index)++;
    }

    destination[*index] = '\0';
}

static void string_append_dec(char* destination, size_t size, size_t* index, uint32_t value) {
    char digits[11];
    size_t digit_count = 0;

    if (value == 0) {
        string_append_char(destination, size, index, '0');
        return;
    }

    while (value > 0 && digit_count < sizeof(digits)) {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (digit_count > 0) {
        string_append_char(destination, size, index, digits[--digit_count]);
    }
}

static const char* skip_spaces(const char* text) {
    while (*text == ' ') {
        text++;
    }

    return text;
}

static int shell_env_is_name_char(char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') ||
        character == '_';
}

void shell_env_initialize(void) {
    for (size_t i = 0; i < SHELL_MAX_VARS; i++) {
        shell_vars[i].used = 0;
        shell_vars[i].name[0] = '\0';
        shell_vars[i].value[0] = '\0';
    }

    for (size_t i = 0; i < SHELL_MAX_ALIASES; i++) {
        shell_aliases[i].used = 0;
        shell_aliases[i].name[0] = '\0';
        shell_aliases[i].value[0] = '\0';
    }

    shell_env_set_var("HOME", "/");
    shell_env_set_var("PATH", "apps");
    shell_env_set_alias("ll", "ls");
    shell_env_set_alias("la", "ls apps");
}

int shell_env_name_is_valid(const char* name) {
    if (name[0] == '\0' || (name[0] >= '0' && name[0] <= '9')) {
        return 0;
    }

    for (size_t i = 0; name[i] != '\0'; i++) {
        if (!shell_env_is_name_char(name[i])) {
            return 0;
        }
    }

    return 1;
}

int shell_env_find_var(const char* name) {
    for (size_t i = 0; i < SHELL_MAX_VARS; i++) {
        if (shell_vars[i].used && string_equals(shell_vars[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

const char* shell_env_get_var(const char* name) {
    int index = shell_env_find_var(name);

    if (index < 0) {
        return "";
    }

    return shell_vars[index].value;
}

int shell_env_set_var(const char* name, const char* value) {
    int index;

    if (!shell_env_name_is_valid(name)) {
        terminal_write("set: invalid name\n");
        return 0;
    }

    index = shell_env_find_var(name);

    if (index < 0) {
        for (size_t i = 0; i < SHELL_MAX_VARS; i++) {
            if (!shell_vars[i].used) {
                index = (int)i;
                shell_vars[i].used = 1;
                string_copy(shell_vars[i].name, name, sizeof(shell_vars[i].name));
                break;
            }
        }
    }

    if (index < 0) {
        terminal_write("set: variable table full\n");
        return 0;
    }

    string_copy(shell_vars[index].value, value, sizeof(shell_vars[index].value));
    return 1;
}

int shell_env_unset_var(const char* name) {
    int index = shell_env_find_var(name);

    if (index < 0) {
        return 0;
    }

    shell_vars[index].used = 0;
    shell_vars[index].name[0] = '\0';
    shell_vars[index].value[0] = '\0';
    return 1;
}

void shell_env_print_vars(void) {
    for (size_t i = 0; i < SHELL_MAX_VARS; i++) {
        if (!shell_vars[i].used) {
            continue;
        }

        terminal_write(shell_vars[i].name);
        terminal_write("=");
        terminal_write(shell_vars[i].value);
        terminal_write("\n");
    }
}

void shell_env_expand_variables(const char* input, char* output, size_t size,
    int last_status, char script_args[SHELL_SCRIPT_ARG_COUNT][SHELL_SCRIPT_ARG_SIZE]) {
    size_t read = 0;
    size_t write = 0;
    int in_single_quote = 0;
    int in_double_quote = 0;

    if (size == 0) {
        return;
    }

    output[0] = '\0';

    while (input[read] != '\0' && write < size - 1) {
        if (input[read] == '\\' && input[read + 1] != '\0') {
            read++;
            string_append_char(output, size, &write, input[read++]);
            continue;
        }

        if (input[read] == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            string_append_char(output, size, &write, input[read++]);
            continue;
        }

        if (input[read] == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            string_append_char(output, size, &write, input[read++]);
            continue;
        }

        if (in_single_quote || input[read] != '$') {
            string_append_char(output, size, &write, input[read++]);
            continue;
        }

        read++;

        if (input[read] == '?') {
            string_append_dec(output, size, &write, (uint32_t)last_status);
            read++;
            continue;
        }

        if (input[read] >= '0' && input[read] <= '4') {
            uint32_t arg_index = (uint32_t)(input[read] - '0');
            string_append(output, size, &write, script_args[arg_index]);
            read++;
            continue;
        }

        if (!shell_env_is_name_char(input[read])) {
            string_append_char(output, size, &write, '$');
            continue;
        }

        char name[SHELL_VAR_NAME_SIZE];
        size_t name_index = 0;

        while (shell_env_is_name_char(input[read]) && name_index < sizeof(name) - 1) {
            name[name_index++] = input[read++];
        }

        while (shell_env_is_name_char(input[read])) {
            read++;
        }

        name[name_index] = '\0';
        string_append(output, size, &write, shell_env_get_var(name));
    }
}

static int shell_find_alias(const char* name) {
    for (size_t i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (shell_aliases[i].used && string_equals(shell_aliases[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

int shell_env_set_alias(const char* name, const char* value) {
    int index;

    if (!shell_env_name_is_valid(name)) {
        terminal_write("alias: invalid name\n");
        return 0;
    }

    if (value[0] == '\0') {
        terminal_write("alias: missing value\n");
        return 0;
    }

    index = shell_find_alias(name);
    if (index < 0) {
        for (size_t i = 0; i < SHELL_MAX_ALIASES; i++) {
            if (!shell_aliases[i].used) {
                index = (int)i;
                shell_aliases[i].used = 1;
                string_copy(shell_aliases[i].name, name, sizeof(shell_aliases[i].name));
                break;
            }
        }
    }

    if (index < 0) {
        terminal_write("alias: table full\n");
        return 0;
    }

    string_copy(shell_aliases[index].value, value, sizeof(shell_aliases[index].value));
    return 1;
}

int shell_env_unset_alias(const char* name) {
    int index = shell_find_alias(name);

    if (index < 0) {
        return 0;
    }

    shell_aliases[index].used = 0;
    shell_aliases[index].name[0] = '\0';
    shell_aliases[index].value[0] = '\0';
    return 1;
}

void shell_env_print_aliases(void) {
    for (size_t i = 0; i < SHELL_MAX_ALIASES; i++) {
        if (!shell_aliases[i].used) {
            continue;
        }

        terminal_write(shell_aliases[i].name);
        terminal_write("=");
        terminal_write(shell_aliases[i].value);
        terminal_write("\n");
    }
}

void shell_env_expand_alias(const char* input, char* output, size_t size) {
    size_t read = 0;
    size_t write = 0;
    char name[SHELL_ALIAS_NAME_SIZE];
    size_t name_index = 0;

    if (size == 0) {
        return;
    }

    output[0] = '\0';

    while (input[read] == ' ') {
        read++;
    }

    while (input[read] != '\0' && input[read] != ' ' && name_index < sizeof(name) - 1) {
        name[name_index++] = input[read++];
    }

    while (input[read] != '\0' && input[read] != ' ') {
        read++;
    }

    name[name_index] = '\0';

    int alias_index = shell_find_alias(name);
    if (name[0] == '\0' || alias_index < 0) {
        string_copy(output, input, size);
        return;
    }

    string_append(output, size, &write, shell_aliases[alias_index].value);
    if (input[read] != '\0') {
        string_append_char(output, size, &write, ' ');
        string_append(output, size, &write, skip_spaces(input + read));
    }
}
