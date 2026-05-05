#include "vfs.h"

#include "initrd.h"
#include "terminal.h"

#define VFS_MAX_OPEN_FILES 8
#define VFS_CAT_BUFFER_SIZE 64

struct vfs_handle {
    int used;
    const char* data;
    uint32_t size;
    uint32_t offset;
};

static struct vfs_handle handles[VFS_MAX_OPEN_FILES];

struct vfs_list_context {
    const char* path;
    int printed_apps;
    int printed_any;
};

static int string_equals(const char* left, const char* right) {
    uint32_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static int string_starts_with(const char* text, const char* prefix) {
    uint32_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }

        index++;
    }

    return 1;
}

static const char* path_basename(const char* path) {
    const char* last = path;

    for (uint32_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            last = path + i + 1;
        }
    }

    return last;
}

static void vfs_print_root_entry(const struct initrd_file* file, void* context) {
    struct vfs_list_context* list = (struct vfs_list_context*)context;

    if (string_starts_with(file->name, "apps/")) {
        if (!list->printed_apps) {
            terminal_write("  apps/\n");
            list->printed_apps = 1;
            list->printed_any = 1;
        }

        return;
    }

    terminal_write("  ");
    terminal_write(file->name);
    terminal_write("  ");
    terminal_write_dec(file->size);
    terminal_write(" bytes\n");
    list->printed_any = 1;
}

static void vfs_print_apps_entry(const struct initrd_file* file, void* context) {
    struct vfs_list_context* list = (struct vfs_list_context*)context;

    if (!string_starts_with(file->name, "apps/")) {
        return;
    }

    terminal_write("  ");
    terminal_write(path_basename(file->name));
    terminal_write("  ");
    terminal_write_dec(file->size);
    terminal_write(" bytes\n");
    list->printed_any = 1;
}

int vfs_open(const char* path) {
    struct initrd_file file;

    if (!initrd_find(path, &file)) {
        return VFS_INVALID_FD;
    }

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!handles[i].used) {
            handles[i].used = 1;
            handles[i].data = file.data;
            handles[i].size = file.size;
            handles[i].offset = 0;
            return (int)i;
        }
    }

    return VFS_INVALID_FD;
}

int32_t vfs_read(int fd, char* buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || buffer == 0 || !handles[fd].used) {
        return -1;
    }

    struct vfs_handle* handle = &handles[fd];
    uint32_t available = handle->size - handle->offset;

    if (available == 0) {
        return 0;
    }

    uint32_t to_read = size < available ? size : available;

    for (uint32_t i = 0; i < to_read; i++) {
        buffer[i] = handle->data[handle->offset + i];
    }

    handle->offset += to_read;
    return (int32_t)to_read;
}

void vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return;
    }

    handles[fd].used = 0;
    handles[fd].data = 0;
    handles[fd].size = 0;
    handles[fd].offset = 0;
}

void vfs_list(void) {
    vfs_list_path("");
}

void vfs_list_path(const char* path) {
    struct vfs_list_context context;
    context.path = path;
    context.printed_apps = 0;
    context.printed_any = 0;

    if (path[0] == '\0' || string_equals(path, "/")) {
        terminal_write("Files /:\n");
        initrd_for_each(vfs_print_root_entry, &context);
    } else if (string_equals(path, "apps") || string_equals(path, "/apps")) {
        terminal_write("Files /apps:\n");
        initrd_for_each(vfs_print_apps_entry, &context);
    } else {
        terminal_write("Directory not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    if (!context.printed_any) {
        terminal_write("  <empty>\n");
    }
}

void vfs_cat(const char* path) {
    char buffer[VFS_CAT_BUFFER_SIZE];
    int fd = vfs_open(path);

    if (fd == VFS_INVALID_FD) {
        terminal_write("File not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    for (;;) {
        int32_t bytes = vfs_read(fd, buffer, sizeof(buffer));

        if (bytes < 0) {
            terminal_write("Read failed: ");
            terminal_write(path);
            terminal_write("\n");
            break;
        }

        if (bytes == 0) {
            break;
        }

        for (int32_t i = 0; i < bytes; i++) {
            terminal_putchar(buffer[i]);
        }
    }

    vfs_close(fd);
    terminal_write("\n");
}
