#include "paging.h"
#include "pmm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PAGE_COUNT 16u
#define PAGE_ADDRESS_MASK 0x000ffffffffff000ull

static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint8_t page_pool[TEST_PAGE_COUNT][4096]
    __attribute__((aligned(4096)));
static uint64_t root_snapshot[512];
static uint8_t page_pool_snapshot[TEST_PAGE_COUNT][4096];
static uint8_t page_freed[TEST_PAGE_COUNT];
static unsigned int allocated_pages;
static unsigned int freed_pages;
static unsigned int allocation_limit = TEST_PAGE_COUNT;

void *pmm_alloc_page(void)
{
    if (allocated_pages >= allocation_limit) {
        return 0;
    }
    return page_pool[allocated_pages++];
}

int pmm_free_page(void *page)
{
    for (unsigned int index = 0; index < allocated_pages; ++index) {
        if (page == page_pool[index] && !page_freed[index]) {
            page_freed[index] = 1;
            ++freed_pages;
            return 1;
        }
    }
    return 0;
}

int pmm_get_memory_stats(struct memory_stats *stats)
{
    (void)stats;
    return 0;
}

int pmm_get_stats(struct pmm_stats *stats)
{
    (void)stats;
    return 0;
}

void serial_write(const char *text)
{
    (void)text;
}

static int fail(const char *message)
{
    fprintf(stderr, "VMM test failed: %s\n", message);
    return 1;
}

static unsigned int active_page_allocations(void)
{
    return allocated_pages - freed_pages;
}

static void snapshot_page_tables(void)
{
    memcpy(root_snapshot, root_table, sizeof(root_table));
    memcpy(page_pool_snapshot, page_pool, sizeof(page_pool));
}

static int page_tables_match_snapshot(void)
{
    return memcmp(root_snapshot, root_table, sizeof(root_table)) == 0 &&
           memcmp(page_pool_snapshot, page_pool, sizeof(page_pool)) == 0;
}

static int mapping_path(uintptr_t virtual_address,
                       volatile uint64_t *entries[5])
{
    volatile uint64_t *table = root_table;
    entries[0] = 0;
    for (unsigned int level = 4; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        entries[4u - level + 1u] = &table[index];
        uint64_t entry = table[index];
        if ((entry & PAGING_FLAG_PRESENT) == 0) {
            return 0;
        }
        table = (volatile uint64_t *)(uintptr_t)(entry & PAGE_ADDRESS_MASK);
    }
    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    entries[4] = &table[index];
    return 1;
}

