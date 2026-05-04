#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
KERNEL_SECTORS=64
KERNEL_BYTES=$((KERNEL_SECTORS * 512))
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

echo "Built $IMAGE_PATH"

if [[ "$RUN" -eq 1 ]]; then
    require_command qemu-system-i386
    qemu-system-i386 -drive format=raw,file="$IMAGE_PATH"
fi
