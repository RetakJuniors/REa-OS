#include "keyboard.h"

#include <stdint.h>

extern const char keyboard_plain[128];
extern const char keyboard_shifted[128];

static int left_shift;
static int right_shift;
static int caps_lock;

static uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static int keyboard_read_scancode(uint8_t *scancode)
{
    uint8_t status = inb(0x64);

    if ((status & 1u) == 0 || (status & 0x20u) != 0) {
        return 0;
    }

    *scancode = inb(0x60);
    return 1;
}

int keyboard_poll_char(char *character)
{
    uint8_t scancode;

    while (keyboard_read_scancode(&scancode)) {
        if (scancode == 0xe0 || scancode == 0xe1) {
            continue;
        }

        int released = (scancode & 0x80u) != 0;
        uint8_t key = scancode & 0x7fu;

        if (key == 0x2a) {
            left_shift = !released;
            continue;
        }
        if (key == 0x36) {
            right_shift = !released;
            continue;
        }
        if (key == 0x3a && !released) {
            caps_lock = !caps_lock;
            continue;
        }
        if (released) {
            continue;
        }

        char plain = keyboard_plain[key];
        char shifted = keyboard_shifted[key];
        int shift = left_shift || right_shift;

        if (plain >= 'a' && plain <= 'z') {
            *character = (shift != caps_lock) ? shifted : plain;
        } else {
            *character = shift ? shifted : plain;
        }

        if (*character != '\0') {
            return 1;
        }
    }

    return 0;
}
