#include "paging.h"

#include "memory.h"
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
        if ((entry & PAGE_LARGE) != 0) {
            return 0;
        }
        if ((flags & PAGING_FLAG_WRITABLE) != 0) {
            entry |= PAGING_FLAG_WRITABLE;
        }
        if ((flags & PAGING_FLAG_USER) != 0) {
            entry |= PAGING_FLAG_USER;
        }
        parent[index] = entry;
        return table_from_physical(entry & PAGE_ADDRESS_MASK);
    }
    if (!create) {
        return 0;
    }

    void *new_table = memory_alloc_page();
    if (new_table == 0) {
        return 0;
    }
    clear_page(new_table);

    uint64_t physical_address;
    if (!paging_translate((uintptr_t)new_table, &physical_address) ||
        (physical_address & (PAGING_PAGE_SIZE - 1)) != 0) {
        (void)memory_free_page(new_table);
        return 0;
    }

    uint64_t entry_flags = PAGE_PRESENT | PAGING_FLAG_WRITABLE;
    if ((flags & PAGING_FLAG_USER) != 0) {
        entry_flags |= PAGING_FLAG_USER;
    }
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
    if (!initialized || !is_canonical(virtual_address) ||
        (virtual_address & (PAGING_PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGING_PAGE_SIZE - 1)) != 0 ||
        (physical_address & ~PAGE_ADDRESS_MASK) != 0 ||
        ((flags & PAGING_FLAG_NO_EXECUTE) != 0 && !nx_enabled)) {
        return 0;
    }

    volatile uint64_t *table = table_from_physical(root_table_physical);
    if (table == 0) {
        return 0;
    }

    for (unsigned int level = page_levels; level > 1; --level) {
        unsigned int shift = 12u + 9u * (level - 1u);
        uint16_t index = (uint16_t)((virtual_address >> shift) & 0x1ffu);
        table = get_child_table(table, index, 1, flags);
        if (table == 0) {
            return 0;
        }
    }

    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if ((table[index] & PAGE_PRESENT) != 0) {
        return 0;
    }

    uint64_t allowed_flags = PAGING_FLAG_WRITABLE | PAGING_FLAG_USER |
                             PAGING_FLAG_WRITE_THROUGH |
                             PAGING_FLAG_CACHE_DISABLE |
                             PAGING_FLAG_GLOBAL | PAGING_FLAG_NO_EXECUTE;
    table[index] = physical_address | PAGE_PRESENT | (flags & allowed_flags);
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
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
            ((level == 3 || level == 2) && (entry & PAGE_LARGE) != 0)) {
            return 0;
        }
        table = table_from_physical(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return 0;
        }
    }

    uint16_t index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if ((table[index] & PAGE_PRESENT) == 0) {
        return 0;
    }
    table[index] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
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
