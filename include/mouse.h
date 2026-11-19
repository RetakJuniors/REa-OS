#ifndef REA_MOUSE_H
#define REA_MOUSE_H

#include <stdint.h>

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t left_button;
    uint8_t right_button;
    uint8_t middle_button;
};

int mouse_init(void);
void mouse_irq_handler(void);
int mouse_has_data(void);
void mouse_poll(void);
void mouse_get_state(struct mouse_state *state);

#endif
