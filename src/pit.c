#include "pit.h"

#include "pic.h"
#include "serial.h"
#include "timer.h"

#include <stdint.h>

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_INPUT_HZ 1193182u
#define PIT_FREQUENCY 100u

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pit_init(void)
{
    uint16_t divisor = PIT_INPUT_HZ / PIT_FREQUENCY;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)divisor);
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
    pic_enable_irq(0);
    serial_write("REA OS: PIT started at 100 Hz\r\n");
}

void pit_irq_handler(void)
{
    timer_on_tick();
}
