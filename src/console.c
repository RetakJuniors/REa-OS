#include "console.h"

#include "boot.h"
#include "font.h"
#include "util.h"

#include <stdint.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define COLOR_BLACK 0x000000
#define COLOR_TEXT 0xd8e2f0

static struct limine_framebuffer *framebuffer;
static size_t columns;
static size_t rows;
static size_t cursor_x;
static size_t cursor_y;

static void put_pixel(size_t x, size_t y, uint32_t color)
{
    uint8_t *pixel = (uint8_t *)framebuffer->address +
                     y * framebuffer->pitch + x * 4;
    uint32_t red = (color >> 16) & 0xff;
    uint32_t green = (color >> 8) & 0xff;
    uint32_t blue = color & 0xff;
    uint32_t packed = ((red >> (8 - framebuffer->red_mask_size)) <<
                       framebuffer->red_mask_shift) |
                      ((green >> (8 - framebuffer->green_mask_size)) <<
                       framebuffer->green_mask_shift) |
                      ((blue >> (8 - framebuffer->blue_mask_size)) <<
                       framebuffer->blue_mask_shift);

    *(uint32_t *)pixel = packed;
}

static void fill_rect(size_t x, size_t y, size_t width, size_t height,
                      uint32_t color)
{
    for (size_t row = y; row < y + height; ++row) {
        for (size_t column = x; column < x + width; ++column) {
            put_pixel(column, row, color);
        }
    }
}

static void scroll_if_needed(void)
{
    if (cursor_y < rows) {
        return;
    }

    size_t scroll_bytes = FONT_HEIGHT * framebuffer->pitch;
    size_t remaining_bytes = (framebuffer->height - FONT_HEIGHT) *
                             framebuffer->pitch;
    memory_copy(framebuffer->address,
                (uint8_t *)framebuffer->address + scroll_bytes,
                remaining_bytes);
    fill_rect(0, framebuffer->height - FONT_HEIGHT,
              framebuffer->width, FONT_HEIGHT, COLOR_BLACK);
    cursor_y = rows - 1;
}

static void draw_character(char character)
{
    if (character < 32 || character > 126) {
        character = '?';
    }

    for (unsigned int row = 0; row < 8; ++row) {
        uint8_t bits = font_row(character, row);
        for (unsigned int column = 0; column < FONT_WIDTH; ++column) {
            uint32_t color = (bits & (0x80u >> column)) ?
                             COLOR_TEXT : COLOR_BLACK;
            put_pixel(cursor_x * FONT_WIDTH + column,
                      cursor_y * FONT_HEIGHT + row * 2, color);
            put_pixel(cursor_x * FONT_WIDTH + column,
                      cursor_y * FONT_HEIGHT + row * 2 + 1, color);
        }
    }

    if (++cursor_x >= columns) {
        cursor_x = 0;
        ++cursor_y;
        scroll_if_needed();
    }
}

void console_init(void)
{
    if (!boot_get_framebuffer(&framebuffer) ||
        framebuffer->address == 0 || framebuffer->bits_per_pixel != 32 ||
        framebuffer->red_mask_size == 0 ||
        framebuffer->green_mask_size == 0 ||
        framebuffer->blue_mask_size == 0) {
        /* Use COM1 for diagnostics if the framebuffer could not be obtained. */
        extern void serial_write(const char *text);
        serial_write("REA OS: framebuffer unavailable\r\n");
        for (;;) {
            __asm__ volatile ("pause");
        }
    }

    columns = framebuffer->width / FONT_WIDTH;
    rows = framebuffer->height / FONT_HEIGHT;
    cursor_x = 0;
    cursor_y = 0;
    console_clear();
}

void console_clear(void)
{
    fill_rect(0, 0, framebuffer->width, framebuffer->height, COLOR_BLACK);
    cursor_x = 0;
    cursor_y = 0;
}

void console_putc(char character)
{
    if (character == '\r') {
        return;
    }
    if (character == '\n') {
        cursor_x = 0;
        ++cursor_y;
        scroll_if_needed();
        return;
    }
    if (character == '\b') {
        if (cursor_x != 0) {
            --cursor_x;
        } else if (cursor_y != 0) {
            --cursor_y;
            cursor_x = columns - 1;
        } else {
            return;
        }
        fill_rect(cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT,
                  FONT_WIDTH, FONT_HEIGHT, COLOR_BLACK);
        return;
    }

    draw_character(character);
}

void console_write(const char *text)
{
    while (*text != '\0') {
        console_putc(*text++);
    }
}
