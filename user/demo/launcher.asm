[bits 32]

[global launcher_app]
[extern user_write]
[extern user_write_dec]
[extern user_exec]
[extern user_wait]
[extern user_sleep]
[extern user_exit]

SYS_WAIT_RUNNING equ 0xFFFFFFFE

section .user_text
launcher_app:
    mov ecx, launcher_title
    mov edx, launcher_title_len
    call user_write

    mov ebx, launcher_child_name
    mov ecx, launcher_child_args
    call user_exec
    cmp eax, 0xFFFFFFFF
    je .spawn_failed

    mov esi, eax

    mov ecx, launcher_pid_msg
    mov edx, launcher_pid_msg_len
    call user_write
    mov ebx, esi
    call user_write_dec

.wait_loop:
    mov ebx, esi
    call user_wait
    cmp eax, SYS_WAIT_RUNNING
    jne .child_done

    mov ecx, launcher_waiting
    mov edx, launcher_waiting_len
    call user_write
    mov ebx, 20
    call user_sleep
    jmp .wait_loop

.child_done:
    mov edi, eax
    mov ecx, launcher_exit_msg
    mov edx, launcher_exit_msg_len
    call user_write
    mov ebx, edi
    call user_write_dec
    mov ebx, edi
    call user_exit

.spawn_failed:
    mov ecx, launcher_failed
    mov edx, launcher_failed_len
    call user_write
    mov ebx, 1
    call user_exit

section .user_rodata
launcher_title db "[launcher] SYS_EXEC/SYS_WAIT demo", 10
launcher_title_len equ $ - launcher_title
launcher_pid_msg db "[launcher] child pid:", 10
launcher_pid_msg_len equ $ - launcher_pid_msg
launcher_waiting db "[launcher] child still running", 10
launcher_waiting_len equ $ - launcher_waiting
launcher_exit_msg db "[launcher] child exit code:", 10
launcher_exit_msg_len equ $ - launcher_exit_msg
launcher_failed db "[launcher] exec failed", 10
launcher_failed_len equ $ - launcher_failed
launcher_child_name db "sleeper", 0
launcher_child_args db "", 0

section .note.GNU-stack noalloc noexec nowrite progbits
