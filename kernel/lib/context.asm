[bits 32]

[global context_save]
[global context_restore]

context_save:
    mov edx, [esp + 4]
    mov [edx + 0], esp
    mov [edx + 4], ebp
    mov [edx + 8], ebx
    mov [edx + 12], esi
    mov [edx + 16], edi
    mov eax, [esp]
    mov [edx + 20], eax
    xor eax, eax
    ret

context_restore:
    mov edx, [esp + 4]
    mov esp, [edx + 0]
    mov ebp, [edx + 4]
    mov ebx, [edx + 8]
    mov esi, [edx + 12]
    mov edi, [edx + 16]
    mov eax, 1
    sti
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
