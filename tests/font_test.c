#include "font.h"

#include <stdio.h>

int main(void)
{
    for (int character = 33; character <= 126; ++character) {
        int has_pixel = 0;
        for (unsigned int row = 0; row < 7; ++row) {
            has_pixel |= font_row((char)character, row) != 0;
        }
        if (!has_pixel || font_row((char)character, 7) != 0) {
            fprintf(stderr, "Invalid bitmap for ASCII %d\n", character);
            return 1;
        }
    }

    puts("Font tests passed");
    return 0;
}
