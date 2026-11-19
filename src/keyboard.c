#include "keyboard.h"

#include "pic.h"
#include "serial.h"

#include <stdint.h>

#define KEYBOARD_BUFFER_SIZE 256u
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_DATA_PORT 0x60
#define PS2_WAIT_LIMIT 100000u

extern const char keyboard_plain[128];
extern const char keyboard_shifted[128];

static int left_shift;
static int right_shift;
static int caps_lock;
static volatile uint8_t scancode_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint8_t buffer_head;
static volatile uint8_t buffer_tail;
static int e0_prefix;
static int e1_prefix;

static uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int wait_input_empty(void)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS_PORT) & 0x02u) == 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int wait_output_full(void)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS_PORT) & 0x01u) != 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int enable_controller_irq(void)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS_PORT) & 0x01u) == 0) {
            break;
        }
        (void)inb(PS2_DATA_PORT);
    }

    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_COMMAND_PORT, 0x20);
    if (!wait_output_full()) {
        return 0;
    }
    uint8_t status = inb(PS2_STATUS_PORT);
    uint8_t config = inb(PS2_DATA_PORT);
    if ((status & 0x20u) != 0) {
        return 0;
    }

    config = (uint8_t)((config | 0x01u) & (uint8_t)~0x10u);
    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_COMMAND_PORT, 0x60);
    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_DATA_PORT, config);
    return 1;
}

void keyboard_init(void)
{
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    buffer_head = 0;
    buffer_tail = 0;
    if (!enable_controller_irq()) {
        serial_write("REA OS: keyboard controller IRQ setup failed\r\n");
    }
    pic_enable_irq(1);
}

void keyboard_irq_handler(void)
{
    uint8_t status = inb(0x64);
    if ((status & 1u) == 0 || (status & 0x20u) != 0) {
        return;
    }

    uint8_t scancode = inb(0x60);
    uint8_t next_head = (uint8_t)(buffer_head + 1u);
    if (next_head != buffer_tail) {
        scancode_buffer[buffer_head] = scancode;
        buffer_head = next_head;
    }
}

int keyboard_has_data(void)
{
    return buffer_head != buffer_tail;
}

static int dequeue_scancode(uint8_t *scancode)
{
    uint8_t tail = buffer_tail;
    if (tail == buffer_head) {
        return 0;
    }

    *scancode = scancode_buffer[tail];
    buffer_tail = (uint8_t)(tail + 1u);
    return 1;
}

int keyboard_poll_char(char *character)
{
    struct keyboard_event event;
    while (keyboard_poll_event(&event)) {
        if (event.type == KEY_EVENT_CHAR) {
            *character = event.character;
            return 1;
        }
        if (event.type == KEY_EVENT_LEFT ||
            event.type == KEY_EVENT_RIGHT ||
            event.type == KEY_EVENT_UP ||
            event.type == KEY_EVENT_DOWN ||
            event.type == KEY_EVENT_HOME ||
            event.type == KEY_EVENT_END ||
            event.type == KEY_EVENT_DELETE) {
            return 0;
        }
    }
    return 0;
}

int keyboard_poll_event(struct keyboard_event *event)
{
    if (event == 0) {
        return 0;
    }
    uint8_t scancode;

    while (dequeue_scancode(&scancode)) {
        if (e1_prefix) {
            if (scancode == 0xe1) {
                continue;
            }
            e1_prefix = 0;
            continue;
        }
        if (scancode == 0xe0) {
            e0_prefix = 1;
            continue;
        }
        if (scancode == 0xe1) {
            e1_prefix = 1;
            e0_prefix = 0;
            continue;
        }

        int released = (scancode & 0x80u) != 0;
        uint8_t key = scancode & 0x7fu;
        if (released) {
            if (key == 0x2a) {
                left_shift = 0;
            } else if (key == 0x36) {
                right_shift = 0;
            }
            e0_prefix = 0;
            continue;
        }

        if (e0_prefix) {
            e0_prefix = 0;
            event->character = '\0';
            if (key == 0x4b) event->type = KEY_EVENT_LEFT;
            else if (key == 0x4d) event->type = KEY_EVENT_RIGHT;
            else if (key == 0x48) event->type = KEY_EVENT_UP;
            else if (key == 0x50) event->type = KEY_EVENT_DOWN;
            else if (key == 0x47) event->type = KEY_EVENT_HOME;
            else if (key == 0x4f) event->type = KEY_EVENT_END;
            else if (key == 0x53) event->type = KEY_EVENT_DELETE;
            else event->type = KEY_EVENT_NONE;
            if (event->type != KEY_EVENT_NONE) {
                return 1;
            }
            continue;
        }

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

        char character;
        if (plain >= 'a' && plain <= 'z') {
            character = (shift != caps_lock) ? shifted : plain;
        } else {
            character = shift ? shifted : plain;
        }

        if (character != '\0') {
            event->type = KEY_EVENT_CHAR;
            event->character = character;
            return 1;
        }
    }

    return 0;
}
