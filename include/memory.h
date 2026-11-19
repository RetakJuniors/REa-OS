#ifndef REA_MEMORY_H
#define REA_MEMORY_H

#include <stdint.h>

struct limine_memmap_response;

struct memory_stats {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
};

int memory_init(const struct limine_memmap_response *memory_map,
                uint64_t hhdm_offset);
int memory_get_stats(struct memory_stats *stats);
void *memory_alloc_page(void);
int memory_free_page(void *page);

#endif
