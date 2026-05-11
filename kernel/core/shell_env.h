#ifndef KERNEL_SHELL_ENV_H
#define KERNEL_SHELL_ENV_H

#include <stddef.h>
#include <stdint.h>

#define SHELL_SCRIPT_ARG_COUNT 5
#define SHELL_SCRIPT_ARG_SIZE 32
#define SHELL_VAR_NAME_SIZE 16
#define SHELL_ALIAS_NAME_SIZE 16
#define SHELL_ALIAS_VALUE_SIZE 64
#define SHELL_VAR_VALUE_SIZE 48

void shell_env_initialize(void);
int shell_env_name_is_valid(const char* name);
int shell_env_find_var(const char* name);
const char* shell_env_get_var(const char* name);
int shell_env_set_var(const char* name, const char* value);
int shell_env_unset_var(const char* name);
void shell_env_print_vars(void);
void shell_env_expand_variables(const char* input, char* output, size_t size,
    int last_status, char script_args[SHELL_SCRIPT_ARG_COUNT][SHELL_SCRIPT_ARG_SIZE]);

int shell_env_set_alias(const char* name, const char* value);
int shell_env_unset_alias(const char* name);
void shell_env_print_aliases(void);
void shell_env_expand_alias(const char* input, char* output, size_t size);

#endif
