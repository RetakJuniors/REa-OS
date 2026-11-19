#include "boot.h"
#include "console.h"
#include "font.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 64
#define HEIGHT 60

static uint32_t pixels[WIDTH * HEIGHT];
static struct limine_framebuffer test_framebuffer = {
    .address = pixels,
    .width = WIDTH,
    .height = HEIGHT,
    .pitch = WIDTH * sizeof(uint32_t),
    .bits_per_pixel = 32,
    .red_mask_size = 8,
    .red_mask_shift = 16,
    .green_mask_size = 8,
    .green_mask_shift = 8,
    .blue_mask_size = 8,
    .blue_mask_shift = 0
};

int boot_get_framebuffer(struct limine_framebuffer **framebuffer)
{
    *framebuffer = &test_framebuffer;
    return 1;
}

void serial_write(const char *text)
{
    (void)text;
}

static int same_pixels(const uint32_t *left, const uint32_t *right)
{
    return memcmp(left, right, sizeof(pixels)) == 0;
}

int main(void)
{
    uint32_t invalid_output[WIDTH * HEIGHT];
    uint32_t fallback_output[WIDTH * HEIGHT];
    size_t cursor_x;
    size_t cursor_y;

    console_init();
    console_write_n("\xc5\x9e", 2);
    console_get_cursor_position(&cursor_x, &cursor_y);
    if (cursor_x != font_get_glyph(0x015e)->advance || cursor_y != 0) {
        fputs("UTF-8 Turkish glyph was not laid out correctly\n", stderr);
        return 1;
    }

    console_clear();
    console_write_n("\xff", 1);
    memcpy(invalid_output, pixels, sizeof(pixels));
    console_clear();
    console_write_n("?", 1);
    memcpy(fallback_output, pixels, sizeof(pixels));
    if (!same_pixels(invalid_output, fallback_output)) {
        fputs("Invalid UTF-8 did not draw the replacement glyph\n", stderr);
        return 1;
    }

    console_clear();
    console_write_n("WWWWWW", 6);
    console_get_cursor_position(&cursor_x, &cursor_y);
    if (cursor_y == 0) {
        fputs("Text did not wrap at the framebuffer edge\n", stderr);
        return 1;
    }

    console_clear();
    console_write("A");
    console_get_cursor_position(&cursor_x, &cursor_y);
    const struct glyph *capital_a = font_get_glyph('A');
    console_erase_input(0, 0, capital_a->advance);
    console_get_cursor_position(&cursor_x, &cursor_y);
    if (cursor_x != 0 || cursor_y != 0 || pixels[0] != 0) {
        fputs("Backspace layout reset failed\n", stderr);
        return 1;
    }
    for (size_t index = 0; index < WIDTH * HEIGHT; ++index) {
        if (pixels[index] != 0) {
            fputs("Backspace did not clear the glyph area\n", stderr);
            return 1;
        }
    }

    puts("Console tests passed");
    return 0;
}
