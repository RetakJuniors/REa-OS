#include "boot.h"
#include "memory.h"
#include "pmm.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_PAGE_SIZE 4096
#define TEST_PAGE_COUNT 32

static uint8_t physical_memory[TEST_PAGE_SIZE * TEST_PAGE_COUNT]
    __attribute__((aligned(TEST_PAGE_SIZE)));

int main(void)
{
    struct limine_memmap_entry low_usable = {
        .base = 0,
        .length = 8 * TEST_PAGE_SIZE,
        .type = 0
    };
    struct limine_memmap_entry reserved = {
        .base = 8 * TEST_PAGE_SIZE,
        .length = 4 * TEST_PAGE_SIZE,
        .type = 1
    };
    struct limine_memmap_entry high_usable = {
        .base = 12 * TEST_PAGE_SIZE,
        .length = 20 * TEST_PAGE_SIZE,
        .type = 0
    };
    struct limine_memmap_entry *entries[] = {
        &low_usable, &reserved, &high_usable
    };
    struct limine_memmap_response response = {
        .revision = 0,
        .entry_count = 3,
        .entries = entries
    };
    uint64_t hhdm_offset = (uintptr_t)physical_memory;

    if (!memory_init(&response, hhdm_offset)) {
        fputs("memory_init failed\n", stderr);
        return 1;
    }

    struct memory_stats stats;
    if (!memory_get_stats(&stats) ||
        stats.total_bytes != 28 * TEST_PAGE_SIZE ||
        stats.used_bytes != TEST_PAGE_SIZE ||
        stats.free_bytes != 27 * TEST_PAGE_SIZE) {
        fputs("initial memory stats are incorrect\n", stderr);
        return 1;
    }

    void *page = memory_alloc_page();
    if (page != physical_memory + TEST_PAGE_SIZE ||
        !memory_get_stats(&stats) ||
        stats.used_bytes != 2 * TEST_PAGE_SIZE ||
        stats.free_bytes != 26 * TEST_PAGE_SIZE) {
        fputs("page allocation failed\n", stderr);
        return 1;
    }

    for (unsigned int index = 0; index < 6; ++index) {
        if (memory_alloc_page() == 0) {
            fputs("page allocation stopped early\n", stderr);
            return 1;
        }
    }
    if (memory_alloc_page() != physical_memory + 12 * TEST_PAGE_SIZE) {
        fputs("allocator returned a page from a reserved region\n", stderr);
        return 1;
    }

    if (memory_free_page(physical_memory + 8 * TEST_PAGE_SIZE) ||
        memory_free_page(physical_memory + TEST_PAGE_SIZE + 1u)) {
        fputs("allocator accepted a reserved or unaligned page\n", stderr);
        return 1;
    }

    if (!memory_free_page(page) || memory_free_page(page) ||
        !memory_get_stats(&stats) ||
        stats.used_bytes != 8 * TEST_PAGE_SIZE ||
        stats.free_bytes != 20 * TEST_PAGE_SIZE) {
        fputs("page release failed\n", stderr);
        return 1;
    }
    struct pmm_stats pmm;
    if (!pmm_get_stats(&pmm) ||
        pmm.total_pages != 28u ||
        pmm.used_pages != 8u ||
        pmm.free_pages != 20u ||
        pmm_alloc_page() == 0) {
        fputs("PMM API or page statistics failed\n", stderr);
        return 1;
    }

    puts("Memory and PMM tests passed");
    return 0;
}
