#include "cursor.h"

#include "boot.h"
#include "cursor_data.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static struct limine_framebuffer *framebuffer;
static const struct cursor_image *active_image;
static uint32_t saved_pixels[(size_t)CURSOR_MAX_WIDTH * CURSOR_MAX_HEIGHT];
static int64_t pointer_x;
static int64_t pointer_y;
static int initialized;
static int drawn;

static uint16_t display_dimension(uint16_t source_dimension)
{
    return (uint16_t)((source_dimension + CURSOR_DISPLAY_SCALE - 1) /
                      CURSOR_DISPLAY_SCALE);
}

static uint16_t display_hotspot(uint16_t source_hotspot)
{
    return (uint16_t)((source_hotspot + CURSOR_DISPLAY_SCALE / 2) /
                      CURSOR_DISPLAY_SCALE);
}

static uint8_t expand_channel(uint32_t pixel, uint8_t shift, uint8_t size)
{
    uint32_t maximum = (1u << size) - 1;
    uint32_t value = (pixel >> shift) & maximum;
    return (uint8_t)((value * 255 + maximum / 2) / maximum);
}

static uint32_t scale_channel(uint8_t value, uint8_t size)
{
    uint32_t maximum = (1u << size) - 1;
    return ((uint32_t)value * maximum + 127) / 255;
}

static uint32_t pack_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    return (scale_channel(red, framebuffer->red_mask_size) <<
            framebuffer->red_mask_shift) |
           (scale_channel(green, framebuffer->green_mask_size) <<
            framebuffer->green_mask_shift) |
           (scale_channel(blue, framebuffer->blue_mask_size) <<
            framebuffer->blue_mask_shift);
}

static uint32_t *framebuffer_pixel(size_t x, size_t y)
{
    return (uint32_t *)((uint8_t *)framebuffer->address +
                        y * framebuffer->pitch + x * sizeof(uint32_t));
}

static int64_t image_left(void)
{
    return pointer_x - display_hotspot(active_image->hotspot_x);
}

static int64_t image_top(void)
{
    return pointer_y - display_hotspot(active_image->hotspot_y);
}

static uint32_t downsample_pixel(uint16_t column, uint16_t row)
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
            if (pixel_x >= active_image->width ||
                pixel_y >= active_image->height) {
                continue;
            }

            uint32_t pixel =
                active_image->argb[(size_t)pixel_y * active_image->width +
                                   pixel_x];
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

static void restore_pixels(void)
{
    int64_t left = image_left();
    int64_t top = image_top();
    uint16_t width = display_dimension(active_image->width);
    uint16_t height = display_dimension(active_image->height);

    for (uint16_t row = 0; row < height; ++row) {
        int64_t y = top + row;
        if (y < 0 || (uint64_t)y >= framebuffer->height) {
            continue;
        }
        for (uint16_t column = 0; column < width; ++column) {
            int64_t x = left + column;
            if (x < 0 || (uint64_t)x >= framebuffer->width) {
                continue;
            }
            *framebuffer_pixel((size_t)x, (size_t)y) =
                saved_pixels[(size_t)row * width + column];
        }
    }
}

static uint32_t blend_pixel(uint32_t source, uint32_t destination)
{
    uint8_t alpha = (uint8_t)(source >> 24);
    uint8_t inverse_alpha = (uint8_t)(255 - alpha);
    uint8_t source_red = (uint8_t)(source >> 16);
    uint8_t source_green = (uint8_t)(source >> 8);
    uint8_t source_blue = (uint8_t)source;
    uint8_t destination_red = expand_channel(
        destination, framebuffer->red_mask_shift, framebuffer->red_mask_size);
    uint8_t destination_green = expand_channel(
        destination, framebuffer->green_mask_shift, framebuffer->green_mask_size);
    uint8_t destination_blue = expand_channel(
        destination, framebuffer->blue_mask_shift, framebuffer->blue_mask_size);

    uint8_t red = (uint8_t)(((uint16_t)source_red * alpha +
                             (uint16_t)destination_red * inverse_alpha + 127) /
                            255);
    uint8_t green = (uint8_t)(((uint16_t)source_green * alpha +
                               (uint16_t)destination_green * inverse_alpha + 127) /
                              255);
    uint8_t blue = (uint8_t)(((uint16_t)source_blue * alpha +
                              (uint16_t)destination_blue * inverse_alpha + 127) /
                             255);
    return pack_rgb(red, green, blue);
}