static int is_supervisor_mapping(uintptr_t virtual_address)
{
    volatile uint64_t *entries[5];
    if (!mapping_path(virtual_address, entries)) {
        return 0;
    }
    for (unsigned int index = 1; index < 5; ++index) {
        if ((*entries[index] & PAGING_FLAG_USER) != 0) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    paging_test_initialize((uintptr_t)root_table, 0, 4, 1);
    const uintptr_t base = 0x0000004000000000ull;
    const uintptr_t sibling = base + 0x200000u;
    const uintptr_t rejected_user = base + 0x400000u;
    const uintptr_t oom_address = 0x0000010000000000ull;
    const uint64_t physical_address = 0x12345000ull;
    uint64_t translated = 0;

    if (!vmm_map_page(base, physical_address,
                      PAGING_FLAG_WRITABLE | PAGING_FLAG_NO_EXECUTE)) {
        return fail("map supervisor page");
    }
    if (!vmm_translate(base + 17u, &translated) ||
        translated != physical_address + 17u ||
        !is_supervisor_mapping(base)) {
        return fail("translate or verify supervisor mapping");
    }

    if (!vmm_map_page(sibling, physical_address + PMM_PAGE_SIZE,
                      PAGING_FLAG_WRITABLE)) {
        return fail("map sibling supervisor page");
    }
    if (!is_supervisor_mapping(sibling)) {
        return fail("sibling mapping must remain supervisor-only");
    }

    volatile uint64_t *base_path[5];
    volatile uint64_t *sibling_path[5];
    if (!mapping_path(base, base_path) ||
        !mapping_path(sibling, sibling_path)) {
        return fail("capture existing page-table paths");
    }
    uint64_t base_path_snapshot[5];
    uint64_t sibling_path_snapshot[5];
    for (unsigned int index = 1; index < 5; ++index) {
        base_path_snapshot[index] = *base_path[index];
        sibling_path_snapshot[index] = *sibling_path[index];
    }
    unsigned int allocations_before_user = allocated_pages;
    unsigned int active_before_user = active_page_allocations();
    snapshot_page_tables();

    if (vmm_map_page(rejected_user, physical_address + 2u * PMM_PAGE_SIZE,
                     PAGING_FLAG_USER | PAGING_FLAG_WRITABLE) ||
        paging_map_page(rejected_user,
                        physical_address + 2u * PMM_PAGE_SIZE,
                        PAGING_FLAG_USER)) {
        return fail("global kernel mapping API must reject USER");
    }
    if (!page_tables_match_snapshot() ||
        allocated_pages != allocations_before_user ||
        active_page_allocations() != active_before_user) {
        return fail("rejected USER mapping changed tables or allocated pages");
    }
    for (unsigned int index = 1; index < 5; ++index) {
        if (*base_path[index] != base_path_snapshot[index] ||
            *sibling_path[index] != sibling_path_snapshot[index]) {
            return fail("USER rejection changed an existing path entry");
        }
    }
    if (vmm_translate(rejected_user, &translated) ||
        !is_supervisor_mapping(base) ||
        !is_supervisor_mapping(sibling)) {
        return fail("USER rejection changed mapping visibility");
    }

    uint64_t supervisor_root_entry = *base_path[1];
    *base_path[1] |= PAGING_FLAG_USER;
    snapshot_page_tables();
    if (vmm_unmap_page(base) || !page_tables_match_snapshot()) {
        return fail("unmap must reject a USER-marked ancestor unchanged");
    }
    *base_path[1] = supervisor_root_entry;

    uint64_t supervisor_leaf_entry = *base_path[4];
    *base_path[4] |= PAGING_FLAG_USER;
    snapshot_page_tables();
    if (paging_unmap_page(base) || !page_tables_match_snapshot()) {
        return fail("unmap must reject a USER-marked leaf unchanged");
    }
    *base_path[4] = supervisor_leaf_entry;

    if (vmm_map_page(base, physical_address, 0) ||
        vmm_map_page(base + 1u, physical_address, 0) ||
        vmm_map_page(base, physical_address + 1u, 0) ||
        vmm_map_page((uintptr_t)0x0000800000000000ull,
                     physical_address, 0) ||
        vmm_map_page(base, physical_address, 1ull << 9)) {
        return fail("reject duplicate, unaligned, non-canonical or bad flags");
    }
    if (!vmm_translate(base, &translated) ||
        translated != physical_address ||
        active_page_allocations() != active_before_user) {
        return fail("invalid requests changed existing state");
    }

    uint64_t original_root_entry = *base_path[1];
    *base_path[1] &= ~PAGING_FLAG_WRITABLE;
    unsigned int active_before_read_only = active_page_allocations();
    snapshot_page_tables();
    if (vmm_map_page(base + 0x400000u, physical_address,
                     PAGING_FLAG_WRITABLE) ||
        !page_tables_match_snapshot() ||
        active_page_allocations() != active_before_read_only) {
        return fail("writable map must not loosen a read-only ancestor");
    }
    *base_path[1] = original_root_entry;

    unsigned int active_before_oom = active_page_allocations();
    unsigned int frees_before_oom = freed_pages;
    allocation_limit = allocated_pages + 1u;
    if (vmm_map_page(oom_address, physical_address,
                     PAGING_FLAG_WRITABLE)) {
        return fail("mid-path page-table allocation failure must reject map");
    }
    allocation_limit = TEST_PAGE_COUNT;
    if (active_page_allocations() != active_before_oom ||
        freed_pages != frees_before_oom + 1u ||
        root_table[(oom_address >> 39) & 0x1ffu] != 0) {
        return fail("failed map leaked or left an intermediate table");
    }

    if (!vmm_unmap_page(base) || vmm_translate(base, &translated) ||
        !vmm_translate(sibling, &translated) ||
        translated != physical_address + PMM_PAGE_SIZE ||
        !is_supervisor_mapping(sibling) || vmm_unmap_page(base)) {
        return fail("unmap changed sibling or failed supervisor behavior");
    }
    if (!vmm_unmap_page(sibling) ||
        vmm_translate(sibling, &translated) ||
        vmm_unmap_page(sibling)) {
        return fail("unmap and reject missing mapping");
    }

    paging_test_initialize((uintptr_t)root_table, 0, 4, 0);
    if (vmm_map_page(base, physical_address, PAGING_FLAG_NO_EXECUTE)) {
        return fail("reject NX when NX is unavailable");
    }

    puts("VMM tests passed");
    return 0;
}
