#include "util.h"

size_t str_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

int str_compare(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

void *memory_copy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;

    if (out < in) {
        for (size_t index = 0; index < count; ++index) {
            out[index] = in[index];
        }
    } else if (out > in) {
        while (count != 0) {
            --count;
            out[count] = in[count];
        }
    }
    return destination;
}

void memory_set(void *destination, int value, size_t count)
{
    unsigned char *bytes = destination;
    for (size_t index = 0; index < count; ++index) {
        bytes[index] = (unsigned char)value;
    }
}
