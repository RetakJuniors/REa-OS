#include "timer.h"

#include "serial.h"

#include <stdint.h>

#define PIT_HZ 100
#define USAGE_WINDOW_SECONDS 5

static volatile uint64_t ticks;
static volatile uint64_t window_ticks[USAGE_WINDOW_SECONDS];
static volatile uint64_t window_idle_ticks[USAGE_WINDOW_SECONDS];
static volatile int cpu_idle;

void timer_init(void)
{
    ticks = 0;
    cpu_idle = 0;
    for (unsigned int index = 0; index < USAGE_WINDOW_SECONDS; ++index) {
        window_ticks[index] = 0;
        window_idle_ticks[index] = 0;
    }
    serial_write("REA OS: tick accounting ready\r\n");
}

void timer_on_tick(void)
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
}

void timer_wait_for_interrupt(void)
{
    __asm__ volatile ("cli" ::: "memory");
    cpu_idle = 1;
    __asm__ volatile ("sti; hlt; cli" ::: "memory");
    cpu_idle = 0;
    __asm__ volatile ("sti" ::: "memory");
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
