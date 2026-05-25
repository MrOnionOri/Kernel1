[bits 32]

[global busy_app]
[extern user_write]
[extern user_write_dec]
[extern user_ticks]
[extern user_exit]

section .user_text
busy_app:
    mov ecx, busy_start
    mov edx, busy_start_len
    call user_write

    call user_ticks
    mov ebx, eax
    call user_write_dec

    mov ecx, 0x00300000
.loop:
    dec ecx
    jnz .loop

    mov ecx, busy_done
    mov edx, busy_done_len
    call user_write

    call user_ticks
    mov ebx, eax
    call user_write_dec

    xor ebx, ebx
    call user_exit

section .user_rodata
busy_start db "[busy] start no-yield loop", 10
busy_start_len equ $ - busy_start
busy_done db "[busy] done", 10
busy_done_len equ $ - busy_done

section .note.GNU-stack noalloc noexec nowrite progbits
