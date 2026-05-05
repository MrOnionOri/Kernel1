[bits 32]

[global reader_app]
[extern user_write]
[extern user_open]
[extern user_read]
[extern user_close]
[extern user_getargs]
[extern user_exit]

READER_BUFFER_SIZE equ 96

section .user_text
reader_app:
    mov ecx, reader_title
    mov edx, reader_title_len
    call user_write

    mov ebx, reader_args
    mov ecx, READER_ARGS_SIZE
    call user_getargs
    cmp eax, 0
    jg .open_args

    mov ebx, reader_default_path
    jmp .open_file

.open_args:
    mov ebx, reader_args

.open_file:
    call user_open
    cmp eax, 0
    jl .open_failed

    mov esi, eax

.read_next:
    sub esp, READER_BUFFER_SIZE
    mov ebx, esi
    mov ecx, esp
    mov edx, READER_BUFFER_SIZE
    call user_read

    cmp eax, 0
    jl .read_failed
    je .done_reading

    mov ecx, esp
    mov edx, eax
    call user_write
    add esp, READER_BUFFER_SIZE
    jmp .read_next

.done_reading:
    add esp, READER_BUFFER_SIZE
    mov ebx, esi
    call user_close
    xor ebx, ebx
    call user_exit

.open_failed:
    mov ecx, reader_open_failed
    mov edx, reader_open_failed_len
    call user_write
    mov ebx, 1
    call user_exit

.read_failed:
    add esp, READER_BUFFER_SIZE
    mov ebx, esi
    call user_close
    mov ecx, reader_read_failed
    mov edx, reader_read_failed_len
    call user_write
    mov ebx, 2
    call user_exit

section .user_rodata
reader_title db "[reader] reading file", 10
reader_title_len equ $ - reader_title
reader_default_path db "readme.txt", 0
reader_open_failed db "[reader] open failed", 10
reader_open_failed_len equ $ - reader_open_failed
reader_read_failed db "[reader] read failed", 10
reader_read_failed_len equ $ - reader_read_failed
READER_ARGS_SIZE equ 64
reader_args times READER_ARGS_SIZE db 0

section .note.GNU-stack noalloc noexec nowrite progbits
