#include "serial.h"

#include <stdint.h>

#define COM1_DATA 0x3f8
#define COM1_LINE_STATUS 0x3fd
#define COM1_LINE_CONTROL 0x3fb

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

void serial_write(const char *text)
{
    /* Never configure the UART; skip output if COM1 is absent or not ready. */
    if ((inb(COM1_LINE_CONTROL) & 0x80u) != 0) {
        return;
    }

    while (*text != '\0') {
        unsigned int attempts = 0;
        while ((inb(COM1_LINE_STATUS) & 0x20u) == 0 &&
               attempts++ < 100000) {
            if ((inb(COM1_LINE_CONTROL) & 0x80u) != 0) {
                return;
            }
        }
        if (attempts >= 100000) {
            return;
        }
        outb(COM1_DATA, (uint8_t)*text++);
    }
}
