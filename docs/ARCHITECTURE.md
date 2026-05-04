# Kernel1 Architecture

Kernel1 is being organized as a portable OS ecosystem, not only a single x86 demo.

## Layers

- `kernel/core`: portable kernel logic: boot flow after arch setup, shell, syscalls, tasks, user-mode orchestration.
- `kernel/mm`: portable-ish memory managers: BIOS memory map reader, PMM, heap. Some inputs are still x86/BIOS specific and should later move behind arch APIs.
- `kernel/drivers`: hardware/device drivers currently used by the x86 build: VGA text terminal, PS/2 keyboard, PIT timer.
- `kernel/arch/x86`: x86-specific CPU/platform code: entry, GDT/TSS, IDT/ISR, PIC, paging, port I/O, ring-3 switch.
- `kernel/lib`: low-level helpers shared by the kernel, such as context save/restore.
- `user/demo`: linked-in user-mode demo program used while we do not yet load external apps.
- `boot`: current BIOS boot sector for the x86 target.

## Portability Rule

Code in `kernel/core` should not directly touch CPU registers, ports, segment selectors, page tables, or interrupt-controller details. Those belong in `kernel/arch/<target>` or `kernel/drivers`.

Over time, core code should call APIs like:

```c
arch_enable_interrupts();
arch_enter_user_mode(entry, stack);
arch_map_page(virt, phys, flags);
driver_console_write(text);
```

## Future Targets

Planned shape:

```text
kernel/arch/x86
kernel/arch/x86_64
kernel/arch/arm64
```

Each target provides the same small arch API while `kernel/core` stays shared.

## Current Stabilization State

- `kernel/core` now uses `arch_initialize`, `arch_map_page`, `arch_enter_user_mode`, `arch_halt`, and IRQ helpers instead of initializing x86 pieces directly.
- `SYS_WRITE` and `SYS_EXIT` are active.
- `SYS_YIELD` is reserved in the syscall ABI, but cooperative context switching is intentionally parked while the process context model is cleaned up.
- The current user app is still linked into the kernel image under `user/demo`; the next major ecosystem step is an initrd/app loader.

## Ecosystem Direction

The long-term macOS-like ecosystem needs:

- stable syscall ABI;
- process/app model;
- executable loader;
- filesystem and package/app bundles;
- graphics server/compositor;
- UI toolkit and SDK.