static void draw_pixels(void)
{
    int64_t left = image_left();
    int64_t top = image_top();
    uint16_t width = display_dimension(active_image->width);
    uint16_t height = display_dimension(active_image->height);

    for (uint16_t row = 0; row < height; ++row) {
        int64_t y = top + row;
        for (uint16_t column = 0; column < width; ++column) {
            int64_t x = left + column;
            size_t index = (size_t)row * width + column;
            if (x < 0 || y < 0 || (uint64_t)x >= framebuffer->width ||
                (uint64_t)y >= framebuffer->height) {
                continue;
            }

            uint32_t *pixel = framebuffer_pixel((size_t)x, (size_t)y);
            saved_pixels[index] = *pixel;
            uint32_t argb = downsample_pixel(column, row);
            if ((argb >> 24) != 0) {
                *pixel = blend_pixel(argb, *pixel);
            }
        }
    }
    drawn = 1;
}

int cursor_init(void)
{
    if (!boot_get_framebuffer(&framebuffer) ||
        framebuffer->address == 0 || framebuffer->width == 0 ||
        framebuffer->height == 0 || framebuffer->width > INT32_MAX ||
        framebuffer->height > INT32_MAX ||
        framebuffer->bits_per_pixel != 32 ||
        framebuffer->red_mask_size == 0 ||
        framebuffer->green_mask_size == 0 ||
        framebuffer->blue_mask_size == 0 ||
        framebuffer->red_mask_size > 16 ||
        framebuffer->green_mask_size > 16 ||
        framebuffer->blue_mask_size > 16 ||
        framebuffer->red_mask_shift + framebuffer->red_mask_size > 32 ||
        framebuffer->green_mask_shift + framebuffer->green_mask_size > 32 ||
        framebuffer->blue_mask_shift + framebuffer->blue_mask_size > 32) {
        serial_write("REA OS: cursor framebuffer unavailable\r\n");
        return 0;
    }

    active_image = cursor_get_default();
    if (active_image == 0 || active_image->width == 0 ||
        active_image->height == 0 ||
        active_image->width > CURSOR_MAX_WIDTH ||
        active_image->height > CURSOR_MAX_HEIGHT) {
        serial_write("REA OS: cursor image unavailable\r\n");
        return 0;
    }

    pointer_x = (int64_t)(framebuffer->width / 2);
    pointer_y = (int64_t)(framebuffer->height / 2);
    initialized = 1;
    draw_pixels();
    serial_write("REA OS: cursor initialized\r\n");
    return 1;
}

void cursor_move(int32_t delta_x, int32_t delta_y)
{
    if (!initialized) {
        return;
    }

    if (drawn) {
        restore_pixels();
        drawn = 0;
    }

    pointer_x += delta_x;
    pointer_y += delta_y;
    if (pointer_x < 0) {
        pointer_x = 0;
    } else if ((uint64_t)pointer_x >= framebuffer->width) {
        pointer_x = (int64_t)framebuffer->width - 1;
    }
    if (pointer_y < 0) {
        pointer_y = 0;
    } else if ((uint64_t)pointer_y >= framebuffer->height) {
        pointer_y = (int64_t)framebuffer->height - 1;
    }
    draw_pixels();
}

void cursor_get_position(int32_t *x, int32_t *y)
{
    if (x != 0) {
        *x = (int32_t)pointer_x;
    }
    if (y != 0) {
        *y = (int32_t)pointer_y;
    }
}

void cursor_begin_console_update(void)
{
    if (initialized && drawn) {
        restore_pixels();
        drawn = 0;
    }
}

void cursor_end_console_update(void)
{
    if (initialized && !drawn) {
        draw_pixels();
    }
}
