#ifndef REA_KEYBOARD_H
#define REA_KEYBOARD_H

#define KEY_EVENT_NONE 0
#define KEY_EVENT_CHAR 1
#define KEY_EVENT_LEFT 2
#define KEY_EVENT_RIGHT 3
#define KEY_EVENT_UP 4
#define KEY_EVENT_DOWN 5
#define KEY_EVENT_HOME 6
#define KEY_EVENT_END 7
#define KEY_EVENT_DELETE 8

struct keyboard_event {
    int type;
    char character;
};

void keyboard_init(void);
void keyboard_irq_handler(void);
int keyboard_has_data(void);
int keyboard_poll_char(char *character);
int keyboard_poll_event(struct keyboard_event *event);

#endif
