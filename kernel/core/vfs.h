#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>

#define VFS_INVALID_FD (-1)
#define VFS_O_READ 0x01
#define VFS_O_WRITE 0x02
#define VFS_O_CREATE 0x04
#define VFS_O_TRUNC 0x08
#define VFS_O_APPEND 0x10
#define VFS_NAME_SIZE 64

enum vfs_node_type {
    VFS_NODE_NONE = 0,
    VFS_NODE_FILE = 1,
    VFS_NODE_DIRECTORY = 2,
};

enum vfs_node_source {
    VFS_SOURCE_NONE = 0,
    VFS_SOURCE_INITRD = 1,
    VFS_SOURCE_RAM = 2,
    VFS_SOURCE_KFS = 3,
    VFS_SOURCE_VFS = 4,
};

struct vfs_stat_info {
    uint32_t type;
    uint32_t size;
    uint32_t allocated_size;
    uint32_t writable;
    uint32_t source;
    uint32_t children;
};

struct vfs_dir_entry {
    char name[VFS_NAME_SIZE];
    uint32_t type;
    uint32_t size;
    uint32_t source;
};

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
int vfs_stat_info(const char* path, struct vfs_stat_info* info);
int vfs_read_dir(const char* path, uint32_t index, struct vfs_dir_entry* entry);
void vfs_list(void);
void vfs_list_path(const char* path);
void vfs_cat(const char* path);
void vfs_du(const char* path);
void vfs_find(const char* path, const char* pattern);
void vfs_stat(const char* path);
void vfs_tree(const char* path);

#endif
