#include "system.h"

#include "boot.h"
#include "heap.h"
#include "memory.h"
#include "serial.h"
#include "timer.h"

#include <stddef.h>
#include <stdint.h>

enum hypervisor_id {
    HYPERVISOR_NONE,
    HYPERVISOR_VBOX,
    HYPERVISOR_KVM,
    HYPERVISOR_TCG,
    HYPERVISOR_MICROSOFT
};

struct cpuid_result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static struct cpuid_result cpuid(uint32_t leaf)
{
    struct cpuid_result result;
    __asm__ volatile ("cpuid"
                      : "=a"(result.eax), "=b"(result.ebx),
                        "=c"(result.ecx), "=d"(result.edx)
                      : "a"(leaf), "c"(0));
    return result;
}

static enum hypervisor_id hypervisor_id;
static char hypervisor_vendor[13];
static int hypervisor_initialized;

static int vendor_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return 0;
        }
    }
    return *left == *right;
}

static void copy_uint32(char *output, uint32_t value)
{
    for (unsigned int index = 0; index < 4u; ++index) {
        *output++ = (char)((value >> (index * 8u)) & 0xffu);
    }
}

static void initialize_hypervisor(void)
{
    if (hypervisor_initialized) {
        return;
    }
    hypervisor_initialized = 1;
    hypervisor_id = HYPERVISOR_NONE;
    hypervisor_vendor[0] = '\0';

    struct cpuid_result leaf1 = cpuid(1);
    if ((leaf1.ecx & (1u << 31)) == 0) {
        return;
    }
    struct cpuid_result leaf40000000 = cpuid(0x40000000u);
    copy_uint32(hypervisor_vendor, leaf40000000.ebx);
    copy_uint32(hypervisor_vendor + 4, leaf40000000.ecx);
    copy_uint32(hypervisor_vendor + 8, leaf40000000.edx);
    hypervisor_vendor[12] = '\0';

    if (vendor_equals(hypervisor_vendor, "VBoxVBoxVBox")) {
        hypervisor_id = HYPERVISOR_VBOX;
    } else if (vendor_equals(hypervisor_vendor, "KVMKVMKVM")) {
        hypervisor_id = HYPERVISOR_KVM;
    } else if (vendor_equals(hypervisor_vendor, "TCGTCGTCG")) {
        hypervisor_id = HYPERVISOR_TCG;
    } else if (vendor_equals(hypervisor_vendor, "Microsoft Hv")) {
        hypervisor_id = HYPERVISOR_MICROSOFT;
    }
}

void system_cpu_vendor(char output[13])
{
    struct cpuid_result leaf0 = cpuid(0);
    copy_uint32(output, leaf0.ebx);
    copy_uint32(output + 4, leaf0.edx);
    copy_uint32(output + 8, leaf0.ecx);
    output[12] = '\0';
}

void system_cpu_brand(char output[49])
{
    struct cpuid_result maximum = cpuid(0x80000000u);
    if (maximum.eax < 0x80000004u) {
        char vendor[13];
        system_cpu_vendor(vendor);
        unsigned int index = 0;
        while (vendor[index] != '\0') {
            output[index] = vendor[index];
            ++index;
        }
        output[index] = '\0';
        return;
    }
    for (unsigned int part = 0; part < 3u; ++part) {
        struct cpuid_result leaf =
            cpuid(0x80000002u + part);
        char *destination = output + part * 16u;
        copy_uint32(destination, leaf.eax);
        copy_uint32(destination + 4, leaf.ebx);
        copy_uint32(destination + 8, leaf.ecx);
        copy_uint32(destination + 12, leaf.edx);
    }
    output[48] = '\0';
}

void system_hypervisor_vendor(char output[13])
{
    initialize_hypervisor();
    unsigned int index = 0;
    while (index < 12u && hypervisor_vendor[index] != '\0') {
        output[index] = hypervisor_vendor[index];
        ++index;
    }
    output[index] = '\0';
}

int system_hypervisor(void)
{
    initialize_hypervisor();
    return hypervisor_id != HYPERVISOR_NONE;
}

static void append_text(char *buffer, size_t capacity,
                        size_t *length, const char *text)
{
    while (*text != '\0' && *length + 1u < capacity) {
        buffer[(*length)++] = *text++;
    }
    buffer[*length] = '\0';
}

static void append_number(char *buffer, size_t capacity,
                          size_t *length, uint64_t number)
{
    char digits[21];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0 && count < sizeof(digits));
    while (count != 0 && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

size_t system_format(char *buffer, size_t capacity)
{
    if (buffer == 0 || capacity == 0) {
        return 0;
    }
    size_t length = 0;
    char vendor[13];
    char brand[49];
    char hypervisor[13];
    system_cpu_vendor(vendor);
    system_cpu_brand(brand);
    system_hypervisor_vendor(hypervisor);

    append_text(buffer, capacity, &length, "CPU: ");
    append_text(buffer, capacity, &length, vendor);
    append_text(buffer, capacity, &length, " ");
    append_text(buffer, capacity, &length, brand);
    append_text(buffer, capacity, &length, "\nHypervisor: ");
    if (hypervisor[0] == '\0') {
        append_text(buffer, capacity, &length, "none");
    } else {
        append_text(buffer, capacity, &length, hypervisor);
    }

    struct memory_stats memory;
    append_text(buffer, capacity, &length, "\nRAM: ");
    if (memory_get_stats(&memory)) {
        append_number(buffer, capacity, &length,
                      memory.total_bytes / (1024u * 1024u));
        append_text(buffer, capacity, &length, " MB");
    } else {
        append_text(buffer, capacity, &length, "unavailable");
    }

    struct limine_framebuffer *framebuffer;
    append_text(buffer, capacity, &length, "\nDisplay: ");
    if (boot_get_framebuffer(&framebuffer) && framebuffer != 0) {
        append_number(buffer, capacity, &length, framebuffer->width);
        append_text(buffer, capacity, &length, "x");
        append_number(buffer, capacity, &length, framebuffer->height);
    } else {
        append_text(buffer, capacity, &length, "unavailable");
    }

    append_text(buffer, capacity, &length, "\nUptime: ");
    uint64_t seconds = timer_ticks() / 100u;
    append_number(buffer, capacity, &length, seconds / 3600u);
    append_text(buffer, capacity, &length, "h ");
    append_number(buffer, capacity, &length, (seconds / 60u) % 60u);
    append_text(buffer, capacity, &length, "m ");
    append_number(buffer, capacity, &length, seconds % 60u);
    append_text(buffer, capacity, &length, "s");
    append_text(buffer, capacity, &length, "\nVersion: REA OS v0.0.8a");
    return length;
}

void system_reboot(void)
{
    __asm__ volatile ("cli");
    __asm__ volatile ("outb %0, %1"
                      : : "a"((uint8_t)0xfe),
                          "d"((uint16_t)0x64));

    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) zero_idtr = {0, 0};
    __asm__ volatile ("lidt %0" : : "m"(zero_idtr));
    __asm__ volatile ("int $0");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

int system_poweroff(void)
{
    initialize_hypervisor();
    if (hypervisor_id == HYPERVISOR_VBOX) {
        outw(0x4004u, 0x3400u);
        return 1;
    } else if (hypervisor_id == HYPERVISOR_KVM ||
               hypervisor_id == HYPERVISOR_TCG) {
        outw(0x604u, 0x2000u);
        return 1;
    }
    return 0;
}
