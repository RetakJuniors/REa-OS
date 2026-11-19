#ifndef REA_TIMER_H
#define REA_TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_wait_for_interrupt(void);
uint64_t timer_ticks(void);
uint64_t timer_cpu_usage_percent(void);
void timer_irq_handler(void);

#endif
