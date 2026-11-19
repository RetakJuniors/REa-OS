#include "heap.h"

#include "pmm.h"
#include "paging.h"

#include <stdint.h>

#ifndef REA_HEAP_BASE
#define REA_HEAP_BASE 0xffffc00000000000ull
#endif

#define HEAP_BASE REA_HEAP_BASE
#define HEAP_LIMIT_BYTES (32u * 1024u * 1024u)
#define HEAP_BLOCK_MAGIC 0x5245414845415042ull
#define HEAP_ALIGNMENT 16u

struct heap_block {
    size_t size;
    struct heap_block *next;
    struct heap_block *previous;
    uint64_t magic;
    int free;
} __attribute__((aligned(HEAP_ALIGNMENT)));

static struct heap_block *first_block;
static struct heap_block *last_block;
static size_t mapped_pages;
static size_t max_pages;
static int initialized;
static uint64_t allocated_bytes;

static int align_size(size_t size, size_t *aligned)
{
    if (size > SIZE_MAX - (HEAP_ALIGNMENT - 1u)) {
        return 0;
    }
    *aligned = (size + HEAP_ALIGNMENT - 1u) &
               ~(size_t)(HEAP_ALIGNMENT - 1u);
    return 1;
}

static void merge_with_next(struct heap_block *block)
{
    struct heap_block *next = block->next;
    if (next == 0 || !next->free) {
        return;
    }
    block->size += sizeof(*next) + next->size;
    block->next = next->next;
    if (block->next != 0) {
        block->next->previous = block;
    } else {
        last_block = block;
    }
}

static int grow_heap(void)
{
    if (mapped_pages >= max_pages) {
        return 0;
    }

    uintptr_t virtual_address =
        (uintptr_t)(HEAP_BASE + mapped_pages * PAGING_PAGE_SIZE);
    void *physical_alias = pmm_alloc_page();
    if (physical_alias == 0) {
        return 0;
    }

    uint64_t *words = physical_alias;
    for (unsigned int index = 0; index < PAGING_PAGE_SIZE / sizeof(*words);
         ++index) {
        words[index] = 0;
    }

    uint64_t physical_address;
    if (!paging_translate((uintptr_t)physical_alias, &physical_address) ||
        !paging_map_page(virtual_address, physical_address,
                         PAGING_FLAG_WRITABLE |
                             (paging_nx_available()
                                  ? PAGING_FLAG_NO_EXECUTE
                                  : 0))) {
        (void)pmm_free_page(physical_alias);
        return 0;
    }

    struct heap_block *block = (struct heap_block *)virtual_address;
    block->size = PAGING_PAGE_SIZE - sizeof(*block);
    block->next = 0;
    block->previous = last_block;
    block->magic = HEAP_BLOCK_MAGIC;
    block->free = 1;
    if (last_block != 0) {
        last_block->next = block;
    } else {
        first_block = block;
    }
    last_block = block;
    ++mapped_pages;

    if (block->previous != 0 && block->previous->free) {
        merge_with_next(block->previous);
    }
    return 1;
}

int heap_init(void)
{
    initialized = 0;
    first_block = 0;
    last_block = 0;
    mapped_pages = 0;
    max_pages = HEAP_LIMIT_BYTES / PAGING_PAGE_SIZE;
    struct memory_stats memory;
    if (pmm_get_memory_stats(&memory)) {
        uint64_t quarter = memory.free_bytes / 4u;
        uint64_t quarter_pages = quarter / PAGING_PAGE_SIZE;
        if (quarter_pages < max_pages) {
            max_pages = (size_t)quarter_pages;
        }
    }
    allocated_bytes = 0;
    if (!paging_is_active()) {
        return 0;
    }

    initialized = 1;
    if (!grow_heap()) {
        initialized = 0;
        return 0;
    }
    return 1;
}

void *kmalloc(size_t size)
{
    size_t allocation_size;
    if (!initialized || size == 0 || !align_size(size, &allocation_size)) {
        return 0;
    }

    for (;;) {
        for (struct heap_block *block = first_block;
             block != 0; block = block->next) {
            if (!block->free || block->size < allocation_size) {
                continue;
            }

            if (block->size - allocation_size >=
                sizeof(*block) + HEAP_ALIGNMENT) {
                struct heap_block *remainder =
                    (struct heap_block *)((uint8_t *)(block + 1) +
                                          allocation_size);
                remainder->size = block->size - allocation_size -
                                  sizeof(*block);
                remainder->next = block->next;
                remainder->previous = block;
                remainder->magic = HEAP_BLOCK_MAGIC;
                remainder->free = 1;
                if (remainder->next != 0) {
                    remainder->next->previous = remainder;
                } else {
                    last_block = remainder;
                }
                block->next = remainder;
                block->size = allocation_size;
            }
            block->free = 0;
            allocated_bytes += block->size;
            return block + 1;
        }

        if (!grow_heap()) {
            return 0;
        }
    }
}

void *kcalloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) {
        return 0;
    }
    size_t total = count * size;
    void *pointer = kmalloc(total);
    if (pointer != 0) {
        uint8_t *bytes = pointer;
        for (size_t index = 0; index < total; ++index) {
            bytes[index] = 0;
        }
    }
    return pointer;
}

void kfree(void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;
    uintptr_t heap_start = (uintptr_t)HEAP_BASE;
    uintptr_t heap_end = heap_start + mapped_pages * PAGING_PAGE_SIZE;
    if (!initialized || pointer == 0 ||
        address < heap_start + sizeof(struct heap_block) ||
        address >= heap_end || (address & (HEAP_ALIGNMENT - 1u)) != 0) {
        return;
    }

    struct heap_block *block = (struct heap_block *)pointer - 1;
    for (struct heap_block *candidate = first_block;
         candidate != 0; candidate = candidate->next) {
        if (candidate != block) {
            continue;
        }
        if (candidate->magic != HEAP_BLOCK_MAGIC || candidate->free) {
            return;
        }

        if (candidate->size <= allocated_bytes) {
            allocated_bytes -= candidate->size;
        } else {
            allocated_bytes = 0;
        }
        candidate->free = 1;
        if (candidate->previous != 0 && candidate->previous->free) {
            candidate = candidate->previous;
            merge_with_next(candidate);
        }
        merge_with_next(candidate);
        return;
    }
}

int heap_get_stats(struct heap_stats *stats)
{
    if (stats == 0) {
        return 0;
    }
    stats->mapped_bytes = initialized
                              ? mapped_pages * PAGING_PAGE_SIZE
                              : 0;
    stats->used_bytes = allocated_bytes;
    return initialized;
}
