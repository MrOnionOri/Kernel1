#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "idt.h"

#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_YIELD 3
#define SYS_WRITE_DEC 4
#define SYS_GETPID 5
#define SYS_TICKS 6
#define SYS_WRITE_BUF 7
#define SYS_OPEN 8
#define SYS_READ 9
#define SYS_CLOSE 10
#define SYS_GETARGS 11

void syscall_dispatch(struct interrupt_frame* frame);

#endif
