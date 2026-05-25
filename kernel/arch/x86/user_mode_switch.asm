[bits 32]

[global user_mode_switch]
[global user_context_switch]

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

; void user_context_switch(const struct task_context* context)
; Offsets must match kernel/core/task.h::struct task_context.
user_context_switch:
    mov ebp, [esp + 4]

    cli
    mov ax, [ebp + 44]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword [ebp + 44] ; ss
    push dword [ebp + 28] ; user esp
    mov ecx, [ebp + 36]   ; eflags
    or ecx, 0x200
    push ecx
    push dword [ebp + 40] ; cs
    push dword [ebp + 32] ; eip

    mov eax, [ebp + 0]
    mov ebx, [ebp + 4]
    mov ecx, [ebp + 8]
    mov edx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
