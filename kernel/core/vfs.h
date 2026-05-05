#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>

#define VFS_INVALID_FD (-1)

int vfs_open(const char* path);
int32_t vfs_read(int fd, char* buffer, uint32_t size);
void vfs_close(int fd);
int vfs_complete_path(const char* prefix, char* output, uint32_t size);
void vfs_list(void);
void vfs_list_path(const char* path);
void vfs_cat(const char* path);

#endif
