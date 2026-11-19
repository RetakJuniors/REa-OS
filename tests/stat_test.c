#include "memory.h"
#include "heap.h"
#include "stat.h"

#include <stdio.h>
#include <string.h>

static char output[512];
static size_t output_length;

void console_write(const char *text)
{
    while (*text != '\0' && output_length + 1 < sizeof(output)) {
        output[output_length++] = *text++;
    }
    output[output_length] = '\0';
}

void serial_write(const char *text)
{
    (void)text;
}

uint64_t timer_cpu_usage_percent(void)
{
    return 12;
}

int memory_get_stats(struct memory_stats *stats)
{
    stats->total_bytes = 512 * 1024 * 1024;
    stats->used_bytes = 45 * 1024 * 1024;
    stats->free_bytes = 467 * 1024 * 1024;
    return 1;
}

int heap_get_stats(struct heap_stats *stats)
{
    stats->mapped_bytes = 4u * 1024u * 1024u;
    stats->used_bytes = 1u * 1024u * 1024u;
    return 1;
}

static int check(const char *arguments, const char *expected)
{
    output_length = 0;
    output[0] = '\0';
    stat_command(arguments);
    if (strcmp(output, expected) == 0) {
        return 0;
    }

    fprintf(stderr, "stat %s failed: %s\n", arguments, output);
    return 1;
}

int main(void)
{
    int failures = 0;
    failures += check("-cpu", "CPU: 12%\n");
    failures += check("-ram",
                      "RAM: 45 MB / 512 MB (kullanilan / toplam, 467 MB bos)\n"
                      "Heap: 1 MB used / 4 MB mapped\n");
    failures += check("all",
                      "CPU: 12%\n"
                      "RAM: 45 MB / 512 MB (kullanilan / toplam, 467 MB bos)\n"
                      "Heap: 1 MB used / 4 MB mapped\n");
    failures += check("", "Usage: stat -cpu | -ram | all\n");
    failures += check("-x", "Usage: stat -cpu | -ram | all\n");
    failures += check("-cpu extra", "Usage: stat -cpu | -ram | all\n");

    if (failures != 0) {
        return 1;
    }
    puts("Stat tests passed");
    return 0;
}
