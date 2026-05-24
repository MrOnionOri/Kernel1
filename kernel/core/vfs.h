#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>

#define VFS_INVALID_FD (-1)
#define VFS_O_READ 0x01
#define VFS_O_WRITE 0x02
#define VFS_O_CREATE 0x04
#define VFS_O_TRUNC 0x08
#define VFS_O_APPEND 0x10

int vfs_open(const char* path);
int vfs_open_flags(const char* path, uint32_t flags);
int32_t vfs_read(int fd, char* buffer, uint32_t size);
int32_t vfs_write(int fd, const char* buffer, uint32_t size);
void vfs_close(int fd);
int vfs_complete_path(const char* prefix, char* output, uint32_t size);
int vfs_is_directory(const char* path);
int vfs_mkdir(const char* path);
int vfs_write_text(const char* path, const char* text);
int vfs_append_text(const char* path, const char* text);
int vfs_copy(const char* source_path, const char* destination_path);
int vfs_move(const char* source_path, const char* destination_path);
int vfs_remove(const char* path);
int vfs_remove_recursive(const char* path);
void vfs_list(void);
void vfs_list_path(const char* path);
void vfs_cat(const char* path);
void vfs_du(const char* path);
void vfs_find(const char* path, const char* pattern);
void vfs_stat(const char* path);
void vfs_tree(const char* path);

#endif
