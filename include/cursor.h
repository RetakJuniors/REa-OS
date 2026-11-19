#ifndef REA_CURSOR_H
#define REA_CURSOR_H

#include <stdint.h>

#define CURSOR_DISPLAY_SCALE 2u

int cursor_init(void);
void cursor_move(int32_t delta_x, int32_t delta_y);
void cursor_get_position(int32_t *x, int32_t *y);
void cursor_begin_console_update(void);
void cursor_end_console_update(void);

#endif
