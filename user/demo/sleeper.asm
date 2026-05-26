[bits 32]

[global sleeper_app]
[extern user_write]
[extern user_write_dec]
[extern user_ticks]
[extern user_sleep]
[extern user_exit]

section .user_text
sleeper_app:
    mov ecx, sleeper_start
    mov edx, sleeper_start_len
    call user_write

    call user_ticks
    mov ebx, eax
    call user_write_dec

    mov ebx, 50
    call user_sleep

    mov ecx, sleeper_done
    mov edx, sleeper_done_len
    call user_write

    call user_ticks
    mov ebx, eax
    call user_write_dec

    xor ebx, ebx
    call user_exit

section .user_rodata
sleeper_start db "[sleeper] before SYS_SLEEP", 10
sleeper_start_len equ $ - sleeper_start
sleeper_done db "[sleeper] after SYS_SLEEP", 10
sleeper_done_len equ $ - sleeper_done

section .note.GNU-stack noalloc noexec nowrite progbits
