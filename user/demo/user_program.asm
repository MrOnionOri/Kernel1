[bits 32]

[global user_test]
[extern user_write]
[extern user_write_dec]
[extern user_getpid]
[extern user_ticks]
[extern user_exit]

section .user_text
user_test:
    mov ecx, user_message_1
    mov edx, user_message_1_len
    call user_write

    call user_getpid
    mov ebx, eax
    call user_write_dec

    call user_ticks
    mov ebx, eax
    call user_write_dec

    mov ecx, user_message_2
    mov edx, user_message_2_len
    call user_write

    mov ecx, user_message_3
    mov edx, user_message_3_len
    call user_write

    xor ebx, ebx
    call user_exit

section .user_rodata
user_message_1 db "[demo] Hello from ring 3 step 1", 10
user_message_1_len equ $ - user_message_1
user_message_2 db "[demo] Hello from ring 3 step 2", 10
user_message_2_len equ $ - user_message_2
user_message_3 db "[demo] Hello from ring 3 done", 10
user_message_3_len equ $ - user_message_3

section .note.GNU-stack noalloc noexec nowrite progbits
