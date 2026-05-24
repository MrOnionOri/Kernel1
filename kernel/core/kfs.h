#ifndef KERNEL_KFS_H
#define KERNEL_KFS_H

#include <stdint.h>

typedef void (*kfs_visit_callback)(const char* name, uint32_t size,
    uint32_t data_lba, int is_directory, void* context);

int kfs_format(void);
int kfs_check(int verbose);
int kfs_print_usage(void);
int kfs_print_info(void);
int kfs_list(void);
int kfs_save_data(const char* name, const char* data, uint32_t size);
int kfs_save_text(const char* name, const char* text);
int kfs_append_text(const char* name, const char* text);
int kfs_read_text(const char* name, char* output, uint32_t output_size,
    uint32_t* size_out);
uint32_t kfs_allocated_bytes(uint32_t size);
int kfs_mkdir(const char* name);
int kfs_is_directory(const char* name);
int kfs_cat(const char* name);
int kfs_stat(const char* name);
int kfs_remove(const char* name);
int kfs_for_each(kfs_visit_callback callback, void* context);

#endif
