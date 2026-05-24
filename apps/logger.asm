[bits 32]

SYS_EXIT equ 2
SYS_WRITE_BUF equ 7
SYS_GETARGS equ 11
SYS_OPEN_FLAGS equ 14
SYS_WRITE_FD equ 15
SYS_MKDIR equ 16
SYS_CLOSE equ 10
STDOUT_FD equ 1
VFS_O_WRITE equ 0x02
VFS_O_CREATE equ 0x04
VFS_O_APPEND equ 0x10
LOG_FLAGS equ VFS_O_WRITE | VFS_O_CREATE | VFS_O_APPEND
ARGS_SIZE equ 96
PATH_SIZE equ 72

start:
    call .base
.base:
    pop esi

    mov eax, SYS_GETARGS
    lea ebx, [esi + args_buffer - .base]
    mov ecx, ARGS_SIZE
    int 0x80

    cmp eax, 0
    jg .have_args

    lea edi, [esi + default_message - .base]
    mov ebp, default_message_len
    lea eax, [esi + log_path - .base]
    mov [esi + active_path - .base], eax
    jmp .prepare_log

.have_args:
    mov byte [esi + args_buffer - .base + eax], 0
    lea edi, [esi + args_buffer - .base]
    mov ebp, eax
    lea ecx, [esi + log_path - .base]
    mov [esi + active_path - .base], ecx
    call .parse_path_option

.prepare_log:
    call .mkdir_parent

    mov eax, SYS_OPEN_FLAGS
    mov ebx, [esi + active_path - .base]
    mov ecx, LOG_FLAGS
    int 0x80
    cmp eax, -1
    je .fail

    mov [esi + log_fd - .base], eax

    mov ebx, eax
    lea ecx, [esi + log_prefix - .base]
    mov edx, log_prefix_len
    call .write_part
    cmp eax, -1
    je .close_fail

    mov ebx, [esi + log_fd - .base]
    mov ecx, edi
    mov edx, ebp
    call .write_part
    cmp eax, -1
    je .close_fail

    mov ebx, [esi + log_fd - .base]
    lea ecx, [esi + newline - .base]
    mov edx, 1
    call .write_part
    cmp eax, -1
    je .close_fail

    mov eax, SYS_CLOSE
    mov ebx, [esi + log_fd - .base]
    int 0x80

    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + ok_message - .base]
    mov edx, ok_message_len
    int 0x80
    xor ebx, ebx
    jmp .exit

.close_fail:
    mov eax, SYS_CLOSE
    mov ebx, [esi + log_fd - .base]
    int 0x80

.fail:
    mov eax, SYS_WRITE_BUF
    mov ebx, STDOUT_FD
    lea ecx, [esi + fail_message - .base]
    mov edx, fail_message_len
    int 0x80
    mov ebx, 1

.exit:
    mov eax, SYS_EXIT
    int 0x80

.halt:
    jmp .halt

.write_part:
    mov eax, SYS_WRITE_FD
    int 0x80
    ret

.parse_path_option:
    cmp byte [edi], 'p'
    jne .parse_done
    cmp byte [edi + 1], 'a'
    jne .parse_done
    cmp byte [edi + 2], 't'
    jne .parse_done
    cmp byte [edi + 3], 'h'
    jne .parse_done
    cmp byte [edi + 4], '='
    jne .parse_done

    lea ebx, [edi + 5]
    lea edx, [esi + path_buffer - .base]
    xor ecx, ecx

.copy_path:
    cmp byte [ebx], 0
    je .path_copied
    cmp byte [ebx], ' '
    je .path_copied
    cmp ecx, PATH_SIZE - 1
    jae .path_copied
    mov al, [ebx]
    mov [edx + ecx], al
    inc ebx
    inc ecx
    jmp .copy_path

.path_copied:
    cmp ecx, 0
    je .parse_done
    mov byte [edx + ecx], 0
    lea eax, [esi + path_buffer - .base]
    mov [esi + active_path - .base], eax

.skip_spaces:
    cmp byte [ebx], ' '
    jne .set_message
    inc ebx
    jmp .skip_spaces

.set_message:
    cmp byte [ebx], 0
    jne .set_custom_message
    lea edi, [esi + default_message - .base]
    mov ebp, default_message_len
    jmp .parse_done

.set_custom_message:
    mov edi, ebx
    xor ebp, ebp

.count_message:
    cmp byte [edi + ebp], 0
    je .parse_done
    inc ebp
    jmp .count_message

.parse_done:
    ret

.mkdir_parent:
    mov ebx, [esi + active_path - .base]
    xor edx, edx
    mov ecx, ebx

.scan_parent:
    cmp byte [ecx], 0
    je .parent_scanned
    cmp byte [ecx], '/'
    jne .next_parent_char
    mov edx, ecx
.next_parent_char:
    inc ecx
    jmp .scan_parent

.parent_scanned:
    cmp edx, 0
    je .mkdir_default
    cmp edx, ebx
    je .mkdir_default

    lea edi, [esi + dir_buffer - .base]
    mov ecx, ebx
    xor eax, eax

.copy_parent:
    cmp ecx, edx
    je .parent_ready
    cmp eax, PATH_SIZE - 1
    jae .parent_ready
    mov bl, [ecx]
    mov [edi + eax], bl
    inc ecx
    inc eax
    jmp .copy_parent

.parent_ready:
    mov byte [edi + eax], 0
    mov eax, SYS_MKDIR
    mov ebx, edi
    int 0x80
    ret

.mkdir_default:
    mov eax, SYS_MKDIR
    lea ebx, [esi + log_dir - .base]
    int 0x80
    ret

log_fd dd 0
active_path dd 0
log_path db "tmp/app.log", 0
log_prefix db "[logger] "
log_prefix_len equ $ - log_prefix
default_message db "(no message)"
default_message_len equ $ - default_message
newline db 10
ok_message db "[logger.kapp] wrote log via fd", 10
ok_message_len equ $ - ok_message
fail_message db "[logger.kapp] failed to write log", 10
fail_message_len equ $ - fail_message
log_dir db "tmp", 0
path_buffer times PATH_SIZE db 0
dir_buffer times PATH_SIZE db 0
args_buffer times ARGS_SIZE + 1 db 0
