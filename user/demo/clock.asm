[bits 32]

[global clock_app]

section .user_text
clock_app:
    mov eax, 1
    mov ebx, clock_title
    int 0x80

    mov eax, 1
    mov ebx, clock_pid
    int 0x80

    mov eax, 5
    int 0x80
    mov ebx, eax
    mov eax, 4
    int 0x80

    mov eax, 1
    mov ebx, clock_ticks
    int 0x80

    mov eax, 6
    int 0x80
    mov ebx, eax
    mov eax, 4
    int 0x80

    mov eax, 2
    xor ebx, ebx
    int 0x80

.halt:
    jmp .halt

section .user_rodata
clock_title db "Clock app", 0
clock_pid db "pid:", 0
clock_ticks db "ticks:", 0

section .note.GNU-stack noalloc noexec nowrite progbits
