#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

struct mouse_state {
    int initialized;
    int enabled;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;
    uint8_t previous_buttons;
    uint8_t packet_index;
    uint8_t packet[3];
    uint32_t packets;
    uint32_t bytes;
    uint32_t bad_packets;
    uint32_t left_clicks;
    uint32_t right_clicks;
    uint32_t middle_clicks;
};

void mouse_initialize(void);
void mouse_handle_irq(void);
const struct mouse_state* mouse_get_state(void);
void mouse_print_status(void);

#endif
