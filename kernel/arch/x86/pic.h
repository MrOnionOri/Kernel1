#ifndef KERNEL_PIC_H
#define KERNEL_PIC_H

#include <stdint.h>

#define PIC_REMAP_OFFSET 32

void pic_remap(void);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
void pic_send_eoi(uint8_t irq);

#endif
