#include "vfs.h"

#include "heap.h"
#include "initrd.h"
#include "kfs.h"
#include "terminal.h"

#define VFS_MAX_OPEN_FILES 8
#define VFS_CAT_BUFFER_SIZE 64
#define VFS_KFS_FILE_BUFFER_SIZE 4097
#define RAMFS_MAX_NODES 16
#define RAMFS_MAX_PATH 80
#define RAMFS_MAX_FILE_SIZE 4096
#define VFS_MAX_SEEN_DIRS 8
#define VFS_KFS_RECURSIVE_MAX 32

struct vfs_handle {
    int used;
    const char* data;
    uint32_t size;
    uint32_t offset;
    int readable;
    int writable;
    int owns_data;
    uint32_t flags;
    struct ramfs_node* node;
};

enum ramfs_node_type {
    RAMFS_FILE = 1,
    RAMFS_DIRECTORY = 2,
};

struct ramfs_node {
    int used;
    enum ramfs_node_type type;
    char path[RAMFS_MAX_PATH];
    char* data;
    uint32_t size;
    uint32_t capacity;
};

static struct vfs_handle handles[VFS_MAX_OPEN_FILES];
static struct ramfs_node ramfs_nodes[RAMFS_MAX_NODES];
static char kfs_file_buffers[VFS_MAX_OPEN_FILES][VFS_KFS_FILE_BUFFER_SIZE];
static char vfs_copy_buffer[RAMFS_MAX_FILE_SIZE + 1];

static void string_copy(char* destination, const char* source, uint32_t size);
static int ramfs_write_data(const char* path, const char* data, uint32_t size, const char* command_name);
static int vfs_write_data(const char* path, const char* data, uint32_t size);
static const char* kfs_file_name(const char* path);

struct vfs_list_context {
    const char* path;
    int printed_any;
    char seen_dirs[VFS_MAX_SEEN_DIRS][RAMFS_MAX_PATH];
    uint32_t seen_dir_count;
};

