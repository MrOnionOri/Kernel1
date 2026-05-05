[bits 32]

[global user_write]
[global user_write_dec]
[global user_getpid]
[global user_ticks]
[global user_exit]

SYS_EXIT equ 2
SYS_WRITE_DEC equ 4
SYS_GETPID equ 5
SYS_TICKS equ 6
SYS_WRITE_BUF equ 7
STDOUT_FD equ 1

section .user_text
user_write:
    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    int 0x80
    ret

user_write_dec:
    mov eax, SYS_WRITE_DEC
    int 0x80
    ret

user_getpid:
    mov eax, SYS_GETPID
    int 0x80
    ret

user_ticks:
    mov eax, SYS_TICKS
    int 0x80
    ret

user_exit:
    mov eax, SYS_EXIT
    int 0x80

.halt:
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
