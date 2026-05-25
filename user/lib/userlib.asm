[bits 32]

[global user_write]
[global user_write_dec]
[global user_yield]
[global user_getpid]
[global user_ticks]
[global user_open]
[global user_open_flags]
[global user_read]
[global user_write_fd]
[global user_close]
[global user_getargs]
[global user_write_file]
[global user_append_file]
[global user_mkdir]
[global user_exit]

SYS_EXIT equ 2
SYS_YIELD equ 3
SYS_WRITE_DEC equ 4
SYS_GETPID equ 5
SYS_TICKS equ 6
SYS_WRITE_BUF equ 7
SYS_OPEN equ 8
SYS_READ equ 9
SYS_CLOSE equ 10
SYS_GETARGS equ 11
SYS_WRITE_FILE equ 12
SYS_APPEND_FILE equ 13
SYS_OPEN_FLAGS equ 14
SYS_WRITE_FD equ 15
SYS_MKDIR equ 16
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

user_yield:
    mov eax, SYS_YIELD
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

user_open:
    mov eax, SYS_OPEN
    int 0x80
    ret

user_open_flags:
    mov eax, SYS_OPEN_FLAGS
    int 0x80
    ret

user_read:
    mov eax, SYS_READ
    int 0x80
    ret

user_write_fd:
    mov eax, SYS_WRITE_FD
    int 0x80
    ret

user_close:
    mov eax, SYS_CLOSE
    int 0x80
    ret

user_getargs:
    mov eax, SYS_GETARGS
    int 0x80
    ret

user_write_file:
    mov eax, SYS_WRITE_FILE
    int 0x80
    ret

user_append_file:
    mov eax, SYS_APPEND_FILE
    int 0x80
    ret

user_mkdir:
    mov eax, SYS_MKDIR
    int 0x80
    ret

user_exit:
    mov eax, SYS_EXIT
    int 0x80

.halt:
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
