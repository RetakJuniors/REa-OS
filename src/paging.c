#include "paging.h"

#include "pmm.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT (1ull << 0)
#define PAGE_LARGE (1ull << 7)
#define PAGE_ADDRESS_MASK 0x000ffffffffff000ull
#define CR0_PAGING (1ull << 31)
#define CR4_LA57 (1ull << 12)
#define EFER_MSR 0xc0000080u
#define EFER_NXE (1ull << 11)

static uint64_t hhdm;
static uint64_t root_table_physical;
static unsigned int page_levels;
static int nx_enabled;
static int initialized;

static void invalidate_page(uintptr_t virtual_address)
{
#ifdef REA_PAGING_TEST
    (void)virtual_address;
#else
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
#endif
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static int is_canonical(uintptr_t address)
{
    unsigned int bits = page_levels == 5 ? 57u : 48u;
    uint64_t upper_mask = ~((1ull << bits) - 1);
    uint64_t value = (uint64_t)address;

    if ((value & (1ull << (bits - 1))) == 0) {
        return (value & upper_mask) == 0;
    }
    return (value & upper_mask) == upper_mask;
}

static volatile uint64_t *table_from_physical(uint64_t physical_address)
{
    if (physical_address > UINT64_MAX - hhdm) {
        return 0;
    }
    return (volatile uint64_t *)(uintptr_t)(hhdm + physical_address);
}

static void clear_page(void *page)
{
    uint64_t *words = page;
    for (unsigned int index = 0; index < PAGING_PAGE_SIZE / sizeof(*words);
         ++index) {
        words[index] = 0;
    }
}

static volatile uint64_t *get_child_table(volatile uint64_t *parent,
                                          uint16_t index, int create,
                                          uint64_t flags)
{
    uint64_t entry = parent[index];
    if ((entry & PAGE_PRESENT) != 0) {
        if ((entry & PAGE_LARGE) != 0 ||
            ((flags & PAGING_FLAG_WRITABLE) != 0 &&
             (entry & PAGING_FLAG_WRITABLE) == 0)) {
            return 0;
        }
        return table_from_physical(entry & PAGE_ADDRESS_MASK);
    }
    if (!create) {
        return 0;
    }

    void *new_table = pmm_alloc_page();
    if (new_table == 0) {
        return 0;
    }
    clear_page(new_table);

    uintptr_t virtual_address = (uintptr_t)new_table;
    if (virtual_address < hhdm) {
        (void)pmm_free_page(new_table);
        return 0;
    }
    uint64_t physical_address = (uint64_t)(virtual_address - hhdm);
    if ((physical_address & (PAGING_PAGE_SIZE - 1)) != 0 ||
        (physical_address & ~PAGE_ADDRESS_MASK) != 0) {
        (void)pmm_free_page(new_table);
        return 0;
    }

    uint64_t entry_flags = PAGE_PRESENT | PAGING_FLAG_WRITABLE;
    parent[index] = physical_address | entry_flags;
    return (volatile uint64_t *)new_table;
}

int paging_init(uint64_t hhdm_offset)
{
    initialized = 0;
    hhdm = hhdm_offset;

    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    if ((cr0 & CR0_PAGING) == 0) {
        serial_write("REA OS: paging is not enabled\r\n");
        return 0;
    }

    page_levels = (cr4 & CR4_LA57) != 0 ? 5u : 4u;
    root_table_physical = cr3 & PAGE_ADDRESS_MASK;
    if (table_from_physical(root_table_physical) == 0) {
        serial_write("REA OS: current page table is not HHDM-accessible\r\n");
        return 0;
    }

    nx_enabled = (read_msr(EFER_MSR) & EFER_NXE) != 0;
    initialized = 1;
    serial_write(page_levels == 5
                     ? "REA OS: active 5-level paging found\r\n"
                     : "REA OS: active 4-level paging found\r\n");
    return 1;
}

int paging_is_active(void)
{
    return initialized;
}

int paging_nx_available(void)
{
    return initialized && nx_enabled;
}

int paging_map_page(uintptr_t virtual_address, uint64_t physical_address,
                    uint64_t flags)
{
    const uint64_t known_flags = PAGING_FLAG_PRESENT |
                                 PAGING_FLAG_WRITABLE |
                                 PAGING_FLAG_USER |
                                 PAGING_FLAG_WRITE_THROUGH |
                                 PAGING_FLAG_CACHE_DISABLE |
                                 PAGING_FLAG_GLOBAL |
                                 PAGING_FLAG_NO_EXECUTE;
    if ((flags & PAGING_FLAG_USER) != 0 || (flags & ~known_flags) != 0 ||
        !initialized || !is_canonical(virtual_address) ||
        (virtual_address & (PAGING_PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGING_PAGE_SIZE - 1)) != 0 ||
        (physical_address & ~PAGE_ADDRESS_MASK) != 0 ||
        ((flags & PAGING_FLAG_NO_EXECUTE) != 0 && !nx_enabled)) {
        return 0;
    }

    volatile uint64_t *root = table_from_physical(root_table_physical);
    if (root == 0) {
        return 0;
    }

    uint16_t indices[5];
    unsigned int index_count = 0;
    volatile uint64_t *table = root;
    int found_missing_entry = 0;

    /*
     * Inspect every existing ancestor before creating anything. Mapping a
     * writable leaf through a read-only ancestor cannot be done without
     * changing permissions for its siblings, so reject it instead.
     */
    for (unsigned int level = page_levels; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        indices[index_count++] = index;
        if (found_missing_entry) {
            continue;
        }
        uint64_t entry = table[index];
        if ((entry & PAGE_PRESENT) == 0) {
            found_missing_entry = 1;
            continue;
        }
        if ((entry & PAGE_LARGE) != 0 ||
            ((flags & PAGING_FLAG_WRITABLE) != 0 &&
             (entry & PAGING_FLAG_WRITABLE) == 0)) {
            return 0;
        }
        table = table_from_physical(entry & PAGE_ADDRESS_MASK);
        if (table == 0) return 0;
    }

    if (!found_missing_entry) {
        uint16_t leaf_index =
            (uint16_t)((virtual_address >> 12) & 0x1ffu);
        if ((table[leaf_index] & PAGE_PRESENT) != 0) {
            return 0;
        }
    }

    struct {
        volatile uint64_t *parent;
        uint16_t index;
        void *page;
    } allocated_tables[5];
    unsigned int allocated_count = 0;
    table = root;
    for (unsigned int path_index = 0; path_index < index_count;
         ++path_index) {
        uint16_t index = indices[path_index];
        int was_missing = (table[index] & PAGE_PRESENT) == 0;
        volatile uint64_t *child =
            get_child_table(table, index, 1, flags);
        if (child == 0) {
            while (allocated_count != 0) {
                --allocated_count;
                allocated_tables[allocated_count].parent[
                    allocated_tables[allocated_count].index] = 0;
                (void)pmm_free_page(
                    allocated_tables[allocated_count].page);
            }
            invalidate_page(virtual_address);
            return 0;
        }
        if (was_missing) {
            allocated_tables[allocated_count].parent = table;
            allocated_tables[allocated_count].index = index;
            allocated_tables[allocated_count].page = (void *)child;
            ++allocated_count;
        }
        table = child;
    }

    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if ((table[index] & PAGE_PRESENT) != 0) {
        while (allocated_count != 0) {
            --allocated_count;
            allocated_tables[allocated_count].parent[
                allocated_tables[allocated_count].index] = 0;
            (void)pmm_free_page(allocated_tables[allocated_count].page);
        }
        invalidate_page(virtual_address);
        return 0;
    }

    uint64_t allowed_flags = PAGING_FLAG_WRITABLE |
                             PAGING_FLAG_WRITE_THROUGH |
                             PAGING_FLAG_CACHE_DISABLE |
                             PAGING_FLAG_GLOBAL | PAGING_FLAG_NO_EXECUTE;
    table[index] = physical_address | PAGE_PRESENT | (flags & allowed_flags);
    invalidate_page(virtual_address);
    return 1;
}

int paging_unmap_page(uintptr_t virtual_address)
{
    if (!initialized || !is_canonical(virtual_address) ||
        (virtual_address & (PAGING_PAGE_SIZE - 1)) != 0) {
        return 0;
    }

    volatile uint64_t *table = table_from_physical(root_table_physical);
    if (table == 0) {
        return 0;
    }
    for (unsigned int level = page_levels; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        uint64_t entry = table[index];
        if ((entry & PAGE_PRESENT) == 0 ||
            (entry & PAGING_FLAG_USER) != 0 ||
            ((level == 3 || level == 2) && (entry & PAGE_LARGE) != 0)) {
            return 0;
        }
        table = table_from_physical(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return 0;
        }
    }

    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if ((table[index] & PAGE_PRESENT) == 0 ||
        (table[index] & PAGING_FLAG_USER) != 0) {
        return 0;
    }
    table[index] = 0;
    invalidate_page(virtual_address);
    return 1;
}

int paging_translate(uintptr_t virtual_address, uint64_t *physical_address)
{
    if (!initialized || physical_address == 0 ||
        !is_canonical(virtual_address)) {
        return 0;
    }

    volatile uint64_t *table = table_from_physical(root_table_physical);
    if (table == 0) {
        return 0;
    }
    for (unsigned int level = page_levels; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        uint64_t entry = table[index];
        if ((entry & PAGE_PRESENT) == 0) {
            return 0;
        }

        if ((level == 3 || level == 2) && (entry & PAGE_LARGE) != 0) {
            unsigned int page_shift = level == 3 ? 30u : 21u;
            uint64_t page_mask = (1ull << page_shift) - 1;
            *physical_address = (entry & PAGE_ADDRESS_MASK & ~page_mask) |
                                ((uint64_t)virtual_address & page_mask);
            return 1;
        }

        table = table_from_physical(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return 0;
        }
    }

    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    uint64_t entry = table[index];
    if ((entry & PAGE_PRESENT) == 0) {
        return 0;
    }
    *physical_address = (entry & PAGE_ADDRESS_MASK) |
                        ((uint64_t)virtual_address &
                         (PAGING_PAGE_SIZE - 1));
    return 1;
}

int vmm_map_page(uintptr_t virtual_address, uint64_t physical_address,
                 uint64_t flags)
{
    return paging_map_page(virtual_address, physical_address, flags);
}

int vmm_unmap_page(uintptr_t virtual_address)
{
    return paging_unmap_page(virtual_address);
}

int vmm_translate(uintptr_t virtual_address, uint64_t *physical_address)
{
    return paging_translate(virtual_address, physical_address);
}

#ifdef REA_PAGING_TEST
void paging_test_initialize(uint64_t root_physical, uint64_t hhdm_offset,
                            unsigned int levels, int nx_enabled_value)
{
    hhdm = hhdm_offset;
    root_table_physical = root_physical;
    page_levels = levels;
    nx_enabled = nx_enabled_value;
    initialized = levels == 4u || levels == 5u;
}
#endif
