#ifndef KERNEL_KFS_H
#define KERNEL_KFS_H

int kfs_format(void);
int kfs_print_info(void);
int kfs_list(void);
int kfs_save_text(const char* name, const char* text);
int kfs_cat(const char* name);
int kfs_stat(const char* name);
int kfs_remove(const char* name);

#endif
