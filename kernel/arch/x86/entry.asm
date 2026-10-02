; 32-bit entry point reached from the boot sector.

[bits 32]
[global _start]
[extern kernel_main]
[extern bss_start]
[extern bss_end]

_start:
    cld
    ; ESI points to the initrd loaded immediately after the kernel sectors.
    mov edi, 0x80000
    mov ecx, 8192 / 4
    rep movsd
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    mov esp, stack_top
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
[global stack_top]
stack_bottom:
    resb 16384
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
