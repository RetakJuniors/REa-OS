#include "shell.h"

#include "console.h"
#include "heap.h"
#include "keyboard.h"
#include "ramfs.h"
#include "pmm.h"
#include "rtc.h"
#include "serial.h"
#include "stat.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>

#define CURSOR_BLINK_TICKS 50u
#define HISTORY_SIZE 32u
#define ARG_COUNT 8u
#define ARG_SIZE 256u

struct argument {
    char text[ARG_SIZE];
    int quoted;
};

struct parsed_line {
    struct argument args[ARG_COUNT];
    size_t count;
    int unterminated;
};

typedef void (*command_handler)(const struct parsed_line *);
struct command {
    const char *name;
    const char *usage;
    const char *description;
    command_handler handler;
};

static char line[SHELL_LINE_CAPACITY];
static size_t line_length;
static size_t cursor_index;
static uint64_t cursor_last_toggle;
static int cursor_visible;
static size_t prompt_y;
static size_t character_x[SHELL_LINE_CAPACITY];
static size_t character_y[SHELL_LINE_CAPACITY];
static char history[HISTORY_SIZE][SHELL_LINE_CAPACITY];
static size_t history_count;
static int history_position;

static int is_space(char character)
{
    return character == ' ' || character == '\t';
}

static int parse_line(const char *text, struct parsed_line *parsed)
{
    parsed->count = 0;
    parsed->unterminated = 0;
    size_t position = 0;
    while (text[position] != '\0') {
        while (is_space(text[position])) ++position;
        if (text[position] == '\0') break;
        if (parsed->count >= ARG_COUNT) return 0;
        struct argument *argument = &parsed->args[parsed->count];
        argument->quoted = 0;
        size_t length = 0;
        if (text[position] == '"') {
            argument->quoted = 1;
            ++position;
            while (text[position] != '\0' && text[position] != '"') {
                if (length + 1u < ARG_SIZE)
                    argument->text[length++] = text[position];
                ++position;
            }
            if (text[position] == '"') ++position;
            else parsed->unterminated = 1;
        } else {
            while (text[position] != '\0' &&
                   !is_space(text[position])) {
                if (length + 1u < ARG_SIZE)
                    argument->text[length++] = text[position];
                ++position;
            }
        }
        argument->text[length] = '\0';
        ++parsed->count;
    }
    return 1;
}

static void both(const char *text)
{
    console_write(text);
    serial_write(text);
}

static void error(const char *message)
{
    both(message);
    both("\n");
}

