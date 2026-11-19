#include "boot.h"
#include "cursor.h"
#include "cursor_data.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 128
#define HEIGHT 128

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

static int framebuffer_is_black(void)
{
    for (size_t index = 0; index < WIDTH * HEIGHT; ++index) {
        if (pixels[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int verify_default_pixels(const struct cursor_image *image,
                                 int32_t pointer_x, int32_t pointer_y)
{
    int64_t left = (int64_t)pointer_x - image->hotspot_x;
    int64_t top = (int64_t)pointer_y - image->hotspot_y;
    int visible_pixel_found = 0;

    for (uint16_t row = 0; row < image->height; ++row) {
        for (uint16_t column = 0; column < image->width; ++column) {
            uint32_t source = image->argb[(size_t)row * image->width + column];
            uint8_t alpha = (uint8_t)(source >> 24);
            int64_t x = left + column;
            int64_t y = top + row;
            if (alpha == 0 || x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) {
                continue;
            }

            uint8_t red = (uint8_t)((((source >> 16) & 0xff) * alpha + 127) /
                                    255);
            uint8_t green = (uint8_t)((((source >> 8) & 0xff) * alpha + 127) /
                                      255);
            uint8_t blue = (uint8_t)(((source & 0xff) * alpha + 127) / 255);
            uint32_t expected = ((uint32_t)red << 16) |
                                ((uint32_t)green << 8) | blue;
            if (pixels[(size_t)y * WIDTH + (size_t)x] != expected) {
                return 0;
            }
            if (expected != 0) {
                visible_pixel_found = 1;
            }
        }
    }
    return visible_pixel_found;
}

int main(void)
{
    size_t image_count = 0;
    const struct cursor_image *images = cursor_get_images(&image_count);
    const struct cursor_image *default_image = cursor_get_default();
    int32_t x;
    int32_t y;

    if (images == 0 || image_count != CURSOR_IMAGE_COUNT ||
        default_image == 0 || strcmp(default_image->name, "arrow.png") != 0 ||
        default_image->hotspot_x != 22 || default_image->hotspot_y != 1) {
        fputs("Default cursor or hotspot is incorrect\n", stderr);
        return 1;
    }
    if (!cursor_init()) {
        fputs("Cursor initialization failed\n", stderr);
        return 1;
    }

    cursor_get_position(&x, &y);
    if (x != WIDTH / 2 || y != HEIGHT / 2 ||
        !verify_default_pixels(default_image, x, y)) {
        fputs("Cursor was not drawn at its hotspot-adjusted position\n", stderr);
        return 1;
    }

    cursor_begin_console_update();
    if (!framebuffer_is_black()) {
        fputs("Cursor overlay did not restore the saved background\n", stderr);
        return 1;
    }
    cursor_end_console_update();
    cursor_move(1000, 1000);
    cursor_get_position(&x, &y);
    if (x != WIDTH - 1 || y != HEIGHT - 1) {
        fputs("Cursor did not stay inside the framebuffer bounds\n", stderr);
        return 1;
    }
    cursor_begin_console_update();
    if (!framebuffer_is_black()) {
        fputs("Cursor damaged the screen at its edge\n", stderr);
        return 1;
    }
    cursor_end_console_update();
    cursor_move(-1000, -1000);
    cursor_get_position(&x, &y);
    if (x != 0 || y != 0) {
        fputs("Cursor did not clamp at the top-left edge\n", stderr);
        return 1;
    }

    puts("Cursor tests passed");
    return 0;
}
