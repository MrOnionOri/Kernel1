#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

void timer_initialize(uint32_t frequency);
void timer_tick(void);
uint32_t timer_ticks(void);

#endif
