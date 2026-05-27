; 512-byte BIOS boot sector.
; Loads the kernel from disk to 0x1000, enters 32-bit protected mode,
; and jumps to the assembly kernel entry point.

[org 0x7c00]
[bits 16]

KERNEL_OFFSET equ 0x10000
KERNEL_LOAD_SEGMENT equ 0x1000
KERNEL_SECTORS equ 324
KERNEL_READ_CHUNK equ 8
MEMORY_MAP_ADDR equ 0x9000
MEMORY_MAP_ENTRIES equ MEMORY_MAP_ADDR + 4
MEMORY_MAP_MAX equ 16
GRAPHICS_INFO_ADDR equ 0x8F00
VBE_MODE_INFO_ADDR equ 0x8000
VBE_PROBE_MODE equ 0x144

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    mov [boot_drive], dl
    call clear_graphics_info
    call probe_vbe_mode
    call activate_vbe_mode

    cmp dword [GRAPHICS_INFO_ADDR], 0x4647314B
    je .skip_text_mode
    call set_text_mode_80x50

.skip_text_mode:
    call get_memory_map
    call load_kernel
    call enter_protected_mode

hang:
    jmp hang

set_text_mode_80x50:
    mov ax, 0x0003
    int 0x10
    mov ax, 0x1112
    xor bx, bx
    int 0x10
    ret

clear_graphics_info:
    mov di, GRAPHICS_INFO_ADDR
    xor ax, ax
    mov cx, 24
    rep stosb
    ret

probe_vbe_mode:
    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, VBE_PROBE_MODE
    mov di, VBE_MODE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .done
    mov bx, [VBE_MODE_INFO_ADDR]
    test bx, 0x0080
    jz .done
    cmp byte [VBE_MODE_INFO_ADDR + 25], 32
    jne .done
    mov dword [GRAPHICS_INFO_ADDR], 0x5047314B
    mov eax, [VBE_MODE_INFO_ADDR + 40]
    mov [GRAPHICS_INFO_ADDR + 4], eax
    xor eax, eax
    mov ax, [VBE_MODE_INFO_ADDR + 18]
    mov [GRAPHICS_INFO_ADDR + 8], eax
    mov ax, [VBE_MODE_INFO_ADDR + 20]
    mov [GRAPHICS_INFO_ADDR + 12], eax
    mov ax, [VBE_MODE_INFO_ADDR + 16]
    mov [GRAPHICS_INFO_ADDR + 16], eax
    movzx eax, byte [VBE_MODE_INFO_ADDR + 25]
    mov [GRAPHICS_INFO_ADDR + 20], eax
.done:
    pop es
    ret

activate_vbe_mode:
    cmp dword [GRAPHICS_INFO_ADDR], 0x5047314B
    jne .done
    mov ax, 0x4F02
    mov bx, VBE_PROBE_MODE | 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .done
    mov dword [GRAPHICS_INFO_ADDR], 0x4647314B
.done:
    ret

load_kernel:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov word [dap_buffer_offset], 0x0000
    mov word [dap_buffer_segment], KERNEL_LOAD_SEGMENT
    mov word [dap_start_lba], 1
    mov word [sectors_remaining], KERNEL_SECTORS

.next_chunk:
    cmp word [sectors_remaining], 0
    je .done

    mov ax, [sectors_remaining]
    cmp ax, KERNEL_READ_CHUNK
    jbe .set_count
    mov ax, KERNEL_READ_CHUNK

.set_count:
    mov [dap_sector_count], ax

    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    mov ax, [dap_sector_count]
    sub [sectors_remaining], ax

    shl ax, 9
    add [dap_buffer_offset], ax
    jnc .advance_lba
    add word [dap_buffer_segment], 0x1000

.advance_lba:
    mov ax, [dap_sector_count]
    add [dap_start_lba], ax
    jmp .next_chunk

.done:
    ret

disk_error:
    jmp hang

get_memory_map:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov es, ax
    xor ebx, ebx
    xor bp, bp
    mov dword [MEMORY_MAP_ADDR], 0
    mov di, MEMORY_MAP_ENTRIES
.next:
    cmp bp, MEMORY_MAP_MAX
    jae .done
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534d4150
    jne .done
    test ecx, ecx
    jz .done
    inc bp
    add di, 24
    test ebx, ebx
    jne .next
.done:
    mov [MEMORY_MAP_ADDR], bp
    pop es
    pop ds
    ret

enter_protected_mode:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:init_pm

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    call KERNEL_OFFSET

pm_hang:
    jmp pm_hang

gdt_start:
    dq 0x0000000000000000

gdt_code:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

boot_drive db 0

disk_address_packet:
    db 0x10
    db 0x00
dap_sector_count:
    dw 0
dap_buffer_offset:
    dw 0
dap_buffer_segment:
    dw 0
dap_start_lba:
    dq 0

sectors_remaining dw 0

times 510 - ($ - $$) db 0
dw 0xaa55
