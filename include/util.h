#ifndef REA_UTIL_H
#define REA_UTIL_H

#include <stddef.h>

size_t str_length(const char *text);
int str_compare(const char *left, const char *right);
void *memory_copy(void *destination, const void *source, size_t count);
void memory_set(void *destination, int value, size_t count);

#endif
