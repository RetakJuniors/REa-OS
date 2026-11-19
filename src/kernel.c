#include "console.h"
#include "boot.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "memory.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "shell.h"
#include "serial.h"
#include "timer.h"

void kmain(void)
{
    __asm__ volatile ("cli" ::: "memory");
    serial_write("REA OS: kmain started\r\n");
    console_init();
    serial_write("REA OS: console initialized\r\n");

    const struct limine_memmap_response *memory_map;
    uint64_t hhdm_offset;
    if (boot_get_memory_map(&memory_map, &hhdm_offset) &&
        !memory_init(memory_map, hhdm_offset)) {
        serial_write("REA OS: physical memory manager init failed\r\n");
    }

    gdt_init();
    timer_init();
    idt_init();
    pic_init();
    keyboard_init();
    pit_init();
    mouse_init();
    shell_init();
    __asm__ volatile ("sti" ::: "memory");

    for (;;) {
        mouse_poll();
        char character;
        while (keyboard_poll_char(&character)) {
            shell_handle_char(character);
        }
        shell_cursor_update();

        __asm__ volatile ("cli" ::: "memory");
        if (keyboard_has_data() || mouse_has_data()) {
            __asm__ volatile ("sti" ::: "memory");
        } else {
            timer_wait_for_interrupt();
        }
    }
}
