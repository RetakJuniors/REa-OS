#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "serial.h"

void kmain(void)
{
    serial_write("REA OS: kmain started\r\n");
    console_init();
    serial_write("REA OS: console initialized\r\n");
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
