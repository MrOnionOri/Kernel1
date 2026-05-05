#ifndef KERNEL_INITRD_H
#define KERNEL_INITRD_H

#include <stdint.h>

struct initrd_file {
    const char* name;
    const char* data;
    uint32_t size;
};

int initrd_find(const char* name, struct initrd_file* file);
void initrd_for_each(void (*callback)(const struct initrd_file* file, void* context), void* context);
void initrd_print_info(void);
void initrd_list(void);
void initrd_cat(const char* name);
void initrd_list_app_metadata(void);
void initrd_cat_app_metadata(const char* app_name);

#endif
