#include "shell.h"

#include "console.h"
#include "keyboard.h"
#include "serial.h"

#include <stddef.h>

typedef void (*command_handler)(const char *arguments);

struct command {
    const char *name;
    command_handler handler;
};

static char command_line[SHELL_LINE_CAPACITY];
static size_t command_length;

static void print_error(void)
{
    console_write("Error: expected quoted text\n");
    serial_write("Error: expected quoted text\r\n");
}

static void command_print(const char *arguments)
{
    if (*arguments != '"') {
        print_error();
        return;
    }

    const char *end = arguments + 1;
    while (*end != '\0' && *end != '"') {
        ++end;
    }

    if (*end != '"') {
        print_error();
        return;
    }
    const char *trailing = end + 1;
    while (*trailing == ' ' || *trailing == '\t') {
        ++trailing;
    }
    if (*trailing != '\0') {
        print_error();
        return;
    }

    for (const char *character = arguments + 1; character < end; ++character) {
        console_putc(*character);
        char output[2] = { *character, '\0' };
        serial_write(output);
    }
    console_putc('\n');
    serial_write("\r\n");
}

static const struct command commands[] = {
    { "print", command_print }
};

static void execute_command(void)
{
    command_line[command_length] = '\0';

    size_t start = 0;
    while (command_line[start] == ' ' || command_line[start] == '\t') {
        ++start;
    }

    size_t command_end = start;
    while (command_line[command_end] != '\0' &&
           command_line[command_end] != ' ' &&
           command_line[command_end] != '\t') {
        ++command_end;
    }

    char separator = command_line[command_end];
    command_line[command_end] = '\0';
    const char *arguments = separator == '\0' ? "" : command_line + command_end + 1;
    while (*arguments == ' ' || *arguments == '\t') {
        ++arguments;
    }

    int found = 0;
    for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
        if (command_line[start] == '\0') {
            found = 1;
            break;
        }

        const char *left = command_line + start;
        const char *right = commands[index].name;
        while (*left != '\0' && *left == *right) {
            ++left;
            ++right;
        }
        if (*left == '\0' && *right == '\0') {
            commands[index].handler(arguments);
            found = 1;
            break;
        }
    }

    if (!found) {
        console_write("Unknown command\n");
        serial_write("Unknown command\r\n");
    }
}

static void show_prompt(void)
{
    console_write("/> ");
    serial_write("/> ");
}

void shell_init(void)
{
    command_length = 0;
    show_prompt();
}

void shell_handle_char(char character)
{
    if (character == '\n') {
        console_putc('\n');
        serial_write("\r\n");
        execute_command();
        command_length = 0;
        show_prompt();
        return;
    }

    if (character == '\b') {
        if (command_length != 0) {
            --command_length;
            console_putc('\b');
            serial_write("\b \b");
        }
        return;
    }

    if (character < 32 || character > 126 ||
        command_length >= SHELL_LINE_CAPACITY - 1) {
        return;
    }

    command_line[command_length++] = character;
    console_putc(character);
    char output[2] = { character, '\0' };
    serial_write(output);
}
