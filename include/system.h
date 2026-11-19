#ifndef REA_SYSTEM_H
#define REA_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

void system_cpu_vendor(char output[13]);
void system_cpu_brand(char output[49]);
void system_hypervisor_vendor(char output[13]);
int system_hypervisor(void);
size_t system_format(char *buffer, size_t capacity);
void system_reboot(void);
int system_poweroff(void);

#endif