struct vfs_kfs_remove_context {
    const char* root;
    char names[VFS_KFS_RECURSIVE_MAX][RAMFS_MAX_PATH];
    int used[VFS_KFS_RECURSIVE_MAX];
    uint32_t count;
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

static int string_contains(const char* text, const char* pattern) {
    if (pattern[0] == '\0') {
        return 1;
    }

    for (uint32_t i = 0; text[i] != '\0'; i++) {
        uint32_t j = 0;

        while (pattern[j] != '\0' && text[i + j] == pattern[j]) {
            j++;
        }

        if (pattern[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

static uint32_t string_length(const char* text) {
    uint32_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
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

static int path_is_root(const char* path) {
    return path[0] == '\0' || string_equals(path, "/");
}

static const char* path_without_leading_slash(const char* path) {
    if (path[0] == '/') {
        return path + 1;
    }

    return path;
}

static int path_is_kfs_root(const char* path) {
    return string_equals(path_without_leading_slash(path), "disk");
}

static const char* kfs_file_name(const char* path) {
    const char* normalized = path_without_leading_slash(path);

    if (!string_starts_with(normalized, "disk/")) {
        return 0;
    }

    const char* name = normalized + 5;
    if (name[0] == '\0') {
        return 0;
    }

    return name;
}

static int path_has_valid_name(const char* path) {
    const char* normalized = path_without_leading_slash(path);

    return normalized[0] != '\0' && normalized[string_length(normalized) - 1] != '/';
}

static int path_parent_exists(const char* path) {
    const char* normalized = path_without_leading_slash(path);
    int has_slash = 0;
    char parent[RAMFS_MAX_PATH];
    uint32_t last_slash = 0;
    uint32_t index = 0;

    for (uint32_t i = 0; normalized[i] != '\0'; i++) {
        if (normalized[i] == '/') {
            has_slash = 1;
            last_slash = i;
        }
    }

    if (!has_slash) {
        return 1;
    }

    while (index < sizeof(parent) - 1 && index < last_slash) {
        parent[index] = normalized[index];
        index++;
    }

    parent[index] = '\0';
    return vfs_is_directory(parent);
}

static int path_is_direct_child(const char* parent, const char* child) {
    const char* normalized_parent = path_without_leading_slash(parent);
    const char* normalized_child = path_without_leading_slash(child);
    uint32_t parent_length = string_length(normalized_parent);
    uint32_t start = 0;

    if (path_is_root(normalized_parent)) {
        start = 0;
    } else {
        if (!string_starts_with(normalized_child, normalized_parent)) {
            return 0;
        }

        if (normalized_child[parent_length] != '/') {
            return 0;
        }

        start = parent_length + 1;
    }

    if (normalized_child[start] == '\0') {
        return 0;
    }

    for (uint32_t i = start; normalized_child[i] != '\0'; i++) {
        if (normalized_child[i] == '/') {
            return 0;
        }
    }

    return 1;
}

static int path_is_descendant(const char* parent, const char* child) {
    const char* normalized_parent = path_without_leading_slash(parent);
    const char* normalized_child = path_without_leading_slash(child);
    uint32_t parent_length = string_length(normalized_parent);

    if (path_is_root(normalized_parent)) {
        return normalized_child[0] != '\0';
    }

    if (!string_starts_with(normalized_child, normalized_parent)) {
        return 0;
    }

    return normalized_child[parent_length] == '/';
}

static void vfs_kfs_collect_remove_candidate(const char* name, uint32_t size,
        uint32_t data_lba, int is_directory, void* raw_context) {
    struct vfs_kfs_remove_context* context =
        (struct vfs_kfs_remove_context*)raw_context;
    (void)size;
    (void)data_lba;
    (void)is_directory;

    if (!path_is_descendant(context->root, name)) {
        return;
    }

    if (context->count >= VFS_KFS_RECURSIVE_MAX) {
        return;
    }

    string_copy(context->names[context->count], name,
        sizeof(context->names[context->count]));
    context->used[context->count] = 1;
    context->count++;
}

static int vfs_kfs_remove_recursive(const char* disk_name) {
    struct vfs_kfs_remove_context context;

    context.root = disk_name;
    context.count = 0;
    for (uint32_t i = 0; i < VFS_KFS_RECURSIVE_MAX; i++) {
        context.used[i] = 0;
        context.names[i][0] = '\0';
    }

    if (!kfs_for_each(vfs_kfs_collect_remove_candidate, &context)) {
        return kfs_remove(disk_name);
    }

    for (;;) {
        uint32_t selected = VFS_KFS_RECURSIVE_MAX;
        uint32_t selected_length = 0;

        for (uint32_t i = 0; i < context.count; i++) {
            if (!context.used[i]) {
                continue;
            }

            uint32_t length = string_length(context.names[i]);
            if (selected == VFS_KFS_RECURSIVE_MAX || length > selected_length) {
                selected = i;
                selected_length = length;
            }
        }

        if (selected == VFS_KFS_RECURSIVE_MAX) {
            break;
        }

        if (!kfs_remove(context.names[selected])) {
            return 0;
        }
        context.used[selected] = 0;
    }

    return kfs_remove(disk_name);
}

static int path_child_name(const char* parent, const char* child, char* output,
        uint32_t size, int* is_directory) {
    const char* normalized_parent = path_without_leading_slash(parent);
    const char* normalized_child = path_without_leading_slash(child);
    uint32_t parent_length = string_length(normalized_parent);
    uint32_t start = 0;
    uint32_t index = 0;

    if (size == 0) {
        return 0;
    }

    if (!path_is_root(normalized_parent)) {
        if (!string_starts_with(normalized_child, normalized_parent)) {
            return 0;
        }

        if (normalized_child[parent_length] != '/') {
            return 0;
        }

        start = parent_length + 1;
    }

    if (normalized_child[start] == '\0') {
        return 0;
    }

    *is_directory = 0;
    while (normalized_child[start] != '\0' && index < size - 1) {
        if (normalized_child[start] == '/') {
            *is_directory = 1;
            break;
        }

        output[index++] = normalized_child[start++];
    }

    output[index] = '\0';
    return index > 0;
}

static int vfs_list_dir_seen(struct vfs_list_context* list, const char* name) {
    for (uint32_t i = 0; i < list->seen_dir_count; i++) {
        if (string_equals(list->seen_dirs[i], name)) {
            return 1;
        }
    }

    if (list->seen_dir_count < VFS_MAX_SEEN_DIRS) {
        string_copy(list->seen_dirs[list->seen_dir_count], name,
            sizeof(list->seen_dirs[list->seen_dir_count]));
        list->seen_dir_count++;
    }

    return 0;
}

static struct ramfs_node* ramfs_find(const char* path) {
    const char* normalized = path_without_leading_slash(path);

    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (ramfs_nodes[i].used && string_equals(ramfs_nodes[i].path, normalized)) {
            return &ramfs_nodes[i];
        }
    }

    return 0;
}

static struct ramfs_node* ramfs_alloc(void) {
    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!ramfs_nodes[i].used) {
            return &ramfs_nodes[i];
        }
    }

    return 0;
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

struct vfs_complete_context {
    const char* prefix;
    const char* match;
    char match_buffer[RAMFS_MAX_PATH];
    int ambiguous;
};

struct kfs_print_context {
    const char* parent;
    int printed_any;
    char seen_dirs[VFS_MAX_SEEN_DIRS][RAMFS_MAX_PATH];
    uint32_t seen_dir_count;
};

struct kfs_complete_context {
    struct vfs_complete_context* complete;
    char candidate[RAMFS_MAX_PATH];
};

static void vfs_complete_visit_root(const char* candidate, struct vfs_complete_context* context) {
    if (!string_starts_with(candidate, context->prefix)) {
        return;
    }

    if (context->match != 0 && !string_equals(context->match, candidate)) {
        context->ambiguous = 1;
        return;
    }

    string_copy(context->match_buffer, candidate, sizeof(context->match_buffer));
    context->match = context->match_buffer;
}

static void vfs_complete_visit_kfs(const char* name, uint32_t size,
        uint32_t data_lba, int is_directory, void* raw_context) {
    struct kfs_complete_context* context = (struct kfs_complete_context*)raw_context;
    (void)size;
    (void)data_lba;
    (void)is_directory;

    string_copy(context->candidate, "disk/", sizeof(context->candidate));
    uint32_t index = 5;
    uint32_t source = 0;

    while (index < sizeof(context->candidate) - 1 && name[source] != '\0') {
        context->candidate[index++] = name[source++];
    }

    context->candidate[index] = '\0';
    vfs_complete_visit_root(context->candidate, context->complete);
}

static void vfs_complete_visit_file(const struct initrd_file* file, void* raw_context) {
    struct vfs_complete_context* context = (struct vfs_complete_context*)raw_context;

    if (string_starts_with(context->prefix, "apps/")) {
        vfs_complete_visit_root(file->name, context);
        return;
    }

    if (!string_starts_with(file->name, "apps/")) {
        vfs_complete_visit_root(file->name, context);
    }
}

static void vfs_complete_visit_ramfs(struct vfs_complete_context* context) {
    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!ramfs_nodes[i].used) {
            continue;
        }

        vfs_complete_visit_root(ramfs_nodes[i].path, context);
    }
}

static void vfs_print_initrd_entry(const struct initrd_file* file, void* context) {
    struct vfs_list_context* list = (struct vfs_list_context*)context;
    char name[RAMFS_MAX_PATH];
    int is_directory;

    if (!path_child_name(list->path, file->name, name, sizeof(name), &is_directory)) {
        return;
    }

    terminal_write("  ");

    if (is_directory) {
        if (vfs_list_dir_seen(list, name)) {
            return;
        }

        terminal_write(name);
        terminal_write("/\n");
    } else {
        terminal_write(name);
        terminal_write("  ");
        terminal_write_dec(file->size);
        terminal_write(" bytes\n");
    }

    list->printed_any = 1;
}

static void vfs_print_ramfs_entry(struct ramfs_node* node, struct vfs_list_context* list) {
    terminal_write("  ");
    terminal_write(path_basename(node->path));

    if (node->type == RAMFS_DIRECTORY) {
        terminal_write("/  <ram>\n");
    } else {
        terminal_write("  ");
        terminal_write_dec(node->size);
        terminal_write(" bytes  <ram>\n");
    }

    list->printed_any = 1;
}

static void vfs_print_kfs_entry(const char* name, uint32_t size,
        uint32_t data_lba, int is_directory, void* raw_context) {
    struct kfs_print_context* context = (struct kfs_print_context*)raw_context;
    char child_name[RAMFS_MAX_PATH];
    int child_is_directory = 0;
    (void)data_lba;

    if (!path_child_name(context->parent, name, child_name, sizeof(child_name),
            &child_is_directory)) {
        return;
    }

    if (is_directory || child_is_directory) {
        if (vfs_list_dir_seen((struct vfs_list_context*)context, child_name)) {
            return;
        }

        terminal_write("  ");
        terminal_write(child_name);
        terminal_write("/  <disk>\n");
        context->printed_any = 1;
        return;
    }

    terminal_write("  ");
    terminal_write(child_name);
    terminal_write("  ");
    terminal_write_dec(size);
    terminal_write(" bytes  <disk>\n");
    context->printed_any = 1;
}

struct vfs_dir_exists_context {
    const char* path;
    int exists;
};

struct vfs_dir_count_context {
    const char* path;
    uint32_t count;
    char seen_dirs[VFS_MAX_SEEN_DIRS][RAMFS_MAX_PATH];
    uint32_t seen_dir_count;
};

struct vfs_tree_context {
    const char* path;
    uint32_t depth;
    char seen_dirs[VFS_MAX_SEEN_DIRS][RAMFS_MAX_PATH];
    uint32_t seen_dir_count;
};

struct vfs_du_context {
    const char* path;
    uint32_t logical_bytes;
    uint32_t allocated_bytes;
    uint32_t files;
    uint32_t directories;
    int exact_file_found;
};

struct vfs_find_context {
    const char* path;
    const char* pattern;
    uint32_t matches;
};

static void vfs_tree_dir(const char* path, uint32_t depth);

static int vfs_find_in_scope(const char* parent, const char* candidate) {
    const char* normalized_parent = path_without_leading_slash(parent);
    const char* normalized_candidate = path_without_leading_slash(candidate);

    if (path_is_root(normalized_parent)) {
        return normalized_candidate[0] != '\0';
    }

    return string_equals(normalized_parent, normalized_candidate) ||
        path_is_descendant(normalized_parent, normalized_candidate);
}

static void vfs_find_print(struct vfs_find_context* context,
        const char* path, int is_directory, const char* source) {
    if (!vfs_find_in_scope(context->path, path)) {
        return;
    }

    if (!string_contains(path, context->pattern) &&
            !string_contains(path_basename(path), context->pattern)) {
        return;
    }

    terminal_write("  /");
    terminal_write(path_without_leading_slash(path));
    terminal_write(is_directory ? "/  <" : "  <");
    terminal_write(source);
    terminal_write(">\n");
    context->matches++;
}

static void vfs_find_visit_initrd(const struct initrd_file* file, void* raw_context) {
    struct vfs_find_context* context = (struct vfs_find_context*)raw_context;

    vfs_find_print(context, file->name, 0, "initrd");
}

static void vfs_find_visit_kfs(const char* name, uint32_t size,
        uint32_t data_lba, int is_directory, void* raw_context) {
    struct vfs_find_context* context = (struct vfs_find_context*)raw_context;
    char path[RAMFS_MAX_PATH];
    uint32_t index = 0;
    const char* prefix = "disk/";
    (void)size;
    (void)data_lba;

    while (prefix[index] != '\0' && index < sizeof(path) - 1) {
        path[index] = prefix[index];
        index++;
    }

    for (uint32_t source = 0; name[source] != '\0' && index < sizeof(path) - 1; source++) {
        path[index++] = name[source];
    }

    path[index] = '\0';
    vfs_find_print(context, path, is_directory, "disk");
}

static void vfs_check_initrd_dir(const struct initrd_file* file, void* raw_context) {
    struct vfs_dir_exists_context* context = (struct vfs_dir_exists_context*)raw_context;
    const char* path = path_without_leading_slash(context->path);
    uint32_t length = string_length(path);

    if (context->exists || length == 0) {
        return;
    }

    if (string_starts_with(file->name, path) && file->name[length] == '/') {
        context->exists = 1;
    }
}

static void vfs_count_initrd_child(const struct initrd_file* file, void* raw_context) {
    struct vfs_dir_count_context* context = (struct vfs_dir_count_context*)raw_context;
    struct vfs_list_context list;
    char name[RAMFS_MAX_PATH];
    int is_directory;

    list.seen_dir_count = context->seen_dir_count;
    for (uint32_t i = 0; i < context->seen_dir_count; i++) {
        string_copy(list.seen_dirs[i], context->seen_dirs[i], sizeof(list.seen_dirs[i]));
    }

    if (!path_child_name(context->path, file->name, name, sizeof(name), &is_directory)) {
        return;
    }

    if (is_directory && vfs_list_dir_seen(&list, name)) {
        return;
    }

    if (is_directory) {
        context->seen_dir_count = list.seen_dir_count;
        for (uint32_t i = 0; i < list.seen_dir_count; i++) {
            string_copy(context->seen_dirs[i], list.seen_dirs[i], sizeof(context->seen_dirs[i]));
        }
    }

    context->count++;
}

static uint32_t vfs_count_children(const char* path) {
    struct vfs_dir_count_context context;

    context.path = path;
    context.count = 0;
    context.seen_dir_count = 0;

    initrd_for_each(vfs_count_initrd_child, &context);

    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (ramfs_nodes[i].used && path_is_direct_child(path, ramfs_nodes[i].path)) {
            context.count++;
        }
    }

    if (path_is_root(path)) {
        context.count++;
    }

    return context.count;
}

static void vfs_du_add_file(struct vfs_du_context* context, uint32_t logical,
        uint32_t allocated) {
    context->logical_bytes += logical;
    context->allocated_bytes += allocated;
    context->files++;
}

static void vfs_du_visit_initrd(const struct initrd_file* file, void* raw_context) {
    struct vfs_du_context* context = (struct vfs_du_context*)raw_context;

    if (string_equals(path_without_leading_slash(context->path), file->name)) {
        vfs_du_add_file(context, file->size, file->size);
        context->exact_file_found = 1;
        return;
    }

    if (vfs_is_directory(context->path) &&
            path_is_descendant(context->path, file->name)) {
        vfs_du_add_file(context, file->size, file->size);
    }
}

static void vfs_du_visit_kfs(const char* name, uint32_t size,
        uint32_t data_lba, int is_directory, void* raw_context) {
    struct vfs_du_context* context = (struct vfs_du_context*)raw_context;
    const char* disk_name = kfs_file_name(context->path);
    const char* target = disk_name != 0 ? disk_name : "";
    (void)data_lba;

    if (disk_name != 0 && string_equals(target, name)) {
        if (is_directory) {
            context->directories++;
        } else {
            vfs_du_add_file(context, size, kfs_allocated_bytes(size));
            context->exact_file_found = 1;
        }
        return;
    }

    if ((path_is_root(context->path) ||
            path_is_kfs_root(context->path) ||
            (disk_name != 0 && kfs_is_directory(disk_name))) &&
            (target[0] == '\0' || path_is_descendant(target, name))) {
        if (is_directory) {
            context->directories++;
        } else {
            vfs_du_add_file(context, size, kfs_allocated_bytes(size));
        }
    }
}

static void path_join(const char* parent, const char* child, char* output, uint32_t size) {
    const char* normalized_parent = path_without_leading_slash(parent);
    uint32_t index = 0;
    uint32_t source = 0;

    if (size == 0) {
        return;
    }

    if (!path_is_root(normalized_parent)) {
        while (index < size - 1 && normalized_parent[index] != '\0') {
            output[index] = normalized_parent[index];
            index++;
        }

        if (index < size - 1) {
            output[index++] = '/';
        }
    }

    while (index < size - 1 && child[source] != '\0') {
        output[index++] = child[source++];
    }

    output[index] = '\0';
}

static void vfs_tree_indent(uint32_t depth) {
    for (uint32_t i = 0; i < depth; i++) {
        terminal_write("  ");
    }
}

static void vfs_tree_print_initrd_child(const struct initrd_file* file, void* raw_context) {
    struct vfs_tree_context* context = (struct vfs_tree_context*)raw_context;
    struct vfs_list_context seen;
    char name[RAMFS_MAX_PATH];
    char child_path[RAMFS_MAX_PATH];
    int is_directory;

    seen.seen_dir_count = context->seen_dir_count;
    for (uint32_t i = 0; i < context->seen_dir_count; i++) {
        string_copy(seen.seen_dirs[i], context->seen_dirs[i], sizeof(seen.seen_dirs[i]));
    }

    if (!path_child_name(context->path, file->name, name, sizeof(name), &is_directory)) {
        return;
    }

    if (is_directory) {
        if (vfs_list_dir_seen(&seen, name)) {
            return;
        }

        context->seen_dir_count = seen.seen_dir_count;
        for (uint32_t i = 0; i < seen.seen_dir_count; i++) {
            string_copy(context->seen_dirs[i], seen.seen_dirs[i], sizeof(context->seen_dirs[i]));
        }
    }

    vfs_tree_indent(context->depth);
    terminal_write(name);

    if (is_directory) {
        terminal_write("/\n");
        path_join(context->path, name, child_path, sizeof(child_path));
        vfs_tree_dir(child_path, context->depth + 1);
    } else {
        terminal_write("\n");
    }
}

static void vfs_tree_dir(const char* path, uint32_t depth) {
    struct vfs_tree_context context;

    context.path = path;
    context.depth = depth;
    context.seen_dir_count = 0;

    initrd_for_each(vfs_tree_print_initrd_child, &context);

    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!ramfs_nodes[i].used || !path_is_direct_child(path, ramfs_nodes[i].path)) {
            continue;
        }

