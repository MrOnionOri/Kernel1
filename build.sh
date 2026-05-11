#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
KERNEL_SECTORS=160
KERNEL_BYTES=$((KERNEL_SECTORS * 512))
INITRD_SECTORS=16
INITRD_BYTES=$((INITRD_SECTORS * 512))
IMAGE_PATH="$BUILD_DIR/kernel1.img"

RUN=0
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        run|--run|-r)
            RUN=1
            ;;
        clean|--clean|-c)
            CLEAN=1
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: ./build.sh [run|clean]" >&2
            exit 1
            ;;
    esac
done

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf "$BUILD_DIR"
    exit 0
fi

require_command nasm
require_command gcc
require_command ld
require_command objcopy

mkdir -p "$BUILD_DIR"

nasm -f bin boot/boot.asm -o "$BUILD_DIR/boot.bin"

OBJECTS=()
INCLUDE_FLAGS=(
    -Ikernel/core
    -Ikernel/mm
    -Ikernel/drivers
    -Ikernel/arch/x86
    -Ikernel/lib
)

for asm_file in kernel/arch/x86/entry.asm; do
    object_file="$BUILD_DIR/$(basename "${asm_file%.asm}.o")"
    nasm -f elf32 "$asm_file" -o "$object_file"
    OBJECTS+=("$object_file")
done

while IFS= read -r asm_file; do
    if [[ "$(basename "$asm_file")" == "entry.asm" ]]; then
        continue
    fi

    object_file="$BUILD_DIR/$(echo "${asm_file%.asm}" | tr '/\\' '__').o"
    nasm -f elf32 "$asm_file" -o "$object_file"
    OBJECTS+=("$object_file")
done < <(find kernel user -name '*.asm' | sort)

while IFS= read -r c_file; do
    object_file="$BUILD_DIR/$(echo "${c_file%.c}" | tr '/\\' '__').o"
    gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib \
        "${INCLUDE_FLAGS[@]}" -Wall -Wextra -c "$c_file" -o "$object_file"
    OBJECTS+=("$object_file")
done < <(find kernel -name '*.c' | sort)

ld -m elf_i386 -T linker.ld -nostdlib \
    "${OBJECTS[@]}" -o "$BUILD_DIR/kernel.elf"

objcopy -O binary "$BUILD_DIR/kernel.elf" "$BUILD_DIR/kernel.bin"
nasm -f bin apps/hello.asm -o "$BUILD_DIR/hello.payload"
nasm -f bin apps/echo.asm -o "$BUILD_DIR/echo.payload"
nasm -f bin apps/logger.asm -o "$BUILD_DIR/logger.payload"

write_u32() {
    local value="$1"
    printf "\\$(printf '%03o' $((value & 0xFF)))"
    printf "\\$(printf '%03o' $(((value >> 8) & 0xFF)))"
    printf "\\$(printf '%03o' $(((value >> 16) & 0xFF)))"
    printf "\\$(printf '%03o' $(((value >> 24) & 0xFF)))"
}

append_record() {
    local name="$1"
    local file="$2"
    local size
    size=$(wc -c < "$file")
    printf '%s\0' "$name" >> "$BUILD_DIR/initrd.bin"
    write_u32 "$size" >> "$BUILD_DIR/initrd.bin"
    cat "$file" >> "$BUILD_DIR/initrd.bin"
}

