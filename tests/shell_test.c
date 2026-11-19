#include "shell.h"
#include "font.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int stat_calls;
static unsigned int cursor_show_calls;
static unsigned int cursor_hide_calls;
static uint64_t fake_timer_ticks;
static size_t test_cursor_x;
static size_t test_cursor_y;

static const struct glyph test_glyph = {
    .codepoint = '?',
    .width = 8,
    .height = 16,
    .bearing_x = 0,
    .bearing_y = 12,
    .advance = 8,
    .alpha = 0
};

void stat_command(const char *arguments)
{
    (void)arguments;
    ++stat_calls;
}

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
    if (character == '\n') {
        test_cursor_x = 0;
        test_cursor_y += 16;
    } else {
        test_cursor_x += 8;
    }
}

void console_write(const char *text)
{
    while (*text != '\0') {
        console_putc(*text++);
    }
}

void console_write_n(const char *text, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        console_putc(text[index]);
    }
}

void console_get_cursor_position(size_t *x, size_t *y)
{
    *x = test_cursor_x;
    *y = test_cursor_y;
}

void console_erase_input(size_t x, size_t y, uint16_t advance)
{
    (void)advance;
    test_cursor_x = x;
    test_cursor_y = y;
    if (screen_length != 0) {
        screen_output[--screen_length] = '\0';
    }
}

void console_cursor_show(void)
{
    ++cursor_show_calls;
}

void console_cursor_hide(void)
{
    ++cursor_hide_calls;
}

const struct glyph *font_get_glyph(uint32_t codepoint)
{
    (void)codepoint;
    return &test_glyph;
}

void serial_write(const char *text)
{
    (void)text;
}

uint64_t timer_ticks(void)
{
    return fake_timer_ticks;
}

static int run_case(const char *name, const char *input, const char *expected)
{
    fake_timer_ticks = 0;
    screen_length = 0;
    screen_output[0] = '\0';
    test_cursor_x = 0;
    test_cursor_y = 0;
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

    fake_timer_ticks = 0;
    cursor_show_calls = 0;
    cursor_hide_calls = 0;
    shell_init();
    fake_timer_ticks = 49;
    shell_cursor_update();
    if (cursor_show_calls != 1 || cursor_hide_calls != 0) {
        fputs("Prompt cursor blinked before its interval\n", stderr);
        ++failures;
    }
    fake_timer_ticks = 50;
    shell_cursor_update();
    if (cursor_hide_calls != 1) {
        fputs("Prompt cursor did not hide at its blink interval\n", stderr);
        ++failures;
    }
    fake_timer_ticks = 100;
    shell_cursor_update();
    if (cursor_show_calls != 2) {
        fputs("Prompt cursor did not reappear at its blink interval\n", stderr);
        ++failures;
    }

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
    unsigned int previous_stat_calls = stat_calls;
    failures += run_case(
        "stat dispatch",
        "stat -cpu",
        "/> stat -cpu\n/> ");
    if (stat_calls != previous_stat_calls + 1) {
        fputs("stat command was not dispatched\n", stderr);
        ++failures;
    }

    if (failures != 0) {
        return 1;
    }

    puts("Shell tests passed");
    return 0;
}
