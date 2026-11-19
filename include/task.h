#ifndef REA_TASK_H
#define REA_TASK_H

#include <stddef.h>
#include <stdint.h>

enum task_state {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_EXITED
};

struct task;

struct task_info {
    uint64_t id;
    char name[32];
    enum task_state state;
    size_t stack_size;
};

typedef void (*task_entry_fn)(void *argument);

void scheduler_init(void);
struct task *task_create(const char *name, task_entry_fn entry,
                         void *argument, size_t stack_size);
void task_yield(void);
void task_exit(void);
void task_sleep(uint64_t ticks);
void scheduler_schedule(void);
size_t task_count(void);
int task_get_at(size_t index, struct task_info *info);
const char *task_state_name(enum task_state state);

#endif
