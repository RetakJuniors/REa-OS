#include "task.h"

const char *task_state_name(enum task_state state)
{
    switch (state) {
    case TASK_READY:
        return "READY";
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_BLOCKED:
        return "BLOCKED";
    case TASK_EXITED:
        return "EXITED";
    }
    return "UNKNOWN";
}
