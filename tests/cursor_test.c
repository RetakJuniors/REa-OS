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

static uint32_t downsample_pixel(const struct cursor_image *image,
                                uint16_t column, uint16_t row)
{
    uint32_t alpha_sum = 0;
    uint32_t red_sum = 0;
    uint32_t green_sum = 0;
    uint32_t blue_sum = 0;
    uint32_t sample_count = 0;
    uint16_t source_x = column * CURSOR_DISPLAY_SCALE;
    uint16_t source_y = row * CURSOR_DISPLAY_SCALE;

    for (uint16_t y = 0; y < CURSOR_DISPLAY_SCALE; ++y) {
        for (uint16_t x = 0; x < CURSOR_DISPLAY_SCALE; ++x) {
            uint16_t pixel_x = source_x + x;
            uint16_t pixel_y = source_y + y;
            if (pixel_x >= image->width || pixel_y >= image->height) {
                continue;
            }
            uint32_t pixel =
                image->argb[(size_t)pixel_y * image->width + pixel_x];
            uint32_t alpha = pixel >> 24;
            alpha_sum += alpha;
            red_sum += ((pixel >> 16) & 0xff) * alpha;
            green_sum += ((pixel >> 8) & 0xff) * alpha;
            blue_sum += (pixel & 0xff) * alpha;
            ++sample_count;
        }
    }

    if (alpha_sum == 0 || sample_count == 0) {
        return 0;
    }
    uint32_t alpha = (alpha_sum + sample_count / 2) / sample_count;
    uint32_t red = (red_sum + alpha_sum / 2) / alpha_sum;
    uint32_t green = (green_sum + alpha_sum / 2) / alpha_sum;
    uint32_t blue = (blue_sum + alpha_sum / 2) / alpha_sum;
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

static int verify_default_pixels(const struct cursor_image *image,
                                 int32_t pointer_x, int32_t pointer_y)
{
    uint16_t width = (uint16_t)((image->width + CURSOR_DISPLAY_SCALE - 1) /
                                CURSOR_DISPLAY_SCALE);
    uint16_t height = (uint16_t)((image->height + CURSOR_DISPLAY_SCALE - 1) /
                                 CURSOR_DISPLAY_SCALE);
    uint16_t hotspot_x = (uint16_t)((image->hotspot_x +
                                     CURSOR_DISPLAY_SCALE / 2) /
                                    CURSOR_DISPLAY_SCALE);
    uint16_t hotspot_y = (uint16_t)((image->hotspot_y +
                                     CURSOR_DISPLAY_SCALE / 2) /
                                    CURSOR_DISPLAY_SCALE);
    int64_t left = (int64_t)pointer_x - hotspot_x;
    int64_t top = (int64_t)pointer_y - hotspot_y;
    int visible_pixel_found = 0;

    for (uint16_t row = 0; row < height; ++row) {
        for (uint16_t column = 0; column < width; ++column) {
            uint32_t source = downsample_pixel(image, column, row);
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
        default_image->hotspot_x != 22 || default_image->hotspot_y != 1 ||
        (default_image->width + CURSOR_DISPLAY_SCALE - 1) /
                CURSOR_DISPLAY_SCALE != 32 ||
        (default_image->height + CURSOR_DISPLAY_SCALE - 1) /
                CURSOR_DISPLAY_SCALE != 32) {
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
