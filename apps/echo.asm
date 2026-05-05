[bits 32]

SYS_EXIT equ 2
SYS_WRITE_BUF equ 7
SYS_GETARGS equ 11
STDOUT_FD equ 1
ARGS_SIZE equ 64

start:
    call .base
.base:
    pop esi

    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + prefix - .base]
    mov edx, prefix_len
    int 0x80

    mov eax, SYS_GETARGS
    lea ebx, [esi + args_buffer - .base]
    mov ecx, ARGS_SIZE
    int 0x80

    cmp eax, 0
    jg .write_args

    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + empty_message - .base]
    mov edx, empty_message_len
    int 0x80
    jmp .exit

.write_args:
    mov edx, eax
    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + args_buffer - .base]
    int 0x80

    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + newline - .base]
    mov edx, 1
    int 0x80

.exit:
    mov eax, SYS_EXIT
    xor ebx, ebx
    int 0x80

.halt:
    jmp .halt

prefix db "[echo.kapp] "
prefix_len equ $ - prefix
empty_message db "(no args)", 10
empty_message_len equ $ - empty_message
newline db 10
args_buffer times ARGS_SIZE db 0
