#ifndef REA_HEAP_H
#define REA_HEAP_H

#include <stddef.h>

int heap_init(void);
void *kmalloc(size_t size);
void kfree(void *pointer);

#endif
