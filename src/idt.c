#include "idt.h"

#include "console.h"
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_IRQ_BASE 32
#define IDT_EXCEPTION_COUNT 32
#define IDT_IRQ_COUNT 16
#define IDT_DOUBLE_FAULT_IST 1
#define GDT_CODE_SELECTOR 0x08

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef void (*stub_function)(void);

static struct idt_gate idt[IDT_ENTRIES] __attribute__((aligned(16)));
extern stub_function exception_stub_table[IDT_EXCEPTION_COUNT];
extern stub_function irq_stub_table[IDT_IRQ_COUNT];
extern void interrupt_default_stub(void);

static const char *const exception_names[IDT_EXCEPTION_COUNT] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static void set_gate(uint8_t vector, stub_function handler, uint8_t ist)
{
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = GDT_CODE_SELECTOR;
    idt[vector].ist = ist & 0x07u;
    idt[vector].attributes = 0x8e;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

void idt_init(void)
{
    __asm__ volatile ("cli" ::: "memory");

    for (unsigned int vector = 0; vector < IDT_ENTRIES; ++vector) {
        set_gate((uint8_t)vector, interrupt_default_stub, 0);
    }
    for (unsigned int vector = 0; vector < IDT_EXCEPTION_COUNT; ++vector) {
        uint8_t ist = vector == 8 ? IDT_DOUBLE_FAULT_IST : 0;
        set_gate((uint8_t)vector, exception_stub_table[vector], ist);
    }
    for (unsigned int irq = 0; irq < IDT_IRQ_COUNT; ++irq) {
        set_gate((uint8_t)(IDT_IRQ_BASE + irq), irq_stub_table[irq], 0);
    }

    struct idt_register idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
    serial_write("REA OS: IDT loaded (256 entries)\r\n");
}

static void write_hex(uint64_t value, char output[19])
{
    static const char digits[] = "0123456789abcdef";
    output[0] = '0';
    output[1] = 'x';
    for (unsigned int index = 0; index < 16; ++index) {
        unsigned int shift = (15 - index) * 4;
        output[index + 2] = digits[(value >> shift) & 0x0fu];
    }
    output[18] = '\0';
}

static void exception_halt(const struct interrupt_frame *frame)
{
    uint64_t vector = frame->vector;
    const char *name = vector < IDT_EXCEPTION_COUNT
                           ? exception_names[vector]
                           : "Unknown Exception";
    char error_code[19];
    char rip[19];
    write_hex(frame->error_code, error_code);
    write_hex(frame->rip, rip);

    serial_write("REA OS: exception ");
    serial_write(name);
    serial_write(" error=");
    serial_write(error_code);
    serial_write(" RIP=");
    serial_write(rip);
    serial_write("\r\n");

    console_write("\nEXCEPTION: ");
    console_write(name);
    console_write("\nError code: ");
    console_write(error_code);
    console_write("\nRIP: ");
    console_write(rip);
    console_write("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}

void interrupt_dispatch(struct interrupt_frame *frame)
{
    if (frame->vector < IDT_EXCEPTION_COUNT) {
        exception_halt(frame);
    }

    if (frame->vector >= IDT_IRQ_BASE &&
        frame->vector < IDT_IRQ_BASE + IDT_IRQ_COUNT) {
        uint8_t irq = (uint8_t)(frame->vector - IDT_IRQ_BASE);
        switch (irq) {
        case 0:
            pit_irq_handler();
            break;
        case 1:
            keyboard_irq_handler();
            break;
        case 12:
            mouse_irq_handler();
            break;
        default:
            break;
        }
        pic_send_eoi(irq);
    }
}
