#ifndef REA_CONSOLE_H
#define REA_CONSOLE_H

#include <stddef.h>

void console_init(void);
void console_clear(void);
void console_putc(char ch);
void console_write(const char *text);

#endif