static void write_number(uint64_t number)
{
    char digits[21];
    unsigned int length = 0;
    do {
        digits[length++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0u);
    while (length != 0u) {
        char output[2] = {digits[--length], '\0'};
        both(output);
    }
}

static void command_ps(const struct parsed_line *parsed)
{
    (void)parsed;
    both("ID NAME STATE STACK\n");
    for (size_t index = 0;; ++index) {
        struct task_info task;
        if (!task_get_at(index, &task)) {
            break;
        }
        write_number(task.id);
        both(" ");
        both(task.name);
        both(" ");
        both(task_state_name(task.state));
        both(" ");
        if (task.stack_size == 0) {
            both("bootstrap\n");
        } else {
            write_number(task.stack_size / 1024u);
            both(" KB\n");
        }
    }
}

static void command_mem(const struct parsed_line *parsed)
{
    (void)parsed;
    struct memory_stats memory;
    struct pmm_stats pages;
    struct heap_stats heap;
    if (!pmm_get_memory_stats(&memory) || !pmm_get_stats(&pages)) {
        error("Memory information unavailable");
        return;
    }
    both("RAM total: ");
    write_number(memory.total_bytes / (1024u * 1024u));
    both(" MB\nRAM free: ");
    write_number(memory.free_bytes / (1024u * 1024u));
    both(" MB\nRAM used: ");
    write_number(memory.used_bytes / (1024u * 1024u));
    both(" MB\nPages total/free/used: ");
    write_number(pages.total_pages);
    both("/");
    write_number(pages.free_pages);
    both("/");
    write_number(pages.used_pages);
    both("\n");
    if (heap_get_stats(&heap)) {
        both("Kernel heap used: ");
        write_number(heap.used_bytes);
        both(" bytes\n");
    }
}

static void print_prompt(void)
{
    char path[128];
    ramfs_format_cwd(path, sizeof(path));
    both(path);
    both(">");
    both(" ");
}

static void show_prompt(void)
{
    print_prompt();
    console_get_cursor_position(0, &prompt_y);
    console_cursor_show();
    cursor_visible = 1;
    cursor_last_toggle = timer_ticks();
}

static void redraw_line(void)
{
    console_cursor_hide();
    cursor_visible = 0;
    console_clear_line_from(prompt_y, 0);
    console_set_cursor_position(0, prompt_y);
    print_prompt();
    for (size_t index = 0; index < line_length; ++index) {
        console_get_cursor_position(&character_x[index],
                                    &character_y[index]);
        console_putc(line[index]);
    }
    console_get_cursor_position(&character_x[line_length],
                                &character_y[line_length]);
    if (cursor_index <= line_length) {
        console_set_cursor_position(character_x[cursor_index],
                                    character_y[cursor_index]);
    }
    console_cursor_show();
    cursor_visible = 1;
    cursor_last_toggle = timer_ticks();
}

static void history_add(const char *text)
{
    if (text[0] == '\0') return;
    if (history_count != 0 &&
        str_compare(history[history_count - 1u], text) == 0) return;
    if (history_count < HISTORY_SIZE) {
        size_t length = str_length(text);
        if (length >= SHELL_LINE_CAPACITY)
            length = SHELL_LINE_CAPACITY - 1u;
        memory_copy(history[history_count], text, length);
        history[history_count][length] = '\0';
        ++history_count;
    } else {
        for (size_t index = 1u; index < HISTORY_SIZE; ++index)
            memory_copy(history[index - 1u], history[index],
                        SHELL_LINE_CAPACITY);
        size_t length = str_length(text);
        if (length >= SHELL_LINE_CAPACITY)
            length = SHELL_LINE_CAPACITY - 1u;
        memory_copy(history[HISTORY_SIZE - 1u], text, length);
        history[HISTORY_SIZE - 1u][length] = '\0';
    }
    history_position = (int)history_count;
}

static void command_print(const struct parsed_line *parsed)
{
    if (parsed->count != 2u || !parsed->args[1].quoted ||
        parsed->unterminated)
        error("Error: expected quoted text");
    else {
        both(parsed->args[1].text);
        both("\n");
    }
}

static void command_stat(const struct parsed_line *parsed)
{
    char arguments[ARG_SIZE] = "";
    if (parsed->count >= 2u)
        memory_copy(arguments, parsed->args[1].text,
                    str_length(parsed->args[1].text) + 1u);
    stat_command(arguments);
}

static void command_ls(const struct parsed_line *parsed)
{
    const char *path = parsed->count >= 2u ? parsed->args[1].text : "";
    struct ramfs_node *directory;
    enum ramfs_result result = ramfs_resolve(path, &directory);
    if (result != RAMFS_OK) { error("Path not found"); return; }
    if (!directory->directory) { error("Not a directory"); return; }

    struct ramfs_node *nodes[256];
    size_t count = 0;
    for (struct ramfs_node *node = directory->child;
         node != 0 && count < 256u; node = node->next)
        nodes[count++] = node;
    for (size_t index = 0; index + 1u < count; ++index)
        for (size_t next = index + 1u; next < count; ++next)
            if (str_compare(nodes[next]->name, nodes[index]->name) < 0) {
                struct ramfs_node *temporary = nodes[index];
                nodes[index] = nodes[next];
                nodes[next] = temporary;
            }
    for (size_t index = 0; index < count; ++index) {
        both(nodes[index]->name);
        if (nodes[index]->directory) {
            both("/\n");
        } else {
            char number[16];
            uint32_t value = nodes[index]->size;
            unsigned int digits = 0;
            do number[digits++] = (char)('0' + value % 10u);
            while ((value /= 10u) != 0);
            both(" ");
            for (unsigned int reverse = digits; reverse != 0; --reverse) {
                char output[2] = {number[reverse - 1u], '\0'};
                both(output);
            }
            both(" bytes\n");
        }
    }
}

static void command_cd(const struct parsed_line *parsed)
{
    if (parsed->count > 2u) { error("Usage: cd [path]"); return; }
    const char *path = parsed->count == 1u ? "/" : parsed->args[1].text;
    if (!ramfs_set_cwd_path(path)) error("Not a directory");
}

static void command_pwd(const struct parsed_line *parsed)
{
    (void)parsed;
    char path[128];
    ramfs_format_cwd(path, sizeof(path));
    both(path);
    both("\n");
}

static void command_mkdir(const struct parsed_line *parsed)
{
    if (parsed->count != 2u) { error("Usage: mkdir <path>"); return; }
    enum ramfs_result result = ramfs_mkdir(parsed->args[1].text);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_EXISTS) error("Already exists");
    else if (result == RAMFS_LIMIT) error("Limit reached");
    else if (result == RAMFS_NO_MEMORY) error("Out of memory");
    else if (result == RAMFS_INVALID_NAME) error("Invalid name");
    else error("Cannot create directory");
}

