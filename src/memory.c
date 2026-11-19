#include "memory.h"

#include "boot.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096u
#define PAGE_SHIFT 12u
#define MEMORY_MAP_USABLE 0u

static uint8_t *page_bitmap;
static uint64_t highest_page;
static uint64_t usable_pages;
static uint64_t free_pages;
static uint64_t bitmap_physical;
static uint64_t bitmap_page_count;
static uint64_t hhdm;
static const struct limine_memmap_response *map;
static int initialized;

static uint64_t bitmap_byte_count(uint64_t pages)
{
    return pages / 8 + (pages % 8 != 0);
}

static uint64_t align_page_up(uint64_t address)
{
    if (address > UINT64_MAX - (PAGE_SIZE - 1)) {
        return UINT64_MAX;
    }
    return (address + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
}

static int get_page_range(const struct limine_memmap_entry *entry,
                          uint64_t *first_page, uint64_t *end_page)
{
    if (entry == 0 || entry->type != MEMORY_MAP_USABLE ||
        entry->length > UINT64_MAX - entry->base) {
        return 0;
    }

    uint64_t first_address = align_page_up(entry->base);
    uint64_t end_address = entry->base + entry->length;
    *first_page = first_address >> PAGE_SHIFT;
    *end_page = end_address >> PAGE_SHIFT;
    return *first_page < *end_page;
}

static int bit_is_used(uint64_t page)
{
    return (page_bitmap[page >> 3] & (uint8_t)(1u << (page & 7))) != 0;
}

static void set_page_used(uint64_t page)
{
    page_bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}

static void set_page_free(uint64_t page)
{
    page_bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}

static int page_is_bitmap(uint64_t page)
{
    return page >= bitmap_physical / PAGE_SIZE &&
           page - bitmap_physical / PAGE_SIZE < bitmap_page_count;
}

int memory_init(const struct limine_memmap_response *memory_map,
                uint64_t hhdm_offset)
{
    initialized = 0;
    page_bitmap = 0;

    if (memory_map == 0 || memory_map->entries == 0 ||
        memory_map->entry_count == 0) {
        return 0;
    }

    uint64_t max_page = 0;
    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        struct limine_memmap_entry *entry = memory_map->entries[index];
        uint64_t first_page;
        uint64_t end_page;
        if (get_page_range(entry, &first_page, &end_page) &&
            end_page > max_page) {
            max_page = end_page;
        }
    }
    if (max_page == 0) {
        return 0;
    }
    if (max_page > (UINT64_MAX - hhdm_offset) / PAGE_SIZE) {
        return 0;
    }

    uint64_t bytes_needed = bitmap_byte_count(max_page);
    if (bytes_needed > UINT64_MAX - (PAGE_SIZE - 1)) {
        return 0;
    }
    uint64_t pages_needed = (bytes_needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed == 0 || pages_needed > UINT64_MAX / PAGE_SIZE) {
        return 0;
    }
    uint64_t bitmap_bytes = pages_needed * PAGE_SIZE;

    uint64_t chosen_bitmap_address = UINT64_MAX;
    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        struct limine_memmap_entry *entry = memory_map->entries[index];
        uint64_t first_page;
        uint64_t end_page;
        if (!get_page_range(entry, &first_page, &end_page) ||
            end_page - first_page < pages_needed) {
            continue;
        }
        uint64_t candidate = first_page * PAGE_SIZE;
        if (candidate <= UINT64_MAX - hhdm_offset &&
            hhdm_offset + candidate != 0) {
            chosen_bitmap_address = candidate;
            break;
        }
    }
    if (chosen_bitmap_address == UINT64_MAX ||
        hhdm_offset > UINT64_MAX - chosen_bitmap_address) {
        return 0;
    }

    bitmap_physical = chosen_bitmap_address;
    bitmap_page_count = pages_needed;
    highest_page = max_page;
    hhdm = hhdm_offset;
    map = memory_map;
    page_bitmap = (uint8_t *)(uintptr_t)(hhdm + bitmap_physical);
    for (uint64_t index = 0; index < bitmap_bytes; ++index) {
        page_bitmap[index] = 0xff;
    }

    usable_pages = 0;
    free_pages = 0;
    for (uint64_t index = 0; index < memory_map->entry_count; ++index) {
        struct limine_memmap_entry *entry = memory_map->entries[index];
        uint64_t first_page;
        uint64_t end_page;
        if (!get_page_range(entry, &first_page, &end_page)) {
            continue;
        }

        for (uint64_t page = first_page; page < end_page; ++page) {
            ++usable_pages;
            if (!page_is_bitmap(page)) {
                set_page_free(page);
                ++free_pages;
            }
        }
    }

    if (usable_pages < bitmap_page_count) {
        page_bitmap = 0;
        return 0;
    }
    initialized = 1;
    return 1;
}

int memory_get_stats(struct memory_stats *stats)
{
    if (stats == 0 || !initialized) {
        return 0;
    }

    stats->total_bytes = usable_pages * PAGE_SIZE;
    stats->free_bytes = free_pages * PAGE_SIZE;
    stats->used_bytes = stats->total_bytes - stats->free_bytes;
    return 1;
}

void *memory_alloc_page(void)
{
    if (!initialized) {
        return 0;
    }

    for (uint64_t page = 0; page < highest_page; ++page) {
        if (!bit_is_used(page)) {
            set_page_used(page);
            --free_pages;
            return (void *)(uintptr_t)(hhdm + page * PAGE_SIZE);
        }
    }
    return 0;
}

int memory_free_page(void *address)
{
    uintptr_t virtual_address = (uintptr_t)address;
    if (!initialized || virtual_address < hhdm) {
        return 0;
    }

    uint64_t physical_address = virtual_address - hhdm;
    if ((physical_address & (PAGE_SIZE - 1)) != 0) {
        return 0;
    }
    uint64_t page = physical_address >> PAGE_SHIFT;
    if (page >= highest_page || page_is_bitmap(page) || !bit_is_used(page)) {
        return 0;
    }

    int usable = 0;
    for (uint64_t index = 0; index < map->entry_count; ++index) {
        uint64_t first_page;
        uint64_t end_page;
        if (get_page_range(map->entries[index], &first_page, &end_page) &&
            page >= first_page && page < end_page) {
            usable = 1;
            break;
        }
    }
    if (!usable) {
        return 0;
    }

    set_page_free(page);
    ++free_pages;
    return 1;
}
