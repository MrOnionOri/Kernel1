# Kernel1 Architecture

Kernel1 is an educational 32-bit OS kernel built from a BIOS boot sector,
assembly support code, and freestanding C. The current direction is a portable
OS ecosystem: x86 is the first target, but the shared kernel code is being kept
behind small arch, driver, VFS, task, and app interfaces.

## Layers

- `boot`: BIOS boot sector. It loads the protected-mode kernel image and the
  initrd area into memory.
- `kernel/arch/x86`: x86 platform code: entry, GDT/TSS, IDT/ISR, PIC, paging,
  I/O ports, and user-mode switching.
- `kernel/core`: portable kernel logic: boot flow after arch setup, shell,
  app registry, KAPP loader, initrd, syscalls, tasks, user-mode orchestration,
  and VFS.
- `kernel/mm`: memory managers: BIOS memory-map ingestion, physical page
  allocator, and kernel heap.
- `kernel/drivers`: current device drivers: VGA text terminal, PS/2 keyboard,
  PIT timer, and a minimal ATA PIO data-disk probe/read path.
- `kernel/lib`: low-level helpers shared by the kernel.
- `user/lib`: tiny user-mode syscall helper library.
- `user/demo`: linked-in user-mode demo apps used for ring-3 and syscall tests.
- `apps`: `.kapp` app payload sources packaged into the initrd.

## Boot Flow

1. `boot/boot.asm` runs in BIOS real mode.
2. The boot sector loads the kernel image and initrd from the raw disk image.
3. The kernel enters 32-bit protected mode at `0x10000`.
4. `kernel/core/kernel.c` initializes arch, memory, drivers, tasks, VFS, initrd,
   app registry, and shell.
5. The interactive shell becomes the main control surface.

The build scripts reserve fixed disk-image space for the kernel and initrd. If
the kernel image grows, the sector constants in `build.sh`, `build.ps1`,
`boot/boot.asm`, and the initrd load address in `kernel/core/initrd.c` must stay
in sync.

## Portability Rule

Code in `kernel/core` should not directly touch CPU registers, ports, segment
selectors, page tables, or interrupt-controller details. Those belong in
`kernel/arch/<target>` or `kernel/drivers`.

Shared code should call APIs such as:

```c
arch_enable_interrupts();
arch_enter_user_mode(entry, stack);
arch_map_page(virt, phys, flags);
terminal_write(text);
```

Planned target shape:

```text
kernel/arch/x86
kernel/arch/x86_64
kernel/arch/arm64
```

Each target should provide the same small arch API while `kernel/core` stays
shared.

## Shell Architecture

The shell is now split into focused modules:

- `shell.c`: interactive input, cursor movement, command history, TAB
  completion dispatch, prompt, and high-level command dispatch.
- `shell_parser.c`: tokenization, quotes, arguments, and redirection markers
  (`>` and `>>`).
- `shell_env.c`: variables, aliases, `$?`, script arguments `$0..$4`, and
  variable/alias expansion.
- `shell_script.c`: `source`, `exit`, `foreach`, `ifset`, `ifnotset`, `ifeq`,
  and `ifneq`.
- `shell_fs.c`: filesystem commands and path state: `pwd`, `cd`, `ls`, `tree`,
  `cat`, `stat`, `du`, `find`, `mkdir`, `write`, `append`, `cp`, `mv`, and `rm`.
- `shell_apps.c`: app/process commands: `apps`, `appinfo`, `which`, `kapp`,
  `spawn`, `run`, `runall`, `kill`, `wait`, `reap`, `ps`, `tasks`, and
  `tasksv`, plus `schedtest` for a built-in busy/demo preemption smoke test.
- `shell_system.c`: diagnostic/system commands: `ticks`, `mem`, `pmm`, `alloc`,
  `heap`, `kmalloc`, `paging`, `vmmtest`, `gdt`, `ring3`, and `about`.

This keeps `shell.c` from becoming the owner of every feature. New command
families should generally get their own module and a small `*_handle_line`
function.

## VFS, Initrd, RAM, And Disk Files

The kernel has a VFS-style interface over initrd, the in-memory filesystem, and
the persistent KFS disk mount at `/disk`. It supports opening, reading, writing,
appending, copying, moving, removing, directory creation, path completion, and
tree/list/stat views.

The initrd uses format `K1RD2`:

```text
K1RD2\0
name\0
u32 size
data bytes
...
\0
```

Initrd files are loaded into the VFS under paths such as:

```text
apps/manifest.txt
apps/busy.txt
apps/hello.kapp
apps/echo.kapp
apps/logger.kapp
readme.txt
docs/kapp.txt
```

Writable files without a `/disk` prefix currently live in RAM. Files under
`/disk` are persisted into the QEMU-only KFS data image.

## Disk Work

The build scripts create a safe secondary raw image at `build/data.img` and pass
it to QEMU as IDE index 1. The kernel treats this as the data disk on ATA
primary slave.

Current disk commands:

```text
diskinfo         show ATA identify data
df               show KFS disk usage, including used/free sectors and bytes
diskread <lba>   dump one 512-byte sector in hex/ascii
diskwrite <lba> <text>
                 overwrite one sector with text and zero-fill the rest
kfsformat        write a KFS superblock and clear the directory area
kfscheck [-v]    verify the KFS superblock, bitmap, directory entries, and block chains
kfsinfo          show KFS superblock metadata
kfsls           list persistent KFS files
kfssave <name> <text>
                save one small text file into KFS
kfscat <name>   print one KFS file
kfsstat <name>  show one KFS file's slot, data sector, and size
kfsrm <name>    remove one KFS file and clear its data sector
```

KFS is also mounted into the normal VFS at `/disk`, so the shell can use common
commands on persistent files:

