#include "mouse.h"

#include "io.h"
#include "terminal.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL 0x02

#define PS2_COMMAND_READ_CONFIG 0x20
#define PS2_COMMAND_WRITE_CONFIG 0x60
#define PS2_COMMAND_ENABLE_AUX 0xA8
#define PS2_COMMAND_WRITE_AUX 0xD4

#define PS2_MOUSE_ACK 0xFA
#define PS2_MOUSE_SET_DEFAULTS 0xF6
#define PS2_MOUSE_ENABLE_DATA 0xF4

struct mouse_state {
    int initialized;
    int enabled;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;
    uint8_t packet_index;
    uint8_t packet[3];
    uint32_t packets;
};

static struct mouse_state mouse;

static int ps2_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return 1;
        }
    }

    return 0;
}

static int ps2_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 1;
        }
    }

    return 0;
}

static int ps2_write_command(uint8_t command) {
    if (!ps2_wait_input_clear()) {
        return 0;
    }

    outb(PS2_COMMAND_PORT, command);
    return 1;
}

static int ps2_write_data(uint8_t value) {
    if (!ps2_wait_input_clear()) {
        return 0;
    }

    outb(PS2_DATA_PORT, value);
    return 1;
}

static int ps2_read_data(uint8_t* value) {
    if (!ps2_wait_output_full()) {
        return 0;
    }

    *value = inb(PS2_DATA_PORT);
    return 1;
}

static int mouse_send(uint8_t command) {
    uint8_t response = 0;

    if (!ps2_write_command(PS2_COMMAND_WRITE_AUX)) {
        return 0;
    }

    if (!ps2_write_data(command)) {
        return 0;
    }

    if (!ps2_read_data(&response)) {
        return 0;
    }

    return response == PS2_MOUSE_ACK;
}

static void write_int(int value) {
    if (value < 0) {
        terminal_write("-");
        terminal_write_dec((uint32_t)(-value));
        return;
    }

    terminal_write_dec((uint32_t)value);
}

void mouse_initialize(void) {
    uint8_t config = 0;

    mouse.initialized = 0;
    mouse.enabled = 0;
    mouse.x = 0;
    mouse.y = 0;
    mouse.dx = 0;
    mouse.dy = 0;
    mouse.buttons = 0;
    mouse.packet_index = 0;
    mouse.packets = 0;

    if (!ps2_write_command(PS2_COMMAND_ENABLE_AUX)) {
        return;
    }

    if (!ps2_write_command(PS2_COMMAND_READ_CONFIG) || !ps2_read_data(&config)) {
        return;
    }

    config |= 0x02;
    config &= (uint8_t)~0x20;

    if (!ps2_write_command(PS2_COMMAND_WRITE_CONFIG) || !ps2_write_data(config)) {
        return;
    }

    if (!mouse_send(PS2_MOUSE_SET_DEFAULTS)) {
        return;
    }

    if (!mouse_send(PS2_MOUSE_ENABLE_DATA)) {
        return;
    }

    mouse.initialized = 1;
    mouse.enabled = 1;
}

void mouse_handle_irq(void) {
    uint8_t byte = inb(PS2_DATA_PORT);

    if (mouse.packet_index == 0 && (byte & 0x08) == 0) {
        return;
    }

    mouse.packet[mouse.packet_index++] = byte;

    if (mouse.packet_index < 3) {
        return;
    }

    mouse.packet_index = 0;
    mouse.dx = (int)(int8_t)mouse.packet[1];
    mouse.dy = -(int)(int8_t)mouse.packet[2];
    mouse.x += mouse.dx;
    mouse.y += mouse.dy;
    mouse.buttons = mouse.packet[0] & 0x07;
    mouse.packets++;
}

void mouse_print_status(void) {
    terminal_write("Mouse:\n");
    terminal_write("  initialized: ");
    terminal_write(mouse.initialized ? "yes\n" : "no\n");
    terminal_write("  enabled: ");
    terminal_write(mouse.enabled ? "yes\n" : "no\n");
    terminal_write("  packets: ");
    terminal_write_dec(mouse.packets);
    terminal_write("\n  x: ");
    write_int(mouse.x);
    terminal_write(" y: ");
    write_int(mouse.y);
    terminal_write("\n  dx: ");
    write_int(mouse.dx);
    terminal_write(" dy: ");
    write_int(mouse.dy);
    terminal_write("\n  buttons: ");
    terminal_write_dec(mouse.buttons);
    terminal_write("\n");
}
