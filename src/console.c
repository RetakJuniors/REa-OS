#include "console.h"

#include "boot.h"
#include "cursor.h"
#include "font.h"
#include "serial.h"
#include "util.h"

#include <stdint.h>

#define COLOR_BLACK 0x000000u
#define COLOR_TEXT 0xd8e2f0u
#define CURSOR_WIDTH 2u

static struct limine_framebuffer *framebuffer;
static size_t cursor_x;
static size_t cursor_y;
static int cursor_visible;

static uint32_t scale_channel(uint8_t value, uint8_t mask_size)
{
    if (mask_size == 0 || mask_size > 16) {
        return 0;
    }

    uint32_t maximum = (1u << mask_size) - 1;
    return ((uint32_t)value * maximum + 127) / 255;
}

static void invert_pixel(size_t x, size_t y)
{
    if (x >= framebuffer->width || y >= framebuffer->height) {
        return;
    }

    uint32_t red_mask =
        ((1u << framebuffer->red_mask_size) - 1) <<
        framebuffer->red_mask_shift;
    uint32_t green_mask =
        ((1u << framebuffer->green_mask_size) - 1) <<
        framebuffer->green_mask_shift;
    uint32_t blue_mask =
        ((1u << framebuffer->blue_mask_size) - 1) <<
        framebuffer->blue_mask_shift;
    uint8_t *pixel = (uint8_t *)framebuffer->address +
                     y * framebuffer->pitch + x * 4;
    *(uint32_t *)pixel ^= red_mask | green_mask | blue_mask;
}

static void put_pixel(size_t x, size_t y, uint32_t color, uint8_t alpha)
{
    if (x >= framebuffer->width || y >= framebuffer->height || alpha == 0) {
        return;
    }

    uint8_t red = (uint8_t)((color >> 16) & 0xff);
    uint8_t green = (uint8_t)((color >> 8) & 0xff);
    uint8_t blue = (uint8_t)(color & 0xff);
    red = (uint8_t)(((uint16_t)red * alpha + 127) / 255);
    green = (uint8_t)(((uint16_t)green * alpha + 127) / 255);
    blue = (uint8_t)(((uint16_t)blue * alpha + 127) / 255);

    uint32_t packed =
        (scale_channel(red, framebuffer->red_mask_size) <<
         framebuffer->red_mask_shift) |
        (scale_channel(green, framebuffer->green_mask_size) <<
         framebuffer->green_mask_shift) |
        (scale_channel(blue, framebuffer->blue_mask_size) <<
         framebuffer->blue_mask_shift);
    uint8_t *pixel = (uint8_t *)framebuffer->address +
                     y * framebuffer->pitch + x * 4;
    *(uint32_t *)pixel = packed;
}

static void fill_rect(size_t x, size_t y, size_t width, size_t height,
                      uint32_t color)
{
    for (size_t row = y; row < y + height && row < framebuffer->height; ++row) {
        for (size_t column = x;
             column < x + width && column < framebuffer->width; ++column) {
            put_pixel(column, row, color, 255);
        }
    }
}

static void scroll_if_needed(void)
{
    size_t line_height = font_line_height();
    if (cursor_y + line_height <= framebuffer->height) {
        return;
    }

    size_t scroll_height = line_height;
    if (scroll_height > framebuffer->height) {
        scroll_height = framebuffer->height;
    }
    size_t scroll_bytes = scroll_height * framebuffer->pitch;
    size_t remaining_bytes =
        (framebuffer->height - scroll_height) * framebuffer->pitch;
    memory_copy(framebuffer->address,
                (uint8_t *)framebuffer->address + scroll_bytes,
                remaining_bytes);
    fill_rect(0, framebuffer->height - scroll_height,
              framebuffer->width, scroll_height, COLOR_BLACK);
    cursor_y = framebuffer->height - line_height;
}

static void new_line(void)
{
    cursor_x = 0;
    cursor_y += font_line_height();
    scroll_if_needed();
}

static void draw_glyph(uint32_t codepoint)
{
    const struct glyph *glyph = font_get_glyph(codepoint);
    if (glyph == 0) {
        return;
    }

    if (glyph->advance > framebuffer->width - cursor_x) {
        new_line();
    }

    int64_t left = (int64_t)cursor_x + glyph->bearing_x;
    int64_t top = (int64_t)cursor_y + font_ascent() - glyph->bearing_y;
    for (uint16_t row = 0; row < glyph->height; ++row) {
        for (uint16_t column = 0; column < glyph->width; ++column) {
            int64_t x = left + column;
            int64_t y = top + row;
            if (x >= 0 && y >= 0) {
                uint8_t alpha = glyph->alpha[(size_t)row * glyph->width + column];
                put_pixel((size_t)x, (size_t)y, COLOR_TEXT, alpha);
            }
        }
    }

    cursor_x += glyph->advance;
    if (cursor_x >= framebuffer->width) {
        new_line();
    }
}

