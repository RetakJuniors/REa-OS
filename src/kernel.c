#include "console.h"
#include "keyboard.h"
#include "shell.h"

void kmain(void)
{
    console_init();
    shell_init();

    for (;;) {
        char character;
        if (keyboard_poll_char(&character)) {
            shell_handle_char(character);
        } else {
            __asm__ volatile ("pause");
        }
    }
}
