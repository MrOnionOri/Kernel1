#ifndef KERNEL_APP_H
#define KERNEL_APP_H

#include <stdint.h>

struct app_descriptor {
    const char* name;
    uint32_t entry;
};

void app_print_all(void);
const struct app_descriptor* app_find(const char* name);
const struct app_descriptor* app_find_prefix(const char* prefix);

#endif