static int decode_utf8(const uint8_t *text, size_t length,
                       uint32_t *codepoint, size_t *consumed)
{
    uint8_t first = text[0];
    if (first < 0x80) {
        *codepoint = first;
        *consumed = 1;
        return 1;
    }

    unsigned int count;
    uint32_t value;
    uint32_t minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        *codepoint = '?';
        *consumed = 1;
        return 0;
    }

    if (length < count) {
        *codepoint = '?';
        *consumed = 1;
        return 0;
    }
    for (unsigned int index = 1; index < count; ++index) {
        uint8_t continuation = text[index];
        if ((continuation & 0xc0) != 0x80) {
            *codepoint = '?';
            *consumed = 1;
            return 0;
        }
        value = (value << 6) | (continuation & 0x3f);
    }

    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        *codepoint = '?';
        *consumed = 1;
        return 0;
    }

    *codepoint = value;
    *consumed = count;
    return 1;
}

void console_init(void)
{
    if (!boot_get_framebuffer(&framebuffer) ||
        framebuffer->address == 0 || framebuffer->bits_per_pixel != 32 ||
        framebuffer->red_mask_size == 0 ||
        framebuffer->green_mask_size == 0 ||
        framebuffer->blue_mask_size == 0 ||
        framebuffer->red_mask_size > 16 ||
        framebuffer->green_mask_size > 16 ||
        framebuffer->blue_mask_size > 16 ||
        framebuffer->red_mask_shift + framebuffer->red_mask_size > 32 ||
        framebuffer->green_mask_shift + framebuffer->green_mask_size > 32 ||
        framebuffer->blue_mask_shift + framebuffer->blue_mask_size > 32 ||
        font_line_height() == 0 ||
        font_line_height() > framebuffer->height) {
        serial_write("REA OS: framebuffer or font unavailable\r\n");
        for (;;) {
            __asm__ volatile ("pause");
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    cursor_visible = 0;
    console_clear();
}

void console_clear(void)
{
    cursor_begin_console_update();
    fill_rect(0, 0, framebuffer->width, framebuffer->height, COLOR_BLACK);
    cursor_x = 0;
    cursor_y = 0;
    cursor_end_console_update();
}

void console_putc(char character)
{
    if (character == '\r') {
        return;
    }
    if (character == '\n') {
        cursor_begin_console_update();
        new_line();
        cursor_end_console_update();
        return;
    }
    if (character == '\b') {
        return;
    }
    cursor_begin_console_update();
    if ((uint8_t)character < 0x80) {
        draw_glyph((uint8_t)character);
    } else {
        draw_glyph('?');
    }
    cursor_end_console_update();
}

void console_write_n(const char *text, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)text;
    size_t offset = 0;

    cursor_begin_console_update();
    while (offset < length) {
        uint32_t codepoint;
        size_t consumed;
        decode_utf8(bytes + offset, length - offset, &codepoint, &consumed);
        if (codepoint == '\r') {
            offset += consumed;
            continue;
        }
        if (codepoint == '\n') {
            new_line();
        } else if (codepoint >= 32) {
            draw_glyph(codepoint);
        }
        offset += consumed;
    }
    cursor_end_console_update();
}

void console_write(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    console_write_n(text, length);
}

void console_get_cursor_position(size_t *x, size_t *y)
{
    if (x != 0) {
        *x = cursor_x;
    }
    if (y != 0) {
        *y = cursor_y;
    }
}

void console_set_cursor_position(size_t x, size_t y)
{
    if (framebuffer == 0) {
        return;
    }
    cursor_begin_console_update();
    if (x >= framebuffer->width) {
        x = framebuffer->width - 1u;
    }
    if (y >= framebuffer->height) {
        y = framebuffer->height - font_line_height();
    }
    cursor_x = x;
    cursor_y = y;
    cursor_end_console_update();
}

void console_clear_line_from(size_t y, size_t x)
{
    if (framebuffer == 0) {
        return;
    }
    cursor_begin_console_update();
    if (y < framebuffer->height && x < framebuffer->width) {
        fill_rect(x, y, framebuffer->width - x,
                  font_line_height(), COLOR_BLACK);
    }
    cursor_end_console_update();
}

void console_erase_input(size_t x, size_t y, uint16_t advance)
{
    cursor_begin_console_update();
    size_t erase_width = advance == 0 ? 1 : advance;
    fill_rect(x, y, erase_width, font_line_height(), COLOR_BLACK);
    cursor_x = x;
    cursor_y = y;
    cursor_end_console_update();
}

void console_cursor_show(void)
{
    if (!cursor_visible) {
        cursor_begin_console_update();
        for (size_t y = 0; y < font_line_height(); ++y) {
            for (size_t x = 0; x < CURSOR_WIDTH; ++x) {
                invert_pixel(cursor_x + x, cursor_y + y);
            }
        }
        cursor_visible = 1;
        cursor_end_console_update();
    }
}

void console_cursor_hide(void)
{
    if (cursor_visible) {
        cursor_begin_console_update();
        for (size_t y = 0; y < font_line_height(); ++y) {
            for (size_t x = 0; x < CURSOR_WIDTH; ++x) {
                invert_pixel(cursor_x + x, cursor_y + y);
            }
        }
        cursor_visible = 0;
        cursor_end_console_update();
    }
}
