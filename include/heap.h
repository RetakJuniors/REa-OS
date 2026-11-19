#ifndef REA_HEAP_H
#define REA_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_stats {
    uint64_t mapped_bytes;
    uint64_t used_bytes;
};

int heap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void kfree(void *pointer);
int heap_get_stats(struct heap_stats *stats);

#endif
