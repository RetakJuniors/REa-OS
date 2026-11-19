#include "console.h"
#include "boot.h"
#include "keyboard.h"
#include "memory.h"
#include "mouse.h"
#include "shell.h"
#include "serial.h"
#include "timer.h"

void kmain(void)
{
    serial_write("REA OS: kmain started\r\n");
    console_init();
    serial_write("REA OS: console initialized\r\n");

    const struct limine_memmap_response *memory_map;
    uint64_t hhdm_offset;
    if (boot_get_memory_map(&memory_map, &hhdm_offset) &&
        !memory_init(memory_map, hhdm_offset)) {
        serial_write("REA OS: physical memory manager init failed\r\n");
    }

    timer_init();
    mouse_init();
    shell_init();

    for (;;) {
        mouse_poll();
        shell_cursor_update();
        char character;
        if (keyboard_poll_char(&character)) {
            shell_handle_char(character);
        } else {
            timer_wait_for_interrupt();
        }
    }
}