static void command_rm(const struct parsed_line *parsed)
{
    if (parsed->count != 2u) { error("Usage: rm <path>"); return; }
    enum ramfs_result result = ramfs_remove(parsed->args[1].text);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_ROOT) error("Cannot remove root");
    else if (result == RAMFS_NOT_EMPTY) error("Directory not empty");
    else if (result == RAMFS_NOT_FOUND) error("Path not found");
    else error("Cannot remove");
}

static void command_cat(const struct parsed_line *parsed)
{
    if (parsed->count != 2u) { error("Usage: cat <file>"); return; }
    struct ramfs_node *node;
    enum ramfs_result result = ramfs_resolve(parsed->args[1].text, &node);
    if (result != RAMFS_OK) { error("Path not found"); return; }
    if (node->directory) { error("Is a directory"); return; }
    for (uint32_t index = 0; index < node->size; ++index) {
        unsigned char value = (unsigned char)node->data[index];
        if (value == '\n') {
            console_putc('\n');
            serial_write("\r\n");
        } else if (value < 32u || value > 126u) {
            console_putc('.');
            serial_write(".");
        } else {
            char output[2] = {(char)value, '\0'};
            console_write(output);
            serial_write(output);
        }
    }
    both("\n");
}

static int text_argument(const struct parsed_line *parsed)
{
    return parsed->count == 3u && parsed->args[2].quoted &&
           !parsed->unterminated;
}

static void command_wr(const struct parsed_line *parsed)
{
    if (!text_argument(parsed)) { error("Usage: wr <file> \"text\""); return; }
    const char *text = parsed->args[2].text;
    size_t length = str_length(text);
    if (length >= RAMFS_MAX_FILE_SIZE) { error("File too large"); return; }
    char *data = kmalloc(length + 1u);
    if (data == 0) { error("Out of memory"); return; }
    memory_copy(data, text, length);
    data[length++] = '\n';
    enum ramfs_result result =
        ramfs_write(parsed->args[1].text, data, length);
    kfree(data);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_LIMIT) error("File or RAMFS limit reached");
    else if (result == RAMFS_NO_MEMORY) error("Out of memory");
    else error("Cannot write file");
}

static void command_ap(const struct parsed_line *parsed)
{
    if (!text_argument(parsed)) { error("Usage: ap <file> \"text\""); return; }
    const char *text = parsed->args[2].text;
    size_t length = str_length(text);
    if (length >= RAMFS_MAX_FILE_SIZE) { error("File too large"); return; }
    char *data = kmalloc(length + 1u);
    if (data == 0) { error("Out of memory"); return; }
    memory_copy(data, text, length);
    data[length++] = '\n';
    enum ramfs_result result =
        ramfs_append(parsed->args[1].text, data, length);
    kfree(data);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_LIMIT) error("File or RAMFS limit reached");
    else if (result == RAMFS_NO_MEMORY) error("Out of memory");
    else error("Cannot append file");
}

static void command_cp(const struct parsed_line *parsed)
{
    if (parsed->count != 3u) { error("Usage: cp <source> <target>"); return; }
    enum ramfs_result result =
        ramfs_copy_file(parsed->args[1].text, parsed->args[2].text);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_LIMIT) error("File or RAMFS limit reached");
    else if (result == RAMFS_NO_MEMORY) error("Out of memory");
    else if (result == RAMFS_NOT_FOUND) error("Source not found");
    else error("Cannot copy");
}

static void command_mv(const struct parsed_line *parsed)
{
    if (parsed->count != 3u) { error("Usage: mv <source> <target>"); return; }
    enum ramfs_result result =
        ramfs_move(parsed->args[1].text, parsed->args[2].text);
    if (result == RAMFS_OK) return;
    if (result == RAMFS_ROOT) error("Cannot move root");
    else if (result == RAMFS_LIMIT) error("Cannot move directory");
    else if (result == RAMFS_NOT_FOUND) error("Source not found");
    else if (result == RAMFS_EXISTS) error("Target exists");
    else error("Cannot move");
}

