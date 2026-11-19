#include "heap.h"
#include "task.h"
#include "timer.h"

#include <stdio.h>
#include <stdlib.h>

static uint64_t test_ticks;
static volatile int sequence[8];
static volatile unsigned int sequence_length;

void *kmalloc(size_t size)
{
    return malloc(size);
}

void *kcalloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void kfree(void *pointer)
{
    free(pointer);
}

uint64_t timer_ticks(void)
{
    return test_ticks;
}

void timer_wait_for_interrupt(void)
{
    ++test_ticks;
}

static void worker_a(void *argument)
{
    (void)argument;
    sequence[sequence_length++] = 1;
    task_yield();
    sequence[sequence_length++] = 3;
}

static void worker_b(void *argument)
{
    (void)argument;
    sequence[sequence_length++] = 2;
    task_yield();
    sequence[sequence_length++] = 4;
    task_sleep(2);
    sequence[sequence_length++] = 5;
}

static int fail(const char *message)
{
    fprintf(stderr, "Scheduler test failed: %s\n", message);
    return 1;
}

int main(void)
{
    scheduler_init();
    if (task_create("bad", worker_a, 0, 1024) != 0 ||
        task_create("bad", 0, 0, 4096) != 0) {
        return fail("reject invalid task");
    }
    if (task_create("worker-a", worker_a, 0, 0) == 0 ||
        task_create("worker-b", worker_b, 0, 8192) == 0) {
        return fail("create two kernel threads");
    }

    task_yield();
    if (sequence_length != 2 || sequence[0] != 1 || sequence[1] != 2) {
        return fail("round-robin cooperative switch");
    }
    task_sleep(2);
    while (sequence_length < 5) {
        timer_wait_for_interrupt();
        task_yield();
    }
    if (sequence[2] != 3 || sequence[3] != 4 || sequence[4] != 5) {
        return fail("resume, sleep, and complete tasks");
    }
    if (task_count() != 1) {
        return fail("reap exited tasks and keep bootstrap");
    }
    struct task_info info;
    if (!task_get_at(0, &info) || info.state != TASK_RUNNING ||
        info.stack_size != 0) {
        return fail("bootstrap task state");
    }
    task_yield();
    if (task_count() != 1) {
        return fail("safe idle after worker exit");
    }

    puts("Scheduler tests passed");
    return 0;
}
