#ifndef KERNEL_APP_H
#define KERNEL_APP_H

#include <stdint.h>

struct app_descriptor {
    const char* name;
    uint32_t entry;
};

enum app_kind {
    APP_KIND_UNKNOWN = 0,
    APP_KIND_BUILT_IN,
    APP_KIND_KAPP,
};

void app_print_all(void);
void app_print_info(const char* name);
void app_print_source(const char* name);
enum app_kind app_manifest_kind(const char* name);
const struct app_descriptor* app_find(const char* name);
const struct app_descriptor* app_find_prefix(const char* prefix);

#endif