printf 'K1RD2\0' > "$BUILD_DIR/initrd.bin"
printf 'demo is currently linked into the kernel image.\nNext: load this app from initrd.\n' > "$BUILD_DIR/demo.txt"
printf 'clock is currently linked into the kernel image.\nNext: load this app from initrd.\n' > "$BUILD_DIR/clock.txt"
printf 'reader opens files through SYS_OPEN/SYS_READ/SYS_CLOSE.\n' > "$BUILD_DIR/reader.txt"
printf 'demo|built-in|Demo ring3 app\nclock|built-in|Shows PID and ticks\nreader|built-in|Reads files through VFS syscalls\nhello|kapp|Hello from initrd\necho|kapp|Prints arguments\nlogger|kapp|Appends args to tmp/app.log\n' > "$BUILD_DIR/manifest.txt"
printf 'Kernel1 initrd v2: name + u32 size + binary-safe data records.\n' > "$BUILD_DIR/readme.txt"
printf 'KAPP v0: magic KAPP, u32 header size, u32 entry offset, u32 image size, u32 flags, payload.\n' > "$BUILD_DIR/kapp.txt"
printf 'KAPP' > "$BUILD_DIR/hello.kapp"
write_u32 20 >> "$BUILD_DIR/hello.kapp"
write_u32 0 >> "$BUILD_DIR/hello.kapp"
write_u32 "$(wc -c < "$BUILD_DIR/hello.payload")" >> "$BUILD_DIR/hello.kapp"
write_u32 0 >> "$BUILD_DIR/hello.kapp"
cat "$BUILD_DIR/hello.payload" >> "$BUILD_DIR/hello.kapp"
printf 'KAPP' > "$BUILD_DIR/echo.kapp"
write_u32 20 >> "$BUILD_DIR/echo.kapp"
write_u32 0 >> "$BUILD_DIR/echo.kapp"
write_u32 "$(wc -c < "$BUILD_DIR/echo.payload")" >> "$BUILD_DIR/echo.kapp"
write_u32 0 >> "$BUILD_DIR/echo.kapp"
cat "$BUILD_DIR/echo.payload" >> "$BUILD_DIR/echo.kapp"
printf 'KAPP' > "$BUILD_DIR/logger.kapp"
write_u32 20 >> "$BUILD_DIR/logger.kapp"
write_u32 0 >> "$BUILD_DIR/logger.kapp"
write_u32 "$(wc -c < "$BUILD_DIR/logger.payload")" >> "$BUILD_DIR/logger.kapp"
write_u32 0 >> "$BUILD_DIR/logger.kapp"
cat "$BUILD_DIR/logger.payload" >> "$BUILD_DIR/logger.kapp"

append_record "apps/demo.txt" "$BUILD_DIR/demo.txt"
append_record "apps/clock.txt" "$BUILD_DIR/clock.txt"
append_record "apps/reader.txt" "$BUILD_DIR/reader.txt"
append_record "apps/manifest.txt" "$BUILD_DIR/manifest.txt"
append_record "apps/hello.kapp" "$BUILD_DIR/hello.kapp"
append_record "apps/echo.kapp" "$BUILD_DIR/echo.kapp"
append_record "apps/logger.kapp" "$BUILD_DIR/logger.kapp"
append_record "readme.txt" "$BUILD_DIR/readme.txt"
append_record "docs/kapp.txt" "$BUILD_DIR/kapp.txt"
printf '\0' >> "$BUILD_DIR/initrd.bin"

INITRD_SIZE=$(wc -c < "$BUILD_DIR/initrd.bin")
if [[ "$INITRD_SIZE" -gt "$INITRD_BYTES" ]]; then
    echo "Initrd is $INITRD_SIZE bytes, but reserved space is $INITRD_BYTES bytes." >&2
    exit 1
fi

BOOT_SIZE=$(wc -c < "$BUILD_DIR/boot.bin")
KERNEL_SIZE=$(wc -c < "$BUILD_DIR/kernel.bin")

if [[ "$BOOT_SIZE" -ne 512 ]]; then
    echo "Boot sector must be exactly 512 bytes. Current size: $BOOT_SIZE" >&2
    exit 1
fi

if [[ "$KERNEL_SIZE" -gt "$KERNEL_BYTES" ]]; then
    echo "Kernel is $KERNEL_SIZE bytes, but bootloader loads only $KERNEL_BYTES bytes." >&2
    exit 1
fi

cat "$BUILD_DIR/boot.bin" "$BUILD_DIR/kernel.bin" > "$IMAGE_PATH"
truncate -s $((512 + KERNEL_BYTES)) "$IMAGE_PATH"
cat "$BUILD_DIR/initrd.bin" >> "$IMAGE_PATH"
truncate -s $((512 + KERNEL_BYTES + INITRD_BYTES)) "$IMAGE_PATH"

echo "Built $IMAGE_PATH"

if [[ "$RUN" -eq 1 ]]; then
    require_command qemu-system-i386
    qemu-system-i386 -drive format=raw,file="$IMAGE_PATH"
fi