```text
ls /disk
mkdir /disk/docs
write /disk/note hello
append /disk/note " again"
write /disk/docs/note hello
cat /disk/note
stat /disk/note
du /disk
find /disk note
cp apps/hello.kapp /disk/hello.kapp
run /disk/hello.kapp
rm /disk/note
rm -r /disk/docs
```

This stage writes only to QEMU's secondary `build/data.img`, not to the boot
image or host disks. KFS v3 currently reserves sector 0 as the superblock,
sectors 1-4 as the directory area, sector 5 as the allocation bitmap, and
starts file data at sector 6. Directory entries are 64 bytes and can represent
either files or directories, giving the current directory area 32 entries and
paths up to 51 characters inside `/disk`. File entries store the first data
block; each data block stores a pointer to the next block followed by file
bytes, so files can use non-contiguous sectors. The current maximum is 4096
bytes per file.

## Apps And KAPP

There are two app sources:

- built-in ring-3 demos from `user/demo`;
- `.kapp` binaries from `apps` packaged into initrd.

The app registry is described by `apps/manifest.txt` in the initrd. Commands
such as `apps`, `appinfo`, and `which` inspect this registry. Commands such as
`spawn` and `run` start built-in or KAPP apps with arguments.

Current KAPP v1 shape:

```text
KAPP
u32 header_size
u32 entry_offset
u32 image_size
u32 flags
u32 version
u32 required_syscalls_mask
char name[32]
u32 reserved
payload bytes
```

The loader still accepts the older KAPP v0 20-byte header for compatibility,
but build scripts now emit v1 metadata for packaged apps.

## Tasks And User Mode

Tasks track:

- id and name;
- state: unused, ready, running, exited;
- entry address;
- user stack and kernel stack;
- exit code, yield count, and preemption count;
- command-line args;
- per-task file descriptors.
- saved user context snapshot: general registers, `eip`, `esp`, `eflags`,
  `cs`, and `ss` for scheduler/debug groundwork.

User-mode apps use `int 0x80` for syscalls. Foreground `run` executes a task
until it exits. `spawn` creates a ready task that can later be driven by
`runall`; `runall -a` keeps stepping the cooperative scheduler until no ready
tasks remain. `wait [-r] <id>` waits for one task and can reap it immediately,
`reap [id|-a]` clears exited task slots, and `ps -s` prints a compact
task-state summary. `sched [cooperative|auto|reset]` exposes the scheduler mode; auto
mode preempts on IRQ0 when the interrupt came from ring 3 and another task is
ready. Kernel-mode timer interrupts still avoid task switching.
`tasks`, `tasksv`, `ctx`, and `sched` expose preemption counters so IRQ0
switching can be verified without guessing from interleaved output alone.
`schedtest` spawns a no-yield `busy` task and two yielding `demo` tasks, enables
auto scheduling, runs until idle, and prints the scheduler summary. Use
`schedtest -r` to reap exited tasks after the test, and `schedtest -r -n N` to
repeat the smoke test for multiple rounds. It prints `PASS` only if all tasks
finish and at least one IRQ0 preemption was observed.
`ps -v` prints the saved context snapshot for each non-unused task, while
`ctx <id>` prints one task context in a more focused form. The x86 arch layer
can now enter ring 3 from a saved task context, which is the manual/cooperative
restore step needed before safe timer-driven preemption.

## Syscall ABI

Apps use:

```text
eax = syscall number
ebx/ecx/edx = arguments
eax = return value
```

Current syscalls:

```text
1  SYS_WRITE      ebx=string_c                  legacy/debug
2  SYS_EXIT       ebx=exit_code                 does not return to user
3  SYS_YIELD      save context and return to scheduler
4  SYS_WRITE_DEC  ebx=value                     decimal debug output
5  SYS_GETPID                                    returns pid in eax
6  SYS_TICKS                                     returns PIT ticks in eax
7  SYS_WRITE_BUF  ebx=fd ecx=buffer edx=len     returns bytes written or -1
8  SYS_OPEN       ebx=path                      returns fd or -1
9  SYS_READ       ebx=fd ecx=buffer edx=len     returns bytes read or -1
10 SYS_CLOSE      ebx=fd                        returns 0 or -1
11 SYS_GETARGS    ebx=buffer ecx=len            returns bytes copied
12 SYS_WRITE_FILE ebx=path ecx=string_c         returns 0 or -1
13 SYS_APPEND_FILE ebx=path ecx=string_c        returns 0 or -1
14 SYS_OPEN_FLAGS ebx=path ecx=flags            returns fd or -1
15 SYS_WRITE_FD   ebx=fd ecx=buffer edx=len     returns bytes written or -1
16 SYS_MKDIR      ebx=path                      returns 0 or -1
```

The syscall layer validates user pointers against the `.user` image and user
stack ranges before reading or writing buffers.

## Current State

Working pieces:

- BIOS boot into 32-bit protected mode.
- GDT/TSS, IDT/ISR, PIC, paging, and ring-3 entry.
- VGA terminal, keyboard input, PIT ticks.
- PMM and kernel heap.
- Shell with history, cursor editing, TAB completion, variables, aliases,
  redirection, conditionals, loops, and scripts.
- VFS over initrd, RAM files, and the `/disk` KFS mount.
- Built-in user apps, including `demo` with `SYS_YIELD` and `busy` without
  yield, plus KAPP apps.
- Basic task table and foreground/background execution flow with wait, kill,
  reap, task summaries, and saved context snapshots.

## Next Work

Recommended next steps:

1. Add dynamically growing directories and longer file support.
2. Harden IRQ0 preemption with longer stress tests and cleaner accounting.
3. After disk/process basics are stable, begin graphics groundwork: framebuffer,
   mouse/events, and a small UI server.
