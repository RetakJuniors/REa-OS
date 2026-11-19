#ifndef REA_CONSOLE_H
#define REA_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

void console_init(void);
void console_clear(void);
void console_putc(char ch);
void console_write(const char *text);
void console_write_n(const char *text, size_t length);
void console_get_cursor_position(size_t *x, size_t *y);
void console_erase_input(size_t x, size_t y, uint16_t advance);
void console_cursor_show(void);
void console_cursor_hide(void);

#endif
