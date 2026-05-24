#include "kapp.h"

#include "arch.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"
#include "vfs.h"

#include <stdint.h>

#define KAPP_V0_HEADER_SIZE 20
#define KAPP_V1_HEADER_SIZE 64
#define KAPP_NAME_SIZE 32
#define KAPP_USER_BASE 0x03000000
#define KAPP_USER_STRIDE 0x00010000
#define KAPP_MAX_IMAGE_SIZE 0x00008000
#define KAPP_MAX_FILE_SIZE (KAPP_V1_HEADER_SIZE + KAPP_MAX_IMAGE_SIZE)
#define USER_STACK_BASE 0x02000000
#define USER_STACK_STRIDE 0x4000
#define PAGE_SIZE 4096

static char kapp_file_buffer[KAPP_MAX_FILE_SIZE];

struct kapp_metadata {
    uint32_t file_size;
    uint32_t header_size;
    uint32_t entry_offset;
    uint32_t image_size;
    uint32_t flags;
    uint32_t version;
    uint32_t required_syscalls;
    char name[KAPP_NAME_SIZE];
};

static uint32_t read_u32(const char* data) {
    return (uint32_t)(uint8_t)data[0] |
        ((uint32_t)(uint8_t)data[1] << 8) |
        ((uint32_t)(uint8_t)data[2] << 16) |
        ((uint32_t)(uint8_t)data[3] << 24);
}

