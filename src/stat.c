#include "stat.h"

#include "console.h"
#include "heap.h"
#include "memory.h"
#include "serial.h"
#include "timer.h"

#include <stdint.h>

static int is_space(char character)
{
    return character == ' ' || character == '\t';
}

static int matches_option(const char *arguments, const char *option)
{
    while (*option != '\0' && *arguments == *option) {
        ++arguments;
        ++option;
    }
    if (*option != '\0') {
        return 0;
    }
    while (is_space(*arguments)) {
        ++arguments;
    }
    return *arguments == '\0';
}

static void write_both(const char *text)
{
    console_write(text);
    serial_write(text);
}

static void write_number(uint64_t number)
{
    char digits[21];
    unsigned int length = 0;

    do {
        digits[length++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);

    while (length != 0) {
        char output[2] = { digits[--length], '\0' };
        write_both(output);
    }
}

static void print_cpu(void)
{
    write_both("CPU: ");
    write_number(timer_cpu_usage_percent());
    write_both("%\n");
}

static void print_ram(void)
{
    struct memory_stats stats;
    if (!memory_get_stats(&stats)) {
        write_both("RAM: unavailable\n");
        return;
    }

    const uint64_t mb = 1024 * 1024;
    write_both("RAM: ");
    write_number(stats.used_bytes / mb);
    write_both(" MB / ");
    write_number(stats.total_bytes / mb);
    write_both(" MB (kullanilan / toplam, ");
    write_number(stats.free_bytes / mb);
    write_both(" MB bos)\n");

    struct heap_stats heap;
    if (heap_get_stats(&heap)) {
        write_both("Heap: ");
        write_number(heap.used_bytes / mb);
        write_both(" MB used / ");
        write_number(heap.mapped_bytes / mb);
        write_both(" MB mapped\n");
    }
}

void stat_command(const char *arguments)
{
    if (matches_option(arguments, "-cpu")) {
        print_cpu();
    } else if (matches_option(arguments, "-ram")) {
        print_ram();
    } else if (matches_option(arguments, "all")) {
        print_cpu();
        print_ram();
    } else {
        write_both("Usage: stat -cpu | -ram | all\n");
    }
}
