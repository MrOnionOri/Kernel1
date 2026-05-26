[bits 32]

[global selfmod_app]
[extern user_write]
[extern user_exit]

section .user_text
selfmod_app:
    mov ecx, selfmod_start
    mov edx, selfmod_start_len
    call user_write

    mov byte [selfmod_app], 0x90

    mov ecx, selfmod_failed
    mov edx, selfmod_failed_len
    call user_write

    mov ebx, 1
    call user_exit

section .user_rodata
selfmod_start db "[selfmod] trying to write built-in user code", 10
selfmod_start_len equ $ - selfmod_start
selfmod_failed db "[selfmod] unexpected write succeeded", 10
selfmod_failed_len equ $ - selfmod_failed

section .note.GNU-stack noalloc noexec nowrite progbits
