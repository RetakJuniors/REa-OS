#include "shell.h"

#include <stdio.h>
#include <string.h>

static char screen_output[2048];
static size_t screen_length;

void console_putc(char character)
{
    if (character == '\b') {
        if (screen_length != 0) {
            screen_output[--screen_length] = '\0';
        }
        return;
    }

    if (screen_length + 1 < sizeof(screen_output)) {
        screen_output[screen_length++] = character;
        screen_output[screen_length] = '\0';
    }
}

void console_write(const char *text)
{
    while (*text != '\0') {
        console_putc(*text++);
    }
}

void serial_write(const char *text)
{
    (void)text;
}

static int run_case(const char *name, const char *input, const char *expected)
{
    screen_length = 0;
    screen_output[0] = '\0';
    shell_init();

    while (*input != '\0') {
        shell_handle_char(*input++);
    }
    shell_handle_char('\n');

    if (strcmp(screen_output, expected) == 0) {
        return 0;
    }

    fprintf(stderr, "%s failed\nExpected: %s\nActual:   %s\n",
            name, expected, screen_output);
    return 1;
}

int main(void)
{
    int failures = 0;

    failures += run_case(
        "print command",
        "print \"hello\"",
        "/> print \"hello\"\nhello\n/> ");
    failures += run_case(
        "unknown command",
        "help",
        "/> help\nUnknown command\n/> ");
    failures += run_case(
        "missing quote",
        "print \"hello",
        "/> print \"hello\nError: expected quoted text\n/> ");
    failures += run_case(
        "backspace",
        "print \"hello\"\b",
        "/> print \"hello\nError: expected quoted text\n/> ");
    failures += run_case(
        "trailing spaces",
        "print \"hello\"  ",
        "/> print \"hello\"  \nhello\n/> ");

    if (failures != 0) {
        return 1;
    }

    puts("Shell tests passed");
    return 0;
}
