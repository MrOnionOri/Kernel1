; 32-bit entry point reached from the boot sector.

[bits 32]
[global _start]
[extern kernel_main]

_start:
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