static void string_copy(char* destination, const char* source, uint32_t size) {
    uint32_t index = 0;

    if (size == 0) {
        return;
    }

    while (index < size - 1 && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static void build_kapp_path(const char* name, char* path, uint32_t size) {
    const char prefix[] = "apps/";
    const char suffix[] = ".kapp";
    uint32_t index = 0;
    uint32_t source = 0;

    while (prefix[source] != '\0' && index < size - 1) {
        path[index++] = prefix[source++];
    }

    source = 0;
    while (name[source] != '\0' && index < size - 1) {
        path[index++] = name[source++];
    }

    source = 0;
    while (suffix[source] != '\0' && index < size - 1) {
        path[index++] = suffix[source++];
    }

    path[index] = '\0';
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

static int read_file(const char* path, char* buffer, uint32_t capacity,
        uint32_t* size_out) {
    uint32_t total = 0;
    int fd = vfs_open(path);

    if (fd == VFS_INVALID_FD) {
        return 0;
    }

    while (total < capacity) {
        int32_t bytes = vfs_read(fd, buffer + total, capacity - total);
        if (bytes < 0) {
            vfs_close(fd);
            return 0;
        }

        if (bytes == 0) {
            break;
        }

        total += (uint32_t)bytes;
    }

    char extra;
    int32_t overflow = vfs_read(fd, &extra, 1);
    vfs_close(fd);

    if (overflow != 0) {
        return 0;
    }

    *size_out = total;
    return 1;
}

static int kapp_parse_data(const char* data, uint32_t file_size,
        struct kapp_metadata* metadata) {
    if (file_size < KAPP_V0_HEADER_SIZE ||
            data[0] != 'K' || data[1] != 'A' ||
            data[2] != 'P' || data[3] != 'P') {
        return 0;
    }

    metadata->file_size = file_size;
    metadata->header_size = read_u32(data + 4);
    metadata->entry_offset = read_u32(data + 8);
    metadata->image_size = read_u32(data + 12);
    metadata->flags = read_u32(data + 16);
    metadata->version = 0;
    metadata->required_syscalls = 0;
    metadata->name[0] = '\0';

    if (metadata->header_size >= KAPP_V1_HEADER_SIZE) {
        metadata->version = read_u32(data + 20);
        metadata->required_syscalls = read_u32(data + 24);

        for (uint32_t i = 0; i < KAPP_NAME_SIZE - 1; i++) {
            metadata->name[i] = data[28 + i];
            if (metadata->name[i] == '\0') {
                break;
            }
        }

        metadata->name[KAPP_NAME_SIZE - 1] = '\0';
    }

    return metadata->header_size >= KAPP_V0_HEADER_SIZE &&
        metadata->header_size + metadata->image_size <= file_size &&
        metadata->entry_offset < metadata->image_size &&
        metadata->image_size <= KAPP_MAX_IMAGE_SIZE;
}

static int kapp_load(const char* path, struct kapp_metadata* metadata) {
    uint32_t file_size = 0;

    if (!read_file(path, kapp_file_buffer, sizeof(kapp_file_buffer), &file_size)) {
        return 0;
    }

    return kapp_parse_data(kapp_file_buffer, file_size, metadata);
}

static int map_user_stack(uint32_t stack_top) {
    uint32_t stack_base = stack_top - USER_STACK_STRIDE;

    for (uint32_t offset = 0; offset < USER_STACK_STRIDE; offset += PAGE_SIZE) {
        uint32_t physical = pmm_alloc_page();

        if (physical == 0) {
            return 0;
        }

        if (!arch_map_page(stack_base + offset, physical,
                ARCH_PAGE_WRITABLE | ARCH_PAGE_USER)) {
            return 0;
        }
    }

    return 1;
}

void kapp_inspect(const char* path) {
    struct kapp_metadata metadata;

    if (!kapp_load(path, &metadata)) {
        terminal_write("Invalid KAPP: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    terminal_write("KAPP ");
    terminal_write(path);
    terminal_write("\n  header=");
    terminal_write_dec(metadata.header_size);
    terminal_write(" entry=");
    terminal_write_hex(metadata.entry_offset);
    terminal_write("\n  image=");
    terminal_write_dec(metadata.image_size);
    terminal_write(" file=");
    terminal_write_dec(metadata.file_size);
    terminal_write(" flags=");
    terminal_write_hex(metadata.flags);
    terminal_write("\n");
    terminal_write("  version=");
    terminal_write_dec(metadata.version);
    terminal_write(" syscalls=");
    terminal_write_hex(metadata.required_syscalls);
    if (metadata.name[0] != '\0') {
        terminal_write(" name=");
        terminal_write(metadata.name);
    }
    terminal_write("\n");

    terminal_write("  status=loadable candidate\n");
}

struct task* kapp_spawn_path(const char* path, const char* args) {
    char task_name[KAPP_NAME_SIZE];
    struct kapp_metadata metadata;

    if (!kapp_load(path, &metadata)) {
        return 0;
    }

    string_copy(task_name, metadata.name[0] != '\0' ? metadata.name : path_basename(path),
        sizeof(task_name));

    uint32_t task_id = task_next_id();
    uint32_t load_base = KAPP_USER_BASE + (task_id * KAPP_USER_STRIDE);
    uint32_t stack_top = USER_STACK_BASE + (task_id * USER_STACK_STRIDE);
    uint32_t pages = (metadata.image_size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (!map_user_stack(stack_top)) {
        terminal_write("kapp spawn failed: stack map failed\n");
        return 0;
    }

    for (uint32_t page = 0; page < pages; page++) {
        uint32_t physical = pmm_alloc_page();

        if (physical == 0) {
            terminal_write("kapp spawn failed: no memory\n");
            return 0;
        }

        if (!arch_map_page(load_base + (page * PAGE_SIZE), physical,
                ARCH_PAGE_WRITABLE | ARCH_PAGE_USER)) {
            terminal_write("kapp spawn failed: map failed\n");
            return 0;
        }
    }

    const char* payload = kapp_file_buffer + metadata.header_size;
    char* destination = (char*)load_base;

    for (uint32_t i = 0; i < metadata.image_size; i++) {
        destination[i] = payload[i];
    }

    struct task* task = task_create_user_with_args(task_name,
        load_base + metadata.entry_offset, stack_top, args);

    if (task == 0) {
        terminal_write("kapp spawn failed: task slot\n");
        return 0;
    }

    terminal_write("Spawned ");
    terminal_write(task_name);
    terminal_write(" KAPP task ");
    terminal_write_dec(task->id);
    terminal_write("\n");
    return task;
}

struct task* kapp_spawn_app(const char* name, const char* args) {
    char path[48];

    build_kapp_path(name, path, sizeof(path));
    return kapp_spawn_path(path, args);
}

int kapp_exists(const char* name) {
    char path[48];
    struct kapp_metadata metadata;

    build_kapp_path(name, path, sizeof(path));
    return kapp_load(path, &metadata);
}
