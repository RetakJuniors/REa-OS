#ifndef REA_PMM_H
#define REA_PMM_H

#include "memory.h"

#include <stdint.h>

#define PMM_PAGE_SIZE 4096u

struct pmm_stats {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
};

int pmm_init(const struct limine_memmap_response *memory_map,
             uint64_t hhdm_offset);
void *pmm_alloc_page(void);
int pmm_free_page(void *hhdm_page);
int pmm_get_stats(struct pmm_stats *stats);
int pmm_get_memory_stats(struct memory_stats *stats);

#endif
