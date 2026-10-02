#include "keyboard.h"

#include "io.h"
#include "shell.h"
#include "shell_system.h"

#define KEYBOARD_DATA_PORT 0x60
#define SCANCODE_LEFT_SHIFT 0x2A
#define SCANCODE_RIGHT_SHIFT 0x36
#define SCANCODE_CAPS_LOCK 0x3A
#define SCANCODE_EXTENDED 0xE0
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_LEFT 0x4B
#define SCANCODE_ARROW_RIGHT 0x4D
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_HOME 0x47
#define SCANCODE_END 0x4F
#define SCANCODE_DELETE 0x53

static int shift_pressed;
static int caps_lock_enabled;
static int extended_scancode;

static const char scancode_ascii_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0,  'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,  '*',
    0,  ' ', 0,
};

static const char scancode_ascii_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0,  'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,  '*',
    0,  ' ', 0,
};

static int is_letter_scancode(uint8_t scancode) {
    char character = scancode_ascii_lower[scancode];
    return character >= 'a' && character <= 'z';
}

void keyboard_initialize(void) {
    shift_pressed = 0;
    caps_lock_enabled = 0;
    extended_scancode = 0;
}

void keyboard_handle_irq(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    uint8_t pressed_scancode = scancode & 0x7F;

    if (scancode == SCANCODE_EXTENDED) {
        extended_scancode = 1;
        return;
    }

    if (extended_scancode) {
        extended_scancode = 0;

        if ((scancode & 0x80) || !shell_system_accepts_keyboard()) {
            return;
        }

        if (scancode == SCANCODE_ARROW_UP) {
            shell_history_previous();
        } else if (scancode == SCANCODE_ARROW_DOWN) {
            shell_history_next();
        } else if (scancode == SCANCODE_ARROW_LEFT) {
            shell_cursor_left();
        } else if (scancode == SCANCODE_ARROW_RIGHT) {
            shell_cursor_right();
        } else if (scancode == SCANCODE_HOME) {
            shell_cursor_home();
        } else if (scancode == SCANCODE_END) {
            shell_cursor_end();
        } else if (scancode == SCANCODE_DELETE) {
            shell_delete_char();
        }

        return;
    }

    if (scancode & 0x80) {
        if (pressed_scancode == SCANCODE_LEFT_SHIFT || pressed_scancode == SCANCODE_RIGHT_SHIFT) {
            shift_pressed = 0;
        }

        return;
    }

    if (scancode == SCANCODE_LEFT_SHIFT || scancode == SCANCODE_RIGHT_SHIFT) {
        shift_pressed = 1;
        return;
    }

    if (scancode == SCANCODE_CAPS_LOCK) {
        caps_lock_enabled = !caps_lock_enabled;
        return;
    }

    if (!shell_system_accepts_keyboard()) {
        return;
    }

    if (scancode < sizeof(scancode_ascii_lower) && scancode_ascii_lower[scancode] == '\t') {
        shell_complete();
        return;
    }

    char character = 0;
    if (scancode < sizeof(scancode_ascii_lower)) {
        int upper = shift_pressed;

        if (is_letter_scancode(scancode)) {
            upper = shift_pressed ^ caps_lock_enabled;
        }

        character = upper ? scancode_ascii_upper[scancode] : scancode_ascii_lower[scancode];
    }

    if (character == 0) {
        return;
    }

    shell_put_char(character);
}
