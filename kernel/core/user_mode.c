#include "user_mode.h"

#include "arch.h"
#include "pmm.h"
#include "task.h"
#include "terminal.h"

#define USER_STACK_TOP 0x02000000
#define USER_STACK_BASE 0x02000000
#define USER_STACK_STRIDE 0x4000
#define PAGE_MASK 0xFFFFF000

extern uint8_t user_image_start;
extern uint8_t user_image_end;
extern void user_test(void);

static int map_user_range(uint32_t page_directory, uint32_t start, uint32_t end, uint32_t flags) {
    start &= PAGE_MASK;
    end = (end + 0xFFF) & PAGE_MASK;

    for (uint32_t address = start; address < end; address += 0x1000) {
        if (!arch_map_address_space_page(page_directory, address, address,
                flags | ARCH_PAGE_USER)) {
            return 0;
        }
    }

    return 1;
}

static int prepare_user_test_memory(uint32_t page_directory, uint32_t user_stack_top) {
    uint32_t user_stack_page = user_stack_top - USER_STACK_STRIDE;
    uint32_t user_region_start = (uint32_t)&user_image_start;
    uint32_t user_region_end = (uint32_t)&user_image_end;

    for (uint32_t offset = 0; offset < USER_STACK_STRIDE; offset += 0x1000) {
        uint32_t user_stack_physical = pmm_alloc_page();

        if (user_stack_physical == 0) {
            terminal_write("ring3 failed: no stack page\n");
            return 0;
        }

        arch_zero_physical_page(user_stack_physical);

        if (!arch_map_address_space_page(page_directory, user_stack_page + offset, user_stack_physical,
                ARCH_PAGE_WRITABLE | ARCH_PAGE_USER)) {
            terminal_write("ring3 failed: stack map failed\n");
            return 0;
        }
    }

    if (!map_user_range(page_directory, user_region_start, user_region_end, 0)) {
        terminal_write("ring3 failed: user image map failed\n");
        return 0;
    }

    return 1;
}

struct task* user_mode_spawn_app_with_args(const char* name, uint32_t entry, const char* args) {
    uint32_t stack_top = USER_STACK_BASE + ((uint32_t)task_next_id() * USER_STACK_STRIDE);

    struct task* task = task_create_user_with_args(name, entry, stack_top, args);
    if (task == 0) {
        terminal_write("spawn failed: no task slot\n");
        return 0;
    }

    if (!prepare_user_test_memory(task->page_directory, stack_top)) {
        task_kill(task->id, 1);
        return 0;
    }

    task_set_user_memory(task, (uint32_t)&user_image_start, (uint32_t)&user_image_end,
        stack_top - USER_STACK_STRIDE, stack_top);

    terminal_write("Spawned ");
    terminal_write(name);
    terminal_write(" task ");
    terminal_write_dec(task->id);
    if (args != 0 && args[0] != '\0') {
        terminal_write(" args=");
        terminal_write(args);
    }
    terminal_write("\n");
    return task;
}

struct task* user_mode_spawn_app(const char* name, uint32_t entry) {
    return user_mode_spawn_app_with_args(name, entry, "");
}

void user_mode_spawn_test(void) {
    user_mode_spawn_app("demo", (uint32_t)user_test);
}

void user_mode_enter_test(void) {
    uint32_t stack_top = USER_STACK_TOP;

    arch_set_kernel_stack(0x90000);

    struct task* task = task_create_user_named("demo", (uint32_t)user_test, stack_top);
    if (task == 0) {
        terminal_write("ring3 failed: no task slot\n");
        return;
    }

    if (!prepare_user_test_memory(task->page_directory, stack_top)) {
        task_kill(task->id, 1);
        return;
    }

    task_set_user_memory(task, (uint32_t)&user_image_start, (uint32_t)&user_image_end,
        stack_top - USER_STACK_STRIDE, stack_top);

    terminal_write("Entering ring 3 task. User code will write, yield, and exit.\n");
    task_run(task);
}