        vfs_tree_indent(depth);
        terminal_write(path_basename(ramfs_nodes[i].path));

        if (ramfs_nodes[i].type == RAMFS_DIRECTORY) {
            terminal_write("/  <ram>\n");
            vfs_tree_dir(ramfs_nodes[i].path, depth + 1);
        } else {
            terminal_write("  <ram>\n");
        }
    }
}

int vfs_open(const char* path) {
    struct initrd_file file;
    struct ramfs_node* node = ramfs_find(path);
    const char* disk_name = kfs_file_name(path);

    if (node != 0) {
        if (node->type != RAMFS_FILE) {
            return VFS_INVALID_FD;
        }

        for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (!handles[i].used) {
                handles[i].used = 1;
                handles[i].data = node->data;
                handles[i].size = node->size;
                handles[i].offset = 0;
                handles[i].readable = 1;
                handles[i].writable = 0;
                handles[i].owns_data = 0;
                handles[i].flags = VFS_O_READ;
                handles[i].node = node;
                return (int)i;
            }
        }

        return VFS_INVALID_FD;
    }

    if (disk_name != 0) {
        for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (!handles[i].used) {
                uint32_t size = 0;

                if (!kfs_read_text(disk_name, kfs_file_buffers[i],
                        VFS_KFS_FILE_BUFFER_SIZE, &size)) {
                    return VFS_INVALID_FD;
                }

                if (size >= VFS_KFS_FILE_BUFFER_SIZE) {
                    size = VFS_KFS_FILE_BUFFER_SIZE - 1;
                }

                handles[i].used = 1;
                handles[i].data = kfs_file_buffers[i];
                handles[i].size = size;
                handles[i].offset = 0;
                handles[i].readable = 1;
                handles[i].writable = 0;
                handles[i].owns_data = 0;
                handles[i].flags = VFS_O_READ;
                handles[i].node = 0;
                return (int)i;
            }
        }

        return VFS_INVALID_FD;
    }

    if (!initrd_find(path, &file)) {
        return VFS_INVALID_FD;
    }

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!handles[i].used) {
            handles[i].used = 1;
            handles[i].data = file.data;
            handles[i].size = file.size;
            handles[i].offset = 0;
            handles[i].readable = 1;
            handles[i].writable = 0;
            handles[i].owns_data = 0;
            handles[i].flags = VFS_O_READ;
            handles[i].node = 0;
            return (int)i;
        }
    }

    return VFS_INVALID_FD;
}

