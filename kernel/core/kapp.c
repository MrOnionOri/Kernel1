#include "kapp.h"

#include "arch.h"
#include "initrd.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"

#include <stdint.h>

#define KAPP_HEADER_SIZE 20
#define KAPP_USER_BASE 0x03000000
#define KAPP_USER_STRIDE 0x00010000
#define KAPP_MAX_IMAGE_SIZE 0x00008000
#define USER_STACK_BASE 0x02000000
#define USER_STACK_STRIDE 0x4000
#define PAGE_SIZE 4096

static uint32_t read_u32(const char* data) {
    return (uint32_t)(uint8_t)data[0] |
        ((uint32_t)(uint8_t)data[1] << 8) |
        ((uint32_t)(uint8_t)data[2] << 16) |
        ((uint32_t)(uint8_t)data[3] << 24);
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

static int kapp_parse(const char* path, struct initrd_file* file, uint32_t* header_size,
        uint32_t* entry_offset, uint32_t* image_size, uint32_t* flags) {
    if (!initrd_find(path, file)) {
        return 0;
    }

    if (file->size < KAPP_HEADER_SIZE ||
            file->data[0] != 'K' || file->data[1] != 'A' ||
            file->data[2] != 'P' || file->data[3] != 'P') {
        return 0;
    }

    *header_size = read_u32(file->data + 4);
    *entry_offset = read_u32(file->data + 8);
    *image_size = read_u32(file->data + 12);
    *flags = read_u32(file->data + 16);

    return *header_size >= KAPP_HEADER_SIZE &&
        *header_size + *image_size <= file->size &&
        *entry_offset < *image_size &&
        *image_size <= KAPP_MAX_IMAGE_SIZE;
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
    struct initrd_file file;
    uint32_t header_size;
    uint32_t entry_offset;
    uint32_t image_size;
    uint32_t flags;

    if (!kapp_parse(path, &file, &header_size, &entry_offset, &image_size, &flags)) {
        terminal_write("Invalid KAPP: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    terminal_write("KAPP ");
    terminal_write(path);
    terminal_write("\n  header=");
    terminal_write_dec(header_size);
    terminal_write(" entry=");
    terminal_write_hex(entry_offset);
    terminal_write("\n  image=");
    terminal_write_dec(image_size);
    terminal_write(" file=");
    terminal_write_dec(file.size);
    terminal_write(" flags=");
    terminal_write_hex(flags);
    terminal_write("\n");

    terminal_write("  status=loadable candidate\n");
}

int kapp_spawn_app(const char* name, const char* args) {
    char path[48];
    struct initrd_file file;
    uint32_t header_size;
    uint32_t entry_offset;
    uint32_t image_size;
    uint32_t flags;

    build_kapp_path(name, path, sizeof(path));

    if (!kapp_parse(path, &file, &header_size, &entry_offset, &image_size, &flags)) {
        return 0;
    }

    (void)flags;

    uint32_t task_id = task_next_id();
    uint32_t load_base = KAPP_USER_BASE + (task_id * KAPP_USER_STRIDE);
    uint32_t stack_top = USER_STACK_BASE + (task_id * USER_STACK_STRIDE);
    uint32_t pages = (image_size + PAGE_SIZE - 1) / PAGE_SIZE;

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

    const char* payload = file.data + header_size;
    char* destination = (char*)load_base;

    for (uint32_t i = 0; i < image_size; i++) {
        destination[i] = payload[i];
    }

    struct task* task = task_create_user_with_args(name, load_base + entry_offset,
        stack_top, args);

    if (task == 0) {
        terminal_write("kapp spawn failed: task slot\n");
        return 0;
    }

    terminal_write("Spawned ");
    terminal_write(name);
    terminal_write(" KAPP task ");
    terminal_write_dec(task->id);
    terminal_write("\n");
    return 1;
}
