#ifndef REA_PAGING_H
#define REA_PAGING_H

#include <stdint.h>

#define PAGING_PAGE_SIZE 4096u
#define PAGING_FLAG_WRITABLE (1ull << 1)
#define PAGING_FLAG_USER (1ull << 2)
#define PAGING_FLAG_WRITE_THROUGH (1ull << 3)
#define PAGING_FLAG_CACHE_DISABLE (1ull << 4)
#define PAGING_FLAG_GLOBAL (1ull << 8)
#define PAGING_FLAG_NO_EXECUTE (1ull << 63)

int paging_init(uint64_t hhdm_offset);
int paging_is_active(void);
int paging_nx_available(void);
int paging_map_page(uintptr_t virtual_address, uint64_t physical_address,
                    uint64_t flags);
int paging_unmap_page(uintptr_t virtual_address);
int paging_translate(uintptr_t virtual_address, uint64_t *physical_address);

#endif
