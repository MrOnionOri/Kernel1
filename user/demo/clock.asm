[bits 32]

[global clock_app]
[extern user_write]
[extern user_write_dec]
[extern user_getpid]
[extern user_ticks]
[extern user_exit]

section .user_text
clock_app:
    mov ecx, clock_title
    mov edx, clock_title_len
    call user_write

    mov ecx, clock_pid
    mov edx, clock_pid_len
    call user_write

    call user_getpid
    mov ebx, eax
    call user_write_dec

    mov ecx, clock_ticks
    mov edx, clock_ticks_len
    call user_write

    call user_ticks
    mov ebx, eax
    call user_write_dec

    xor ebx, ebx
    call user_exit

section .user_rodata
clock_title db "[clock] Clock app", 10
clock_title_len equ $ - clock_title
clock_pid db "[clock] pid:", 10
clock_pid_len equ $ - clock_pid
clock_ticks db "[clock] ticks:", 10
clock_ticks_len equ $ - clock_ticks

section .note.GNU-stack noalloc noexec nowrite progbits
