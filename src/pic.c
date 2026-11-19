#include "pic.h"

#include "serial.h"

#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1

static uint8_t master_mask = 0xff;
static uint8_t slave_mask = 0xff;

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
    outb(0x80, 0);
}

void pic_init(void)
{
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    master_mask = 0xff;
    slave_mask = 0xff;
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
    serial_write("REA OS: PIC remapped to IRQ 32-47\r\n");
}

void pic_enable_irq(uint8_t irq)
{
    if (irq >= 16) {
        return;
    }

    if (irq < 8) {
        master_mask &= (uint8_t)~(1u << irq);
        outb(PIC1_DATA, master_mask);
    } else {
        slave_mask &= (uint8_t)~(1u << (irq - 8));
        master_mask &= (uint8_t)~(1u << 2);
        outb(PIC2_DATA, slave_mask);
        outb(PIC1_DATA, master_mask);
    }
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_COMMAND, 0x20);
    }
    outb(PIC1_COMMAND, 0x20);
}
