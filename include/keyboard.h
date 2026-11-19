#ifndef REA_KEYBOARD_H
#define REA_KEYBOARD_H

void keyboard_init(void);
void keyboard_irq_handler(void);
int keyboard_has_data(void);
int keyboard_poll_char(char *character);

#endif
