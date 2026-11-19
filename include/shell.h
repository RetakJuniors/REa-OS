#ifndef REA_SHELL_H
#define REA_SHELL_H

#define SHELL_LINE_CAPACITY 256

void shell_init(void);
void shell_handle_char(char character);
void shell_cursor_update(void);

#endif
