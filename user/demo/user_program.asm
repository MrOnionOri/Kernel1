[bits 32]

[global user_test]

section .user_text
user_test:
    mov eax, 1
    mov ebx, user_message_1
    int 0x80

    mov eax, 3
    xor ebx, ebx
    int 0x80

    mov eax, 1
    mov ebx, user_message_2
    int 0x80

    mov eax, 3
    xor ebx, ebx
    int 0x80

    mov eax, 1
    mov ebx, user_message_3
    int 0x80

    mov eax, 2
    xor ebx, ebx
    int 0x80

.halt:
    jmp .halt

section .user_rodata
user_message_1 db "Hello from ring 3 step 1", 0
user_message_2 db "Hello from ring 3 step 2", 0
user_message_3 db "Hello from ring 3 done", 0

section .note.GNU-stack noalloc noexec nowrite progbits
