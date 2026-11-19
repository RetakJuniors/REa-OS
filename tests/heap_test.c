#include <stdint.h>
#include <stdio.h>

#define TEST_PAGE_SIZE 4096u

static uint8_t heap_region[8 * TEST_PAGE_SIZE]
    __attribute__((aligned(TEST_PAGE_SIZE)));
static uint8_t physical_frames[8][TEST_PAGE_SIZE]
    __attribute__((aligned(TEST_PAGE_SIZE)));
static unsigned int allocated_frames;
static unsigned int mock_mapped_pages;

#define REA_HEAP_BASE ((uintptr_t)heap_region)
#include "../src/heap.c"

int paging_is_active(void)
{
    return 1;
}

int paging_nx_available(void)
{
    return 0;
}

int paging_translate(uintptr_t virtual_address, uint64_t *physical_address)
{
    for (unsigned int index = 0; index < allocated_frames; ++index) {
        if (virtual_address == (uintptr_t)physical_frames[index]) {
            *physical_address = 0x100000u + index * TEST_PAGE_SIZE;
            return 1;
        }
    }
    return 0;
}

int paging_map_page(uintptr_t virtual_address, uint64_t physical_address,
                    uint64_t flags)
{
    uintptr_t expected = (uintptr_t)heap_region +
                         mock_mapped_pages * TEST_PAGE_SIZE;
    if (virtual_address != expected ||
        (physical_address & (TEST_PAGE_SIZE - 1)) != 0 ||
        (flags & PAGING_FLAG_WRITABLE) == 0) {
        return 0;
    }
    ++mock_mapped_pages;
    return 1;
}

void *memory_alloc_page(void)
{
    if (allocated_frames == 8) {
        return 0;
    }
    return physical_frames[allocated_frames++];
}

int memory_free_page(void *page)
{
    (void)page;
    return 1;
}

static int fail(const char *message)
{
    fprintf(stderr, "Heap test failed: %s\n", message);
    return 1;
}

int main(void)
{
    if (!heap_init()) {
        return fail("initialization");
    }
    if (kmalloc(0) != 0) {
        return fail("zero-size allocation");
    }

    void *first = kmalloc(128);
    void *second = kmalloc(128);
    if (first == 0 || second == 0 || first == second ||
        ((uintptr_t)first & 15u) != 0 ||
        ((uintptr_t)second & 15u) != 0) {
        return fail("allocation and alignment");
    }

    kfree(first);
    void *reused = kmalloc(64);
    if (reused != first) {
        return fail("first-fit reuse");
    }
    kfree(reused);
    kfree(second);

    void *a = kmalloc(1000);
    void *b = kmalloc(1000);
    void *c = kmalloc(1000);
    if (a == 0 || b == 0 || c == 0) {
        return fail("multiple allocations");
    }
    kfree(a);
    kfree(b);
    if (kmalloc(1800) != a) {
        return fail("adjacent free blocks coalesce");
    }

    void *large = kmalloc(6000);
    if (large == 0 || mock_mapped_pages < 3) {
        return fail("heap grows by mapping more pages");
    }
    kfree(large);
    kfree(large);
    puts("Heap tests passed");
    return 0;
}
