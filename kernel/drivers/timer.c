#include "timer.h"

#include "io.h"
#define PIT_FREQUENCY 1193182
#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

static uint32_t ticks;
static uint32_t timer_frequency;

void timer_initialize(uint32_t frequency) {
    timer_frequency = frequency;
    ticks = 0;

    uint32_t divisor = PIT_FREQUENCY / frequency;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_tick(void) {
    ticks++;
}

uint32_t timer_ticks(void) {
    return ticks;
}
