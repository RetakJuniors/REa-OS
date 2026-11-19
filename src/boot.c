#include "boot.h"
#include "serial.h"

#include <stdint.h>

#define LIMINE_BASE_REVISION(N) \
    { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }
#define LIMINE_COMMON_MAGIC \
    0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer **framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_framebuffer_response *response;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_hhdm_response *response;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_memmap_response *response;
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = {
    0xf6b8f4b39de7d1ae,
    0xfab91a6940fcb9cf,
    0x785c6ed015d3e316,
    0x181e920a7852b9d9
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = {
        LIMINE_COMMON_MAGIC,
        0x9d5827dcd881dd75,
        0xa3148604f6fab11b
    },
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = {
        LIMINE_COMMON_MAGIC,
        0x48dcf1cb8ad2b852,
        0x63984e959a98244b
    },
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = {
        LIMINE_COMMON_MAGIC,
        0x67cf3d9d378a806f,
        0xe304acdfc50c3c62
    },
    .revision = 0,
    .response = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = {
    0xadc0e0531bb10d03,
    0x9572709f31764c62
};

int boot_get_framebuffer(struct limine_framebuffer **framebuffer)
{
    if (limine_base_revision[2] != 0) {
        serial_write("REA OS: Limine base revision unsupported\r\n");
        return 0;
    }

    serial_write("REA OS: Limine base revision supported\r\n");
    struct limine_framebuffer_response *response = framebuffer_request.response;

    if (response == 0) {
        serial_write("REA OS: framebuffer response NULL\r\n");
        return 0;
    }
    if (response->framebuffer_count == 0) {
        serial_write("REA OS: framebuffer_count is 0\r\n");
        return 0;
    }
    if (response->framebuffers == 0) {
        serial_write("REA OS: framebuffer array NULL\r\n");
        return 0;
    }
    if (response->framebuffers[0] == 0) {
        serial_write("REA OS: first framebuffer NULL\r\n");
        return 0;
    }

    *framebuffer = response->framebuffers[0];
    return 1;
}

int boot_get_memory_map(const struct limine_memmap_response **memory_map,
                        uint64_t *hhdm_offset)
{
    struct limine_memmap_response *response = memory_map_request.response;
    struct limine_hhdm_response *hhdm = hhdm_request.response;

    if (response == 0) {
        serial_write("REA OS: memory map response NULL\r\n");
        return 0;
    }
    if (response->entries == 0 || response->entry_count == 0) {
        serial_write("REA OS: memory map entries unavailable\r\n");
        return 0;
    }
    if (hhdm == 0) {
        serial_write("REA OS: HHDM response NULL\r\n");
        return 0;
    }

    *memory_map = response;
    *hhdm_offset = hhdm->offset;
    return 1;
}
