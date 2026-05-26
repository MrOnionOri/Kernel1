[bits 32]

[global lsapp_app]
[extern user_write]
[extern user_getargs]
[extern user_stat]
[extern user_readdir]
[extern user_exit]

LSAPP_ARGS_SIZE equ 64
VFS_STAT_SIZE equ 24
VFS_DIRENT_SIZE equ 76
VFS_DIRENT_NAME equ 0
VFS_DIRENT_TYPE equ 64
VFS_DIRENT_SIZE_FIELD equ 68
VFS_NODE_DIRECTORY equ 2
VFS_STAT_TYPE equ 0
VFS_STAT_SIZE_FIELD equ 4
VFS_STAT_CHILDREN equ 20

section .user_text
lsapp_app:
    sub esp, LSAPP_ARGS_SIZE
    mov esi, esp

    mov ebx, esi
    mov ecx, LSAPP_ARGS_SIZE
    call user_getargs
    cmp eax, 0
    jg .have_path

    mov esi, lsapp_root

.have_path:
    mov ecx, lsapp_title
    mov edx, lsapp_title_len
    call user_write

    mov ecx, lsapp_path
    mov edx, lsapp_path_len
    call user_write
    mov ecx, esi
    call write_cstr
    call write_newline

    sub esp, VFS_STAT_SIZE
    mov edi, esp
    mov ebx, esi
    mov ecx, edi
    call user_stat
    cmp eax, 0
    jl .stat_failed

    cmp dword [edi + VFS_STAT_TYPE], VFS_NODE_DIRECTORY
    jne .stat_file

    mov ecx, lsapp_type_dir
    mov edx, lsapp_type_dir_len
    call user_write
    jmp .print_stat_size

.stat_file:
    mov ecx, lsapp_type_file
    mov edx, lsapp_type_file_len
    call user_write

.print_stat_size:
    mov ecx, lsapp_size
    mov edx, lsapp_size_len
    call user_write
    mov ebx, [edi + VFS_STAT_SIZE_FIELD]
    call write_uint
    mov ecx, lsapp_bytes
    mov edx, lsapp_bytes_len
    call user_write

    mov ecx, lsapp_children
    mov edx, lsapp_children_len
    call user_write
    mov ebx, [edi + VFS_STAT_CHILDREN]
    call write_uint_line

    sub esp, VFS_DIRENT_SIZE
    mov edi, esp
    xor ebp, ebp

.next_entry:
    mov ebx, esi
    mov ecx, ebp
    mov edx, edi
    call user_readdir
    cmp eax, 0
    jl .done_entries
    je .done_entries

    cmp dword [edi + VFS_DIRENT_TYPE], VFS_NODE_DIRECTORY
    jne .file_entry

    mov ecx, lsapp_dir_prefix
    mov edx, lsapp_dir_prefix_len
    call user_write
    jmp .print_name

.file_entry:
    mov ecx, lsapp_file_prefix
    mov edx, lsapp_file_prefix_len
    call user_write

.print_name:
    lea ecx, [edi + VFS_DIRENT_NAME]
    call write_cstr
    cmp dword [edi + VFS_DIRENT_TYPE], VFS_NODE_DIRECTORY
    je .entry_done

    mov ecx, lsapp_entry_size
    mov edx, lsapp_entry_size_len
    call user_write
    mov ebx, [edi + VFS_DIRENT_SIZE_FIELD]
    call write_uint
    mov ecx, lsapp_bytes
    mov edx, lsapp_bytes_len
    call user_write

.entry_done:
    mov ecx, lsapp_newline
    mov edx, 1
    call user_write
    inc ebp
    jmp .next_entry

.done_entries:
    xor ebx, ebx
    call user_exit

.stat_failed:
    mov ecx, lsapp_stat_failed
    mov edx, lsapp_stat_failed_len
    call user_write
    mov ebx, 1
    call user_exit

write_cstr:
    push ecx
    xor edx, edx
.strlen:
    cmp byte [ecx + edx], 0
    je .write
    inc edx
    jmp .strlen
.write:
    pop ecx
    call user_write
    ret

write_uint_line:
    call write_uint
    call write_newline
    ret

write_newline:
    mov ecx, lsapp_newline
    mov edx, 1
    call user_write
    ret

write_uint:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    sub esp, 16
    mov edi, esp
    add edi, 16
    xor esi, esi
    mov eax, ebx
    cmp eax, 0
    jne .digits

    dec edi
    mov byte [edi], '0'
    mov esi, 1
    jmp .emit

.digits:
    mov ecx, 10
.digit_loop:
    xor edx, edx
    div ecx
    add dl, '0'
    dec edi
    mov [edi], dl
    inc esi
    cmp eax, 0
    jne .digit_loop

.emit:
    mov ecx, edi
    mov edx, esi
    call user_write
    add esp, 16

    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

section .user_rodata
lsapp_root db "/", 0
lsapp_title db "[lsapp] SYS_STAT/SYS_READDIR", 10
lsapp_title_len equ $ - lsapp_title
lsapp_path db "Path: "
lsapp_path_len equ $ - lsapp_path
lsapp_type_dir db "Type: directory", 10
lsapp_type_dir_len equ $ - lsapp_type_dir
lsapp_type_file db "Type: file", 10
lsapp_type_file_len equ $ - lsapp_type_file
lsapp_size db "Size: "
lsapp_size_len equ $ - lsapp_size
lsapp_children db "Children: "
lsapp_children_len equ $ - lsapp_children
lsapp_dir_prefix db "  [dir] "
lsapp_dir_prefix_len equ $ - lsapp_dir_prefix
lsapp_file_prefix db "  [file] "
lsapp_file_prefix_len equ $ - lsapp_file_prefix
lsapp_entry_size db "  "
lsapp_entry_size_len equ $ - lsapp_entry_size
lsapp_bytes db " bytes"
lsapp_bytes_len equ $ - lsapp_bytes
lsapp_newline db 10
lsapp_stat_failed db "[lsapp] stat failed", 10
lsapp_stat_failed_len equ $ - lsapp_stat_failed

section .note.GNU-stack noalloc noexec nowrite progbits
