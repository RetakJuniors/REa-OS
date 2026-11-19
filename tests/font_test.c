#include "font.h"

#include <stdio.h>

int main(void)
{
    const uint32_t ranges[][2] = {
        { 0x0020, 0x007e },
        { 0x00a0, 0x00ff },
        { 0x0100, 0x017f }
    };

    for (unsigned int range = 0; range < 3; ++range) {
        for (uint32_t codepoint = ranges[range][0];
             codepoint <= ranges[range][1]; ++codepoint) {
            const struct glyph *glyph = font_get_glyph(codepoint);
            if (glyph == 0 || glyph->codepoint != codepoint ||
                (glyph->width != 0 && glyph->height != 0 &&
                 glyph->alpha == 0)) {
                fprintf(stderr, "Invalid glyph for U+%04X\n", codepoint);
                return 1;
            }
        }
    }

    if (font_get_glyph(0x10ffff)->codepoint != '?' ||
        font_ascent() == 0 ||
        font_line_height() < font_ascent() + font_descent()) {
        fputs("Font fallback or metrics are invalid\n", stderr);
        return 1;
    }

    const struct glyph *space = font_get_glyph(' ');
    const struct glyph *capital_i_dotted = font_get_glyph(0x0130);
    const struct glyph *small_dotless_i = font_get_glyph(0x0131);
    if (space->advance == 0 || capital_i_dotted->codepoint != 0x0130 ||
        small_dotless_i->codepoint != 0x0131) {
        fputs("Space or Turkish glyph is missing\n", stderr);
        return 1;
    }

    puts("Font tests passed");
    return 0;
}
