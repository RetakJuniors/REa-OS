#include "pmm.h"

int pmm_init(const struct limine_memmap_response *memory_map,
             uint64_t hhdm_offset)
{
    return memory_init(memory_map, hhdm_offset);
}

void *pmm_alloc_page(void)
{
    return memory_alloc_page();
}

int pmm_free_page(void *hhdm_page)
{
    return memory_free_page(hhdm_page);
}

int pmm_get_stats(struct pmm_stats *stats)
{
    struct memory_stats memory;
    if (stats == 0 || !memory_get_stats(&memory)) {
        return 0;
    }
    stats->total_pages = memory.total_pages;
    stats->used_pages = memory.used_pages;
    stats->free_pages = memory.free_pages;
    return 1;
}

int pmm_get_memory_stats(struct memory_stats *stats)
{
    return memory_get_stats(stats);
}
