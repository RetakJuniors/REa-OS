#include "gdt.h"

#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define DOUBLE_FAULT_IST 1
#define STACK_SIZE 16384

struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct task_state_segment tss;
static uint8_t privilege_stack[STACK_SIZE] __attribute__((aligned(16)));
static uint8_t double_fault_stack[STACK_SIZE] __attribute__((aligned(16)));

extern void gdt_load(const struct gdt_pointer *pointer);

void gdt_init(void)
{
    uint64_t tss_base = (uint64_t)(uintptr_t)&tss;
    uint64_t tss_limit = sizeof(tss) - 1;

    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffff;
    gdt[2] = 0x00cf92000000ffff;
    gdt[3] = (tss_limit & 0xffffu) |
             ((tss_base & 0xffffffu) << 16) |
             (0x89ull << 40) |
             ((tss_limit & 0xf0000u) << 32) |
             ((tss_base & 0xff000000u) << 32);
    gdt[4] = (tss_base >> 32) & 0xffffffffu;

    tss.rsp0 = (uint64_t)(uintptr_t)(privilege_stack + sizeof(privilege_stack));
    tss.ist[DOUBLE_FAULT_IST - 1] =
        (uint64_t)(uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap_base = sizeof(tss);

    struct gdt_pointer pointer = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt
    };
    gdt_load(&pointer);
    serial_write("REA OS: GDT and TSS loaded\r\n");
}