static void command_cls(const struct parsed_line *parsed)
{
    (void)parsed;
    console_clear();
}

static void command_ver(const struct parsed_line *parsed)
{
    (void)parsed;
    both("REA OS v0.0.8a\n");
}

static void command_sys(const struct parsed_line *parsed)
{
    (void)parsed;
    char output[512];
    if (system_format(output, sizeof(output)) != 0) {
        both(output);
        both("\n");
    } else {
        error("System info unavailable");
    }
}

static void number_append(char *output, size_t *length, uint64_t number)
{
    char digits[21];
    unsigned int count = 0;
    do digits[count++] = (char)('0' + number % 10u);
    while ((number /= 10u) != 0);
    while (count != 0) output[(*length)++] = digits[--count];
}

static void command_up(const struct parsed_line *parsed)
{
    (void)parsed;
    uint64_t seconds = timer_ticks() / 100u;
    char output[64];
    size_t length = 0;
    const char prefix[] = "Uptime: ";
    for (size_t index = 0; prefix[index] != '\0'; ++index)
        output[length++] = prefix[index];
    uint64_t values[3] = {seconds / 3600u,
                          (seconds / 60u) % 60u,
                          seconds % 60u};
    const char *labels[3] = {"h ", "m ", "s"};
    for (size_t index = 0; index < 3u; ++index) {
        number_append(output, &length, values[index]);
        for (const char *label = labels[index]; *label != '\0'; ++label)
            output[length++] = *label;
    }
    output[length] = '\0';
    both(output);
    both("\n");
}

static void digits_append(char *output, size_t *length, uint8_t value)
{
    output[(*length)++] = (char)('0' + value / 10u);
    output[(*length)++] = (char)('0' + value % 10u);
}

static void command_dt(const struct parsed_line *parsed)
{
    (void)parsed;
    struct rtc_datetime datetime;
    if (!rtc_read(&datetime)) { error("RTC unavailable"); return; }
    char output[64];
    size_t length = 0;
    number_append(output, &length, datetime.year);
    output[length++] = '-';
    digits_append(output, &length, datetime.month);
    output[length++] = '-';
    digits_append(output, &length, datetime.day);
    output[length++] = ' ';
    digits_append(output, &length, datetime.hour);
    output[length++] = ':';
    digits_append(output, &length, datetime.minute);
    output[length++] = ':';
    digits_append(output, &length, datetime.second);
    const char suffix[] = " (RTC)";
    for (size_t index = 0; suffix[index] != '\0'; ++index)
        output[length++] = suffix[index];
    output[length] = '\0';
    both(output);
    both("\n");
}

static void command_rb(const struct parsed_line *parsed)
{
    (void)parsed;
    system_reboot();
}

