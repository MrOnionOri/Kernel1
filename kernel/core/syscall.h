#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "idt.h"

#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_YIELD 3

void syscall_dispatch(struct interrupt_frame* frame);

#endif
