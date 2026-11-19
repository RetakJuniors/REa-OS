#include "mouse.h"

#include "cursor.h"
#include "pic.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_DATA_PORT 0x60
#define PS2_OUTPUT_FULL 0x01u
#define PS2_INPUT_FULL 0x02u
#define PS2_AUXILIARY_DATA 0x20u
#define PS2_WAIT_LIMIT 100000u
#define MOUSE_BUFFER_SIZE 256u

static struct mouse_state current_state;
static uint8_t packet[3];
static uint8_t packet_index;
static int initialized;
static volatile uint8_t byte_buffer[MOUSE_BUFFER_SIZE];
static volatile uint8_t buffer_head;
static volatile uint8_t buffer_tail;

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
        if ((inb(PS2_STATUS_PORT) & PS2_INPUT_FULL) == 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int wait_output_full(void)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS_PORT) & PS2_OUTPUT_FULL) != 0) {
            return 1;
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static void drain_output(void)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        if ((inb(PS2_STATUS_PORT) & PS2_OUTPUT_FULL) == 0) {
            return;
        }
        (void)inb(PS2_DATA_PORT);
    }
}

static int controller_command(uint8_t command)
{
    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_COMMAND_PORT, command);
    return 1;
}

static int auxiliary_command(uint8_t command)
{
    if (!controller_command(0xd4) || !wait_input_empty()) {
        return 0;
    }
    outb(PS2_DATA_PORT, command);
    return 1;
}

static int read_auxiliary_byte(uint8_t *value)
{
    for (uint32_t attempt = 0; attempt < PS2_WAIT_LIMIT; ++attempt) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if ((status & PS2_OUTPUT_FULL) != 0) {
            uint8_t data = inb(PS2_DATA_PORT);
            if ((status & PS2_AUXILIARY_DATA) != 0) {
                *value = data;
                return 1;
            }
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int send_auxiliary_command(uint8_t command)
{
    uint8_t response;
    return auxiliary_command(command) &&
           read_auxiliary_byte(&response) && response == 0xfa;
}

int mouse_init(void)
{
    serial_write("REA OS: initializing PS/2 mouse\r\n");
    buffer_head = 0;
    buffer_tail = 0;
    packet_index = 0;
    initialized = 0;
    drain_output();

    if (!controller_command(0xa7) || !controller_command(0x20) ||
        !wait_output_full()) {
        goto failed;
    }

    uint8_t config = inb(PS2_DATA_PORT);
    config = (uint8_t)(config | 0x22u);
    if (!controller_command(0x60) || !wait_input_empty()) {
        goto failed;
    }
    outb(PS2_DATA_PORT, config);

    if (!controller_command(0xa8) ||
        !send_auxiliary_command(0xf5) ||
        !send_auxiliary_command(0xf6) ||
        !send_auxiliary_command(0xf4) ||
        !cursor_init()) {
        goto failed;
    }

    cursor_get_position(&current_state.x, &current_state.y);
    current_state.left_button = 0;
    current_state.right_button = 0;
    current_state.middle_button = 0;
    packet_index = 0;
    initialized = 1;
    pic_enable_irq(12);
    serial_write("REA OS: PS/2 mouse found\r\n");
    return 1;

failed:
    serial_write("REA OS: PS/2 mouse initialization failed\r\n");
    return 0;
}

static void process_packet(void)
{
    if ((packet[0] & 0xc0u) != 0) {
        return;
    }

    int16_t delta_x = packet[1];
    int16_t delta_y = packet[2];
    if ((packet[0] & 0x10u) != 0) {
        delta_x -= 256;
    }
    if ((packet[0] & 0x20u) != 0) {
        delta_y -= 256;
    }

    current_state.left_button = packet[0] & 0x01u;
    current_state.right_button = (packet[0] >> 1) & 0x01u;
    current_state.middle_button = (packet[0] >> 2) & 0x01u;
    cursor_move(delta_x, -delta_y);
    cursor_get_position(&current_state.x, &current_state.y);
}

void mouse_irq_handler(void)
{
    uint8_t status = inb(PS2_STATUS_PORT);
    if ((status & PS2_OUTPUT_FULL) == 0 ||
        (status & PS2_AUXILIARY_DATA) == 0) {
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);
    uint8_t next_head = (uint8_t)(buffer_head + 1u);
    if (next_head != buffer_tail) {
        byte_buffer[buffer_head] = data;
        buffer_head = next_head;
    }
}

int mouse_has_data(void)
{
    return buffer_head != buffer_tail;
}

static int dequeue_byte(uint8_t *data)
{
    uint8_t tail = buffer_tail;
    if (tail == buffer_head) {
        return 0;
    }

    *data = byte_buffer[tail];
    buffer_tail = (uint8_t)(tail + 1u);
    return 1;
}

void mouse_poll(void)
{
    if (!initialized) {
        return;
    }

    uint8_t data;
    while (dequeue_byte(&data)) {
        if (packet_index == 0) {
            if ((data & 0x08u) == 0) {
                continue;
            }
            packet[packet_index++] = data;
        } else {
            packet[packet_index++] = data;
            if (packet_index == 3) {
                packet_index = 0;
                process_packet();
            }
        }
    }
}

void mouse_get_state(struct mouse_state *state)
{
    if (state != 0) {
        *state = current_state;
    }
}
