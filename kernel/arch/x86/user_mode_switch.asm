[bits 32]

[global user_mode_switch]

user_mode_switch:
    mov eax, [esp + 4]
    mov ebx, [esp + 8]

    cli
    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push dword 0x23
    push ebx
    pushfd
    pop ecx
    or ecx, 0x200
    push ecx
    push dword 0x1B
    push eax
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
