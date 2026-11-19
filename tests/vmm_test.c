#include "paging.h"
#include "pmm.h"

#include <stdint.h>
#include <stdio.h>

static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint8_t page_pool[8][4096] __attribute__((aligned(4096)));
static unsigned int allocated_pages;

void *pmm_alloc_page(void)
{
    if (allocated_pages >= 8) {
        return 0;
    }
    return page_pool[allocated_pages++];
}

int pmm_free_page(void *page)
{
    (void)page;
    return 1;
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

static uint64_t *leaf_entry(uintptr_t virtual_address)
{
    uint64_t *table = root_table;
    for (unsigned int level = 4; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        table = (uint64_t *)(uintptr_t)(table[index] & 0x000ffffffffff000ull);
    }
    return &table[(virtual_address >> 12) & 0x1ffu];
}

int main(void)
{
    paging_test_initialize((uintptr_t)root_table, 0, 4, 1);
    const uintptr_t virtual_address = 0x0000004000000000ull;
    const uint64_t physical_address = 0x12345000ull;
    uint64_t translated = 0;

    if (!vmm_map_page(virtual_address, physical_address,
                      PAGING_FLAG_WRITABLE | PAGING_FLAG_NO_EXECUTE)) {
        return fail("map 4 KiB page");
    }
    if (!vmm_translate(virtual_address + 17u, &translated) ||
        translated != physical_address + 17u) {
        return fail("translate mapped address");
    }
    uint64_t leaf = *leaf_entry(virtual_address);
    if ((leaf & (PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE |
                 PAGING_FLAG_NO_EXECUTE)) !=
            (PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE |
             PAGING_FLAG_NO_EXECUTE) ||
        (leaf & PAGING_FLAG_USER) != 0) {
        return fail("supervisor read/write/NX permissions");
    }
    const uintptr_t user_address = virtual_address + 0x200000u;
    if (!vmm_map_page(user_address, physical_address, PAGING_FLAG_USER)) {
        return fail("map user page");
    }
    leaf = *leaf_entry(user_address);
    if ((leaf & (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) !=
        (PAGING_FLAG_PRESENT | PAGING_FLAG_USER)) {
        return fail("user page permission");
    }
    if (vmm_map_page(virtual_address, physical_address, 0)) {
        return fail("reject duplicate map");
    }
    if (vmm_map_page(virtual_address + 1u, physical_address, 0)) {
        return fail("reject unaligned virtual address");
    }
    if (vmm_map_page((uintptr_t)0x0000800000000000ull,
                     physical_address, 0)) {
        return fail("reject non-canonical address");
    }
    if (!vmm_unmap_page(virtual_address) ||
        vmm_translate(virtual_address, &translated) ||
        vmm_unmap_page(virtual_address)) {
        return fail("unmap and reject missing mapping");
    }
    if (!vmm_unmap_page(user_address)) {
        return fail("unmap user page");
    }

    puts("VMM tests passed");
    return 0;
}
