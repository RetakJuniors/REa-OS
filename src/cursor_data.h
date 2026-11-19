#ifndef REA_CURSOR_DATA_H
#define REA_CURSOR_DATA_H

#include <stddef.h>
#include <stdint.h>

struct cursor_image {
    const char *name;
    uint16_t width;
    uint16_t height;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
    const uint32_t *argb;
};

#define CURSOR_IMAGE_COUNT 16u
#define CURSOR_MAX_WIDTH 64u
#define CURSOR_MAX_HEIGHT 64u

const struct cursor_image *cursor_get_images(size_t *count);
const struct cursor_image *cursor_get_default(void);

#endif
