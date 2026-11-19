#include "task.h"

#include "heap.h"
#include "timer.h"

#include <stdint.h>

#define TASK_STACK_DEFAULT (16u * 1024u)
#define TASK_STACK_MIN 4096u
#define TASK_STACK_MAX (1024u * 1024u)
#define TASK_STACK_CANARY 0x5245415441534b31ull

struct task_context {
    uintptr_t stack_pointer;
};

struct task {
    uint64_t id;
    char name[32];
    enum task_state state;
    task_entry_fn entry;
    void *argument;
    void *stack_allocation;
    uintptr_t stack_base;
    uintptr_t stack_top;
    size_t stack_size;
    uint64_t wake_tick;
    struct task_context context;
    struct task *next;
    int bootstrap;
};

extern void task_context_switch(struct task_context *old_context,
                                const struct task_context *new_context);

static struct task bootstrap_task;
static struct task *current_task;
static struct task *last_task;
static uint64_t next_task_id = 1;
static size_t task_total;
static int initialized;

static size_t string_length(const char *text)
{
    size_t length = 0;
    if (text == 0) {
        return 0;
    }
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void string_copy(char *destination, const char *source, size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    size_t length = string_length(source);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    for (size_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
    destination[length] = '\0';
}

static int task_stack_valid(const struct task *task, uintptr_t marker)
{
    if (task->bootstrap) {
        return 1;
    }
    if (*(const uint64_t *)task->stack_base != TASK_STACK_CANARY) {
        return 0;
    }
    return marker >= task->stack_base + sizeof(uint64_t) &&
           marker < task->stack_top;
}

static void task_bootstrap(void)
{
    struct task *task = current_task;
    if (task == 0 || task->entry == 0) {
        task_exit();
    }
    task->entry(task->argument);
    task_exit();
}

void scheduler_init(void)
{
    if (initialized) {
        return;
    }
    bootstrap_task.id = next_task_id++;
    string_copy(bootstrap_task.name, "kernel", sizeof(bootstrap_task.name));
    bootstrap_task.state = TASK_RUNNING;
    bootstrap_task.bootstrap = 1;
    bootstrap_task.next = &bootstrap_task;
    current_task = &bootstrap_task;
    last_task = &bootstrap_task;
    task_total = 1;
    initialized = 1;
}

struct task *task_create(const char *name, task_entry_fn entry,
                         void *argument, size_t stack_size)
{
    if (!initialized || name == 0 || name[0] == '\0' || entry == 0) {
        return 0;
    }
    if (stack_size == 0) {
        stack_size = TASK_STACK_DEFAULT;
    }
    if (stack_size < TASK_STACK_MIN || stack_size > TASK_STACK_MAX ||
        stack_size > SIZE_MAX - 15u) {
        return 0;
    }
    stack_size = (stack_size + 15u) & ~(size_t)15u;

    struct task *task = kcalloc(1, sizeof(*task));
    if (task == 0) {
        return 0;
    }
    void *stack = kmalloc(stack_size);
    if (stack == 0) {
        kfree(task);
        return 0;
    }
    if ((uintptr_t)stack > UINTPTR_MAX - stack_size) {
        kfree(stack);
        kfree(task);
        return 0;
    }

    task->id = next_task_id++;
    string_copy(task->name, name, sizeof(task->name));
    task->state = TASK_READY;
    task->entry = entry;
    task->argument = argument;
    task->stack_allocation = stack;
    task->stack_base = (uintptr_t)stack;
    task->stack_top = task->stack_base + stack_size;
    task->stack_size = stack_size;
    *(uint64_t *)task->stack_base = TASK_STACK_CANARY;

    uintptr_t aligned_top = task->stack_top & ~(uintptr_t)15u;
    uintptr_t *frame = (uintptr_t *)(aligned_top - 8u * sizeof(uintptr_t));
    for (unsigned int index = 0; index < 6; ++index) {
        frame[index] = 0;
    }
    frame[6] = (uintptr_t)task_bootstrap;
    task->context.stack_pointer = (uintptr_t)frame;

    task->next = &bootstrap_task;
    last_task->next = task;
    last_task = task;
    ++task_total;
    return task;
}

static void wake_sleepers(void)
{
    uint64_t now = timer_ticks();
    struct task *task = &bootstrap_task;
    for (size_t index = 0; index < task_total; ++index) {
        if (task->state == TASK_BLOCKED && now >= task->wake_tick) {
            task->state = TASK_READY;
        }
        task = task->next;
    }
}

static struct task *find_ready_task(void)
{
    struct task *task = current_task->next;
    for (size_t index = 0; index < task_total; ++index) {
        if (task->state == TASK_READY) {
            return task;
        }
        task = task->next;
    }
    if (current_task->state == TASK_READY) {
        return current_task;
    }
    return 0;
}

static void reap_exited_tasks(void)
{
    if (task_total <= 1u) {
        return;
    }
    struct task *previous = &bootstrap_task;
    struct task *task = previous->next;
    size_t remaining = task_total - 1u;
    while (remaining-- != 0u && task != &bootstrap_task) {
        struct task *next = task->next;
        if (task != current_task && task->state == TASK_EXITED) {
            previous->next = next;
            if (last_task == task) {
                last_task = previous;
            }
            --task_total;
            kfree(task->stack_allocation);
            kfree(task);
        } else {
            previous = task;
        }
        task = next;
    }
}

void scheduler_schedule(void)
{
    if (!initialized || current_task == 0) {
        return;
    }
    struct task *old_task = current_task;
    uintptr_t stack_marker = (uintptr_t)&old_task;
    if (!task_stack_valid(old_task, stack_marker)) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }
    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }

    struct task *next_task;
    for (;;) {
        wake_sleepers();
        next_task = find_ready_task();
        if (next_task == 0) {
            timer_wait_for_interrupt();
            continue;
        }
        if (next_task == old_task || next_task->bootstrap ||
            (next_task->context.stack_pointer >=
                 next_task->stack_base + 8u &&
             next_task->context.stack_pointer < next_task->stack_top)) {
            break;
        }
        next_task->state = TASK_EXITED;
    }
    if (next_task == old_task) {
        next_task->state = TASK_RUNNING;
        return;
    }

    next_task->state = TASK_RUNNING;
    current_task = next_task;
    task_context_switch(&old_task->context, &next_task->context);
    reap_exited_tasks();
}

