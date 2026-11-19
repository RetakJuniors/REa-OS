#include <stdio.h>

extern const char keyboard_plain[128];
extern const char keyboard_shifted[128];

int main(void)
{
    if (keyboard_plain[0x1c] != '\n' ||
        keyboard_plain[0x0e] != '\b' ||
        keyboard_plain[0x39] != ' ') {
        fputs("Common key mapping failed\n", stderr);
        return 1;
    }

#if defined(REA_KEYBOARD_LAYOUT_US)
    if (keyboard_shifted[0x03] != '@' ||
        keyboard_plain[0x1a] != '[' ||
        keyboard_shifted[0x28] != '"') {
        fputs("US QWERTY mapping failed\n", stderr);
        return 1;
    }
#else
    if (keyboard_shifted[0x03] != '"' ||
        keyboard_plain[0x0c] != '-' ||
        keyboard_plain[0x1a] != 'g') {
        fputs("Turkish Q mapping failed\n", stderr);
        return 1;
    }
#endif

    puts("Keyboard layout test passed");
    return 0;
}