int vfs_open_flags(const char* path, uint32_t flags) {
    struct ramfs_node* node;
    struct initrd_file existing;

    if ((flags & VFS_O_WRITE) == 0) {
        return vfs_open(path);
    }

    if (vfs_is_directory(path)) {
        return VFS_INVALID_FD;
    }

    node = ramfs_find(path);
    if (node == 0 && initrd_find(path_without_leading_slash(path), &existing)) {
        return VFS_INVALID_FD;
    }

    if (node == 0 && (flags & VFS_O_CREATE) == 0) {
        return VFS_INVALID_FD;
    }

    if ((flags & VFS_O_TRUNC) || node == 0) {
        if (!ramfs_write_data(path, "", 0, "open")) {
            return VFS_INVALID_FD;
        }

        node = ramfs_find(path);
    }

    if (node == 0 || node->type != RAMFS_FILE) {
        return VFS_INVALID_FD;
    }

    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!handles[i].used) {
            handles[i].used = 1;
            handles[i].data = node->data;
            handles[i].size = node->size;
            handles[i].offset = (flags & VFS_O_APPEND) ? node->size : 0;
            handles[i].readable = (flags & VFS_O_READ) != 0;
            handles[i].writable = 1;
            handles[i].owns_data = 0;
            handles[i].flags = flags;
            handles[i].node = node;
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
    if (!handle->readable) {
        return -1;
    }

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

int32_t vfs_write(int fd, const char* buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || buffer == 0 || !handles[fd].used) {
        return -1;
    }

    struct vfs_handle* handle = &handles[fd];
    struct ramfs_node* node = handle->node;

    if (!handle->writable || node == 0 || node->type != RAMFS_FILE) {
        return -1;
    }

    if (handle->flags & VFS_O_APPEND) {
        handle->offset = node->size;
    }

    uint32_t end_offset = handle->offset + size;
    if (end_offset < handle->offset || end_offset > RAMFS_MAX_FILE_SIZE) {
        return -1;
    }

    uint32_t new_size = node->size > end_offset ? node->size : end_offset;
    char* data = (char*)kmalloc(new_size + 1);
    if (data == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < new_size; i++) {
        data[i] = i < node->size ? node->data[i] : 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        data[handle->offset + i] = buffer[i];
    }

    data[new_size] = '\0';

    int ok = ramfs_write_data(node->path, data, new_size, "write");
    kfree(data);

    if (!ok) {
        return -1;
    }

    handle->offset += size;
    handle->data = node->data;
    handle->size = node->size;
    return (int32_t)size;
}

void vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return;
    }

    if (handles[fd].owns_data && handles[fd].data != 0) {
        kfree((void*)handles[fd].data);
    }

    handles[fd].used = 0;
    handles[fd].data = 0;
    handles[fd].size = 0;
    handles[fd].offset = 0;
    handles[fd].readable = 0;
    handles[fd].writable = 0;
    handles[fd].owns_data = 0;
    handles[fd].flags = 0;
    handles[fd].node = 0;
}

int vfs_complete_path(const char* prefix, char* output, uint32_t size) {
    struct vfs_complete_context context;
    context.prefix = prefix;
    context.match = 0;
    context.ambiguous = 0;
    context.match_buffer[0] = '\0';

    if (prefix[0] == '/' && prefix[1] != '\0') {
        context.prefix = prefix + 1;
    }

    if (string_starts_with("apps", context.prefix)) {
        vfs_complete_visit_root("apps", &context);
    }

    if (string_starts_with("disk", context.prefix)) {
        vfs_complete_visit_root("disk", &context);
    }

    initrd_for_each(vfs_complete_visit_file, &context);
    vfs_complete_visit_ramfs(&context);

    if (string_starts_with(context.prefix, "disk/")) {
        struct kfs_complete_context kfs_context;
        kfs_context.complete = &context;
        kfs_context.candidate[0] = '\0';
        kfs_for_each(vfs_complete_visit_kfs, &kfs_context);
    }

    if (context.match == 0 || context.ambiguous) {
        return 0;
    }

    string_copy(output, context.match, size);
    return 1;
}

int vfs_is_directory(const char* path) {
    struct ramfs_node* node = ramfs_find(path);
    struct vfs_dir_exists_context context;

    if (path_is_root(path)) {
        return 1;
    }

    if (path_is_kfs_root(path)) {
        return 1;
    }

    const char* disk_name = kfs_file_name(path);
    if (disk_name != 0) {
        return kfs_is_directory(disk_name);
    }

    if (node != 0 && node->type == RAMFS_DIRECTORY) {
        return 1;
    }

    context.path = path;
    context.exists = 0;
    initrd_for_each(vfs_check_initrd_dir, &context);
    return context.exists;
}