void task_yield(void)
{
    if (initialized && current_task != 0 &&
        current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
        scheduler_schedule();
    }
}

void task_exit(void)
{
    if (!initialized || current_task == 0) {
        for (;;) {
            __asm__ volatile ("cli; hlt" ::: "memory");
        }
    }
    current_task->state = TASK_EXITED;
    scheduler_schedule();
    for (;;) {
        __asm__ volatile ("cli; hlt" ::: "memory");
    }
}

void task_sleep(uint64_t ticks)
{
    if (!initialized || current_task == 0) {
        return;
    }
    if (ticks == 0) {
        task_yield();
        return;
    }
    uint64_t now = timer_ticks();
    current_task->wake_tick = UINT64_MAX - now < ticks
                                  ? UINT64_MAX
                                  : now + ticks;
    current_task->state = TASK_BLOCKED;
    scheduler_schedule();
}

size_t task_count(void)
{
    size_t active = 0;
    if (!initialized) {
        return 0;
    }
    struct task *task = &bootstrap_task;
    for (size_t index = 0; index < task_total; ++index) {
        if (task->state != TASK_EXITED) {
            ++active;
        }
        task = task->next;
    }
    return active;
}

int task_get_at(size_t index, struct task_info *info)
{
    if (!initialized || info == 0) {
        return 0;
    }
    struct task *task = &bootstrap_task;
    for (size_t visited = 0; visited < task_total; ++visited) {
        if (task->state != TASK_EXITED) {
            if (index == 0) {
                info->id = task->id;
                string_copy(info->name, task->name, sizeof(info->name));
                info->state = task->state;
                info->stack_size = task->bootstrap ? 0 : task->stack_size;
                return 1;
            }
            --index;
        }
        task = task->next;
    }
    return 0;
}
