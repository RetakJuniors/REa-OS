#include "console.h"
#include "boot.h"
#include "gdt.h"
#include "heap.h"
#include "idt.h"
#include "keyboard.h"
#include "memory.h"
#include "mouse.h"
#include "pic.h"
#include "paging.h"
#include "pmm.h"
#include "pit.h"
#include "task.h"
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
    int memory_ready = 0;
    if (boot_get_memory_map(&memory_map, &hhdm_offset)) {
        memory_ready = pmm_init(memory_map, hhdm_offset);
        if (!memory_ready) {
            serial_write("REA OS: physical memory manager init failed\r\n");
        }
    }
    if (memory_ready) {
        if (!paging_init(hhdm_offset)) {
            serial_write("REA OS: paging manager init failed\r\n");
        } else if (!heap_init()) {
            serial_write("REA OS: kernel heap init failed\r\n");
        } else {
            serial_write("REA OS: kernel heap ready\r\n");
        }
    }

    gdt_init();
    timer_init();
    idt_init();
    pic_init();
    keyboard_init();
    pit_init();
    scheduler_init();
    mouse_init();
    shell_init();
    __asm__ volatile ("sti" ::: "memory");

    for (;;) {
        mouse_poll();
        struct keyboard_event event;
        while (keyboard_poll_event(&event)) {
            shell_handle_keyboard_event(&event);
        }
        shell_cursor_update();
        task_yield();

        __asm__ volatile ("cli" ::: "memory");
        if (keyboard_has_data() || mouse_has_data()) {
            __asm__ volatile ("sti" ::: "memory");
        } else {
            timer_wait_for_interrupt();
        }
    }
}