int vfs_mkdir(const char* path) {
    const char* normalized = path_without_leading_slash(path);
    struct ramfs_node* node;

    if (!path_has_valid_name(path)) {
        terminal_write("mkdir: invalid path\n");
        return 0;
    }

    if (path_is_kfs_root(path)) {
        terminal_write("mkdir: already exists: disk\n");
        return 0;
    }

    const char* disk_name = kfs_file_name(path);
    if (disk_name != 0) {
        return kfs_mkdir(disk_name);
    }

    if (vfs_is_directory(path) || ramfs_find(path) != 0) {
        terminal_write("mkdir: already exists: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (!path_parent_exists(path)) {
        terminal_write("mkdir: parent not found: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    node = ramfs_alloc();
    if (node == 0) {
        terminal_write("mkdir: ramfs full\n");
        return 0;
    }

    node->used = 1;
    node->type = RAMFS_DIRECTORY;
    string_copy(node->path, normalized, sizeof(node->path));
    node->data = 0;
    node->size = 0;
    node->capacity = 0;
    return 1;
}

static int ramfs_write_data(const char* path, const char* data, uint32_t size, const char* command_name) {
    const char* normalized = path_without_leading_slash(path);
    struct ramfs_node* node;
    char* allocation;

    if (!path_has_valid_name(path)) {
        terminal_write(command_name);
        terminal_write(": invalid path\n");
        return 0;
    }

    if (!path_parent_exists(path)) {
        terminal_write(command_name);
        terminal_write(": parent not found: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    node = ramfs_find(path);
    if (node != 0 && node->type == RAMFS_DIRECTORY) {
        terminal_write(command_name);
        terminal_write(": is a directory: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (node == 0) {
        struct initrd_file existing;
        if (initrd_find(normalized, &existing)) {
            terminal_write(command_name);
            terminal_write(": destination is read-only: ");
            terminal_write(path);
            terminal_write("\n");
            return 0;
        }
    }

    if (node == 0) {
        node = ramfs_alloc();
    }

    if (node == 0) {
        terminal_write(command_name);
        terminal_write(": ramfs full\n");
        return 0;
    }

    if (size > RAMFS_MAX_FILE_SIZE) {
        terminal_write(command_name);
        terminal_write(": file too large for ramfs\n");
        return 0;
    }

    allocation = (char*)kmalloc(size + 1);
    if (allocation == 0) {
        terminal_write(command_name);
        terminal_write(": out of heap memory\n");
        return 0;
    }

    node->used = 1;
    node->type = RAMFS_FILE;
    string_copy(node->path, normalized, sizeof(node->path));

    for (uint32_t i = 0; i < size; i++) {
        allocation[i] = data[i];
    }

    allocation[size] = '\0';
    if (node->type == RAMFS_FILE && node->data != 0) {
        kfree(node->data);
    }

    node->data = allocation;
    node->size = size;
    node->capacity = size + 1;
    return 1;
}

int vfs_write_text(const char* path, const char* text) {
    return vfs_write_data(path, text, string_length(text));
}

static int vfs_write_data(const char* path, const char* data, uint32_t size) {
    const char* disk_name = kfs_file_name(path);

    if (disk_name != 0) {
        return kfs_save_data(disk_name, data, size);
    }

    return ramfs_write_data(path, data, size, "write");
}

int vfs_append_text(const char* path, const char* text) {
    struct ramfs_node* node = ramfs_find(path);
    struct initrd_file file;
    uint32_t old_size = 0;
    uint32_t append_size = string_length(text);
    uint32_t new_size;
    char* buffer;
    const char* disk_name = kfs_file_name(path);

    if (disk_name != 0) {
        return kfs_append_text(disk_name, text);
    }

    if (node != 0 && node->type == RAMFS_DIRECTORY) {
        terminal_write("append: is a directory: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (node == 0 && initrd_find(path_without_leading_slash(path), &file)) {
        terminal_write("append: destination is read-only: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (node != 0) {
        old_size = node->size;
    }

    new_size = old_size + append_size;
    if (new_size > RAMFS_MAX_FILE_SIZE) {
        terminal_write("append: file too large for ramfs\n");
        return 0;
    }

    buffer = (char*)kmalloc(new_size + 1);
    if (buffer == 0) {
        terminal_write("append: out of heap memory\n");
        return 0;
    }

    for (uint32_t i = 0; i < old_size; i++) {
        buffer[i] = node->data[i];
    }

    for (uint32_t i = 0; i < append_size; i++) {
        buffer[old_size + i] = text[i];
    }

    buffer[new_size] = '\0';
    int ok = ramfs_write_data(path, buffer, new_size, "append");
    kfree(buffer);
    return ok;
}

int vfs_copy(const char* source_path, const char* destination_path) {
    int fd;
    uint32_t total = 0;
    struct initrd_file destination_file;

    if (vfs_is_directory(source_path)) {
        terminal_write("cp: source is a directory: ");
        terminal_write(source_path);
        terminal_write("\n");
        return 0;
    }

    if (initrd_find(path_without_leading_slash(destination_path), &destination_file) &&
            ramfs_find(destination_path) == 0) {
        terminal_write("cp: destination is read-only: ");
        terminal_write(destination_path);
        terminal_write("\n");
        return 0;
    }

    fd = vfs_open(source_path);
    if (fd == VFS_INVALID_FD) {
        terminal_write("cp: source not found: ");
        terminal_write(source_path);
        terminal_write("\n");
        return 0;
    }

    for (;;) {
        int32_t bytes = vfs_read(fd, vfs_copy_buffer + total,
            (RAMFS_MAX_FILE_SIZE + 1) - total);

        if (bytes < 0) {
            terminal_write("cp: read failed: ");
            terminal_write(source_path);
            terminal_write("\n");
            vfs_close(fd);
            return 0;
        }

        if (bytes == 0) {
            break;
        }

        total += (uint32_t)bytes;

        if (total > RAMFS_MAX_FILE_SIZE) {
            terminal_write("cp: source too large for ramfs\n");
            vfs_close(fd);
            return 0;
        }
    }

    vfs_close(fd);
    return vfs_write_data(destination_path, vfs_copy_buffer, total);
}

int vfs_move(const char* source_path, const char* destination_path) {
    struct ramfs_node* source = ramfs_find(source_path);

    if (string_equals(path_without_leading_slash(source_path),
            path_without_leading_slash(destination_path))) {
        return 1;
    }

    if (vfs_is_directory(source_path)) {
        terminal_write("mv: source is a directory: ");
        terminal_write(source_path);
        terminal_write("\n");
        return 0;
    }

    if (source == 0) {
        struct initrd_file file;

        if (initrd_find(path_without_leading_slash(source_path), &file)) {
            terminal_write("mv: source is read-only; use cp: ");
            terminal_write(source_path);
            terminal_write("\n");
            return 0;
        }

        terminal_write("mv: source not found: ");
        terminal_write(source_path);
        terminal_write("\n");
        return 0;
    }

    if (!vfs_copy(source_path, destination_path)) {
        return 0;
    }

    source->used = 0;
    source->path[0] = '\0';
    kfree(source->data);
    source->data = 0;
    source->size = 0;
    source->capacity = 0;
    return 1;
}

int vfs_remove(const char* path) {
    struct ramfs_node* node = ramfs_find(path);
    const char* disk_name = kfs_file_name(path);

    if (disk_name != 0) {
        return kfs_remove(disk_name);
    }

    if (node == 0) {
        terminal_write("rm: not found or read-only: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (node->type == RAMFS_DIRECTORY) {
        for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
            if (ramfs_nodes[i].used && path_is_direct_child(node->path, ramfs_nodes[i].path)) {
                terminal_write("rm: directory not empty: ");
                terminal_write(path);
                terminal_write("\n");
                return 0;
            }
        }
    }

    node->used = 0;
    node->path[0] = '\0';
    kfree(node->data);
    node->data = 0;
    node->size = 0;
    node->capacity = 0;
    return 1;
}

int vfs_remove_recursive(const char* path) {
    struct ramfs_node* node = ramfs_find(path);
    const char* disk_name = kfs_file_name(path);

    if (path_is_kfs_root(path)) {
        terminal_write("rm: refusing to remove disk mount\n");
        return 0;
    }

    if (disk_name != 0) {
        return vfs_kfs_remove_recursive(disk_name);
    }

    if (node == 0) {
        terminal_write("rm: not found or read-only: ");
        terminal_write(path);
        terminal_write("\n");
        return 0;
    }

    if (node->type == RAMFS_DIRECTORY) {
        for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
            if (ramfs_nodes[i].used && path_is_descendant(node->path, ramfs_nodes[i].path)) {
                ramfs_nodes[i].used = 0;
                ramfs_nodes[i].path[0] = '\0';
                kfree(ramfs_nodes[i].data);
                ramfs_nodes[i].data = 0;
                ramfs_nodes[i].size = 0;
                ramfs_nodes[i].capacity = 0;
            }
        }
    }

    node->used = 0;
    node->path[0] = '\0';
    kfree(node->data);
    node->data = 0;
    node->size = 0;
    node->capacity = 0;
    return 1;
}

void vfs_list(void) {
    vfs_list_path("");
}

void vfs_list_path(const char* path) {
    struct vfs_list_context context;
    context.path = path;
    context.printed_any = 0;
    context.seen_dir_count = 0;

    if (path[0] == '\0' || string_equals(path, "/")) {
        terminal_write("Files /:\n");
        initrd_for_each(vfs_print_initrd_entry, &context);
        terminal_write("  disk/\n");
        context.printed_any = 1;
    } else if (path_is_kfs_root(path)) {
        struct kfs_print_context kfs_context;
        kfs_context.parent = "";
        kfs_context.printed_any = 0;
        kfs_context.seen_dir_count = 0;

        terminal_write("Files /disk:\n");
        if (!kfs_for_each(vfs_print_kfs_entry, &kfs_context)) {
            terminal_write("  <not formatted>\n");
        } else if (!kfs_context.printed_any) {
            terminal_write("  <empty>\n");
        }
        return;
    } else if (kfs_file_name(path) != 0 && vfs_is_directory(path)) {
        struct kfs_print_context kfs_context;
        kfs_context.parent = kfs_file_name(path);
        kfs_context.printed_any = 0;
        kfs_context.seen_dir_count = 0;

        terminal_write("Files /");
        terminal_write(path_without_leading_slash(path));
        terminal_write(":\n");
        if (!kfs_for_each(vfs_print_kfs_entry, &kfs_context)) {
            terminal_write("  <not formatted>\n");
        } else if (!kfs_context.printed_any) {
            terminal_write("  <empty>\n");
        }
        return;
    } else if (vfs_is_directory(path)) {
        terminal_write("Files /");
        terminal_write(path_without_leading_slash(path));
        terminal_write(":\n");
        initrd_for_each(vfs_print_initrd_entry, &context);
    } else {
        terminal_write("Directory not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (ramfs_nodes[i].used && path_is_direct_child(path, ramfs_nodes[i].path)) {
            vfs_print_ramfs_entry(&ramfs_nodes[i], &context);
        }
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

void vfs_du(const char* path) {
    struct vfs_du_context context;
    struct ramfs_node* node = ramfs_find(path);
    const char* normalized = path_without_leading_slash(path);
    const char* disk_name = kfs_file_name(path);

    context.path = path;
    context.logical_bytes = 0;
    context.allocated_bytes = 0;
    context.files = 0;
    context.directories = 0;
    context.exact_file_found = 0;

    if (node != 0 && node->type == RAMFS_FILE) {
        vfs_du_add_file(&context, node->size, node->capacity);
        context.exact_file_found = 1;
    } else if (node != 0 && node->type == RAMFS_DIRECTORY) {
        context.directories++;
    }

    initrd_for_each(vfs_du_visit_initrd, &context);

    if (path_is_root(path) || path_is_kfs_root(path) || disk_name != 0) {
        kfs_for_each(vfs_du_visit_kfs, &context);
    }

    if (vfs_is_directory(path)) {
        for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
            if (!ramfs_nodes[i].used || !path_is_descendant(path, ramfs_nodes[i].path)) {
                continue;
            }

            if (ramfs_nodes[i].type == RAMFS_DIRECTORY) {
                context.directories++;
            } else {
                vfs_du_add_file(&context, ramfs_nodes[i].size, ramfs_nodes[i].capacity);
            }
        }
    }

    if (!vfs_is_directory(path) && !context.exact_file_found) {
        terminal_write("du: not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    terminal_write("Disk usage for /");
    terminal_write(normalized);
    terminal_write(":\n  Logical bytes: ");
    terminal_write_dec(context.logical_bytes);
    terminal_write("\n  Allocated bytes: ");
    terminal_write_dec(context.allocated_bytes);
    terminal_write("\n  Files: ");
    terminal_write_dec(context.files);
    terminal_write("\n  Directories: ");
    terminal_write_dec(context.directories);
    terminal_write("\n");
}

void vfs_find(const char* path, const char* pattern) {
    struct vfs_find_context context;
    int fd = VFS_INVALID_FD;

    context.path = path;
    context.pattern = pattern;
    context.matches = 0;

    if (!vfs_is_directory(path)) {
        fd = vfs_open(path);
    }

    if (!vfs_is_directory(path) && fd == VFS_INVALID_FD) {
        terminal_write("find: path not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    if (fd != VFS_INVALID_FD) {
        vfs_close(fd);
    }

    terminal_write("Find results under /");
    terminal_write(path_without_leading_slash(path));
    terminal_write(" for '");
    terminal_write(pattern);
    terminal_write("':\n");

    initrd_for_each(vfs_find_visit_initrd, &context);

    for (uint32_t i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!ramfs_nodes[i].used) {
            continue;
        }

        vfs_find_print(&context, ramfs_nodes[i].path,
            ramfs_nodes[i].type == RAMFS_DIRECTORY, "ram");
    }

    if (path_is_root(path) || path_is_kfs_root(path) || kfs_file_name(path) != 0) {
        kfs_for_each(vfs_find_visit_kfs, &context);
    }

    if (context.matches == 0) {
        terminal_write("  <no matches>\n");
    }
}

void vfs_stat(const char* path) {
    const char* normalized = path_without_leading_slash(path);
    struct ramfs_node* node = ramfs_find(path);
    struct initrd_file file;
    const char* disk_name = kfs_file_name(path);

    terminal_write("Path: /");
    terminal_write(normalized);
    terminal_write("\n");

    if (path_is_root(path)) {
        terminal_write("Type: directory\n");
        terminal_write("Source: vfs\n");
        terminal_write("Writable: partial\n");
        terminal_write("Children: ");
        terminal_write_dec(vfs_count_children(path));
        terminal_write("\n");
        return;
    }

    if (path_is_kfs_root(path)) {
        terminal_write("Type: directory\n");
        terminal_write("Source: kfs mount\n");
        terminal_write("Writable: yes\n");
        return;
    }

    if (disk_name != 0) {
        terminal_write("Source: kfs\n");
        kfs_stat(disk_name);
        return;
    }

    if (node != 0) {
        terminal_write("Type: ");
        terminal_write(node->type == RAMFS_DIRECTORY ? "directory" : "file");
        terminal_write("\nSource: ramfs\nWritable: yes\n");

        if (node->type == RAMFS_FILE) {
            terminal_write("Size: ");
            terminal_write_dec(node->size);
            terminal_write(" bytes\n");
            terminal_write("Capacity: ");
            terminal_write_dec(node->capacity);
            terminal_write(" bytes\n");
        } else {
            terminal_write("Children: ");
            terminal_write_dec(vfs_count_children(path));
            terminal_write("\n");
        }

        return;
    }

    if (initrd_find(normalized, &file)) {
        terminal_write("Type: file\n");
        terminal_write("Source: initrd\n");
        terminal_write("Writable: no\n");
        terminal_write("Size: ");
        terminal_write_dec(file.size);
        terminal_write(" bytes\n");
        return;
    }

    if (vfs_is_directory(path)) {
        terminal_write("Type: directory\n");
        terminal_write("Source: initrd\n");
        terminal_write("Writable: no\n");
        terminal_write("Children: ");
        terminal_write_dec(vfs_count_children(path));
        terminal_write("\n");
        return;
    }

    terminal_write("Not found\n");
}

void vfs_tree(const char* path) {
    const char* normalized = path_without_leading_slash(path);

    if (!vfs_is_directory(path)) {
        terminal_write("tree: directory not found: ");
        terminal_write(path);
        terminal_write("\n");
        return;
    }

    if (path_is_root(path)) {
        terminal_write("/\n");
    } else {
        terminal_write("/");
        terminal_write(normalized);
        terminal_write("/\n");
    }

    if (path_is_root(path)) {
        terminal_write("  disk/\n");
    }

    if (path_is_kfs_root(path)) {
        struct kfs_print_context kfs_context;
        kfs_context.parent = "";
        kfs_context.printed_any = 0;
        kfs_context.seen_dir_count = 0;
        kfs_for_each(vfs_print_kfs_entry, &kfs_context);
        if (!kfs_context.printed_any) {
            terminal_write("  <empty>\n");
        }
        return;
    }

    if (kfs_file_name(path) != 0 && vfs_is_directory(path)) {
        struct kfs_print_context kfs_context;
        kfs_context.parent = kfs_file_name(path);
        kfs_context.printed_any = 0;
        kfs_context.seen_dir_count = 0;
        kfs_for_each(vfs_print_kfs_entry, &kfs_context);
        if (!kfs_context.printed_any) {
            terminal_write("  <empty>\n");
        }
        return;
    }

    vfs_tree_dir(path, 1);
}
