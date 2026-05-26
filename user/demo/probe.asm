[bits 32]

[global probe_app]
[extern user_write]
[extern user_exit]

section .user_text
probe_app:
    mov ecx, probe_start
    mov edx, probe_start_len
    call user_write

    mov eax, [0x02008000]

    mov ecx, probe_failed
    mov edx, probe_failed_len
    call user_write

    mov ebx, 1
    call user_exit

section .user_rodata
probe_start db "[probe] touching another task stack page", 10
probe_start_len equ $ - probe_start
probe_failed db "[probe] unexpected access succeeded", 10
probe_failed_len equ $ - probe_failed

section .note.GNU-stack noalloc noexec nowrite progbits
