#ifndef REA_FONT_H
#define REA_FONT_H

#include <stdint.h>

struct glyph {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t advance;
    const uint8_t *alpha;
};

const struct glyph *font_get_glyph(uint32_t codepoint);
uint16_t font_ascent(void);
uint16_t font_descent(void);
uint16_t font_line_height(void);

#endif
