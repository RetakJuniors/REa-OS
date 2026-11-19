#include "timer.h"

#include <stdint.h>

#define IDT_ENTRIES 256
#define PIT_HZ 100
#define PIT_INPUT_HZ 1193182
#define USAGE_WINDOW_SECONDS 5

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

static struct idt_gate idt[IDT_ENTRIES] __attribute__((aligned(16)));
static volatile uint64_t ticks;
static volatile uint64_t window_ticks[USAGE_WINDOW_SECONDS];
static volatile uint64_t window_idle_ticks[USAGE_WINDOW_SECONDS];
static volatile int cpu_idle;

extern void timer_irq_stub(void);

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void set_idt_gate(uint8_t vector, void (*handler)(void))
{
    uint64_t address = (uint64_t)(uintptr_t)handler;
    uint16_t code_selector;

    __asm__ volatile ("mov %%cs, %0" : "=r"(code_selector));
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = code_selector;
    idt[vector].ist = 0;
    idt[vector].attributes = 0x8e;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void remap_pic(void)
{
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 0x20);
    outb(0xa1, 0x28);
    outb(0x21, 0x04);
    outb(0xa1, 0x02);
    outb(0x21, 0x01);
    outb(0xa1, 0x01);
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);
}

void timer_init(void)
{
    struct idt_register idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt
    };

    __asm__ volatile ("cli" ::: "memory");
    set_idt_gate(0x20, timer_irq_stub);
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");

    remap_pic();

    uint16_t divisor = PIT_INPUT_HZ / PIT_HZ;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xff));
    outb(0x40, (uint8_t)(divisor >> 8));

    __asm__ volatile ("sti" ::: "memory");
}

void timer_irq_handler(void)
{
    uint64_t current_tick = ++ticks;

    if ((current_tick - 1) % PIT_HZ == 0) {
        uint64_t bucket =
            ((current_tick - 1) / PIT_HZ) % USAGE_WINDOW_SECONDS;
        window_ticks[bucket] = 0;
        window_idle_ticks[bucket] = 0;
    }

    uint64_t bucket = ((current_tick - 1) / PIT_HZ) % USAGE_WINDOW_SECONDS;
    ++window_ticks[bucket];
    if (cpu_idle) {
        ++window_idle_ticks[bucket];
    }

    outb(0x20, 0x20);
}

void timer_wait_for_interrupt(void)
{
    cpu_idle = 1;
    __asm__ volatile ("sti; hlt" ::: "memory");
    cpu_idle = 0;
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t timer_cpu_usage_percent(void)
{
    uint64_t total = 0;
    uint64_t idle = 0;

    __asm__ volatile ("pushfq; cli" ::: "memory");
    for (unsigned int index = 0; index < USAGE_WINDOW_SECONDS; ++index) {
        total += window_ticks[index];
        idle += window_idle_ticks[index];
    }
    __asm__ volatile ("popfq" ::: "memory");

    if (total == 0 || idle > total) {
        return 0;
    }
    return ((total - idle) * 100) / total;
}