static void command_off(const struct parsed_line *parsed)
{
    (void)parsed;
    if (!system_hypervisor() || !system_poweroff()) {
        error("Power off not available, safe to close the window");
    }
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

static void command_help(const struct parsed_line *);

static const struct command commands[] = {
    {"print", "print \"text\"", "Print quoted text", command_print},
    {"stat", "stat -cpu | -ram | -task | all", "Show CPU, RAM and task status", command_stat},
    {"ps", "ps", "List active kernel tasks", command_ps},
    {"mem", "mem", "Show physical memory and heap usage", command_mem},
    {"ls", "ls [path]", "List files and directories", command_ls},
    {"cd", "cd [path]", "Change directory", command_cd},
    {"pwd", "pwd", "Print current directory", command_pwd},
    {"mkdir", "mkdir <path>", "Create directory", command_mkdir},
    {"rm", "rm <path>", "Remove file or empty directory", command_rm},
    {"cat", "cat <file>", "Print file contents", command_cat},
    {"wr", "wr <file> \"text\"", "Write text and newline", command_wr},
    {"ap", "ap <file> \"text\"", "Append text and newline", command_ap},
    {"cp", "cp <source> <target>", "Copy file", command_cp},
    {"mv", "mv <source> <target>", "Move or rename", command_mv},
    {"help", "help", "List commands", command_help},
    {"cls", "cls", "Clear screen", command_cls},
    {"ver", "ver", "Print REA OS version", command_ver},
    {"sys", "sys", "Print system information", command_sys},
    {"up", "up", "Print uptime", command_up},
    {"dt", "dt", "Print RTC date and time", command_dt},
    {"rb", "rb", "Reboot virtual machine", command_rb},
    {"off", "off", "Power off supported hypervisor", command_off}
};

static void command_help(const struct parsed_line *parsed)
{
    (void)parsed;
    for (size_t index = 0;
         index < sizeof(commands) / sizeof(commands[0]); ++index) {
        both(commands[index].name);
        both(" ");
        both(commands[index].usage);
        both(" - ");
        both(commands[index].description);
        both("\n");
    }
}

static void execute_command(void)
{
    line[line_length] = '\0';
    if (line_length == 0u) return;
    struct parsed_line parsed;
    (void)parse_line(line, &parsed);
    if (parsed.count == 0u) return;
    for (size_t index = 0;
         index < sizeof(commands) / sizeof(commands[0]); ++index) {
        if (str_compare(parsed.args[0].text, commands[index].name) == 0) {
            commands[index].handler(&parsed);
            return;
        }
    }
    error("Unknown command");
}

void shell_init(void)
{
    ramfs_init();
    line_length = 0;
    cursor_index = 0;
    cursor_visible = 0;
    history_count = 0;
    history_position = 0;
    line[0] = '\0';
    show_prompt();
}

void shell_cursor_update(void)
{
    uint64_t now = timer_ticks();
    if (now - cursor_last_toggle < CURSOR_BLINK_TICKS) return;
    cursor_last_toggle = now;
    if (cursor_visible) {
        console_cursor_hide();
        cursor_visible = 0;
    } else {
        console_cursor_show();
        cursor_visible = 1;
    }
}

static void history_load(int position)
{
    if (position < 0) position = 0;
    if ((size_t)position >= history_count) return;
    size_t length = str_length(history[position]);
    if (length >= SHELL_LINE_CAPACITY)
        length = SHELL_LINE_CAPACITY - 1u;
    memory_copy(line, history[position], length);
    line[length] = '\0';
    line_length = length;
    cursor_index = length;
    redraw_line();
}

static void handle_event(const struct keyboard_event *event)
{
    if (event->type == KEY_EVENT_LEFT) {
        if (cursor_index != 0) { --cursor_index; redraw_line(); }
    } else if (event->type == KEY_EVENT_RIGHT) {
        if (cursor_index < line_length) { ++cursor_index; redraw_line(); }
    } else if (event->type == KEY_EVENT_HOME) {
        cursor_index = 0; redraw_line();
    } else if (event->type == KEY_EVENT_END) {
        cursor_index = line_length; redraw_line();
    } else if (event->type == KEY_EVENT_DELETE) {
        if (cursor_index < line_length) {
            for (size_t index = cursor_index;
                 index + 1u < line_length; ++index)
                line[index] = line[index + 1u];
            --line_length;
            line[line_length] = '\0';
            redraw_line();
        }
    } else if (event->type == KEY_EVENT_UP) {
        if (history_count != 0 && history_position > 0) {
            --history_position;
            history_load(history_position);
        }
    } else if (event->type == KEY_EVENT_DOWN) {
        if (history_position < (int)history_count - 1) {
            ++history_position;
            history_load(history_position);
        } else if (history_position == (int)history_count - 1) {
            history_position = (int)history_count;
            line_length = 0;
            cursor_index = 0;
            line[0] = '\0';
            redraw_line();
        }
    }
}

void shell_handle_char(char character)
{
    if (character == '\n') {
        console_cursor_hide();
        cursor_visible = 0;
        console_putc('\n');
        serial_write("\r\n");
        execute_command();
        history_add(line);
        line_length = 0;
        cursor_index = 0;
        line[0] = '\0';
        history_position = (int)history_count;
        show_prompt();
        return;
    }
    if (character == '\b') {
        if (cursor_index != 0) {
            --cursor_index;
            for (size_t index = cursor_index;
                 index + 1u < line_length; ++index)
                line[index] = line[index + 1u];
            --line_length;
            line[line_length] = '\0';
            redraw_line();
            serial_write("\b \b");
        }
        return;
    }
    if ((uint8_t)character < 32u || (uint8_t)character > 126u ||
        line_length >= SHELL_LINE_CAPACITY - 1u) return;
    for (size_t index = line_length; index > cursor_index; --index)
        line[index] = line[index - 1u];
    line[cursor_index++] = character;
    ++line_length;
    line[line_length] = '\0';
    redraw_line();
    char output[2] = {character, '\0'};
    serial_write(output);
}

void shell_handle_keyboard_event(const struct keyboard_event *event)
{
    if (event == 0) return;
    if (event->type == KEY_EVENT_CHAR)
        shell_handle_char(event->character);
    else
        handle_event(event);
}
