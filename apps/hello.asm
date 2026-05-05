[bits 32]

SYS_EXIT equ 2
SYS_WRITE_BUF equ 7
STDOUT_FD equ 1

start:
    call .base
.base:
    pop esi
    add esi, hello_message - .base

    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    mov ecx, esi
    mov edx, hello_message_len
    int 0x80

    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80

.halt:
    jmp .halt

hello_message db "[hello.kapp] Hello from initrd app", 10
hello_message_len equ $ - hello_message
