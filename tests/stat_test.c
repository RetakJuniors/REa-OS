#include "memory.h"
#include "heap.h"
#include "stat.h"
#include "task.h"

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

size_t task_count(void)
{
    return 2;
}

int task_get_at(size_t index, struct task_info *info)
{
    if (index >= 2 || info == 0) {
        return 0;
    }
    info->id = index + 1u;
    snprintf(info->name, sizeof(info->name), "%s", index == 0 ? "kernel" : "worker");
    info->state = index == 0 ? TASK_RUNNING : TASK_READY;
    info->stack_size = index == 0 ? 0 : 16384;
    return 1;
}

const char *task_state_name(enum task_state state)
{
    return state == TASK_RUNNING ? "RUNNING" : "READY";
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
                      "Heap: 1 MB used / 4 MB mapped\n"
                      "Tasks: 2\n"
                      "1 kernel RUNNING stack=bootstrap\n"
                      "2 worker READY stack=16 KB\n");
    failures += check("-task",
                      "Tasks: 2\n"
                      "1 kernel RUNNING stack=bootstrap\n"
                      "2 worker READY stack=16 KB\n");
    failures += check("", "Usage: stat -cpu | -ram | -task | all\n");
    failures += check("-x", "Usage: stat -cpu | -ram | -task | all\n");
    failures += check("-cpu extra", "Usage: stat -cpu | -ram | -task | all\n");

    if (failures != 0) {
        return 1;
    }
    puts("Stat tests passed");
    return 0;
}
