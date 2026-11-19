#include "ramfs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *kmalloc(size_t size)
{
    return malloc(size);
}

void kfree(void *pointer)
{
    free(pointer);
}

static int fail(const char *message)
{
    fprintf(stderr, "RAMFS test failed: %s\n", message);
    return 1;
}

int main(void)
{
    ramfs_init();
    if (ramfs_mkdir("docs") != RAMFS_OK ||
        ramfs_mkdir("/docs") != RAMFS_EXISTS ||
        !ramfs_set_cwd_path("/docs")) {
        return fail("mkdir/cd");
    }
    char cwd[128];
    if (ramfs_format_cwd(cwd, sizeof(cwd)) == 0 ||
        cwd[0] != '/' ||
        ramfs_write("note.txt", "hello", 5) != RAMFS_OK) {
        return fail("relative write");
    }
    struct ramfs_node *file;
    if (ramfs_resolve("note.txt", &file) != RAMFS_OK ||
        file->size != 5 ||
        file->data == 0 ||
        file->data[0] != 'h') {
        return fail("resolve");
    }
    if (ramfs_append("note.txt", "!", 1) != RAMFS_OK ||
        file->size != 6) {
        return fail("append");
    }
    if (ramfs_copy_file("note.txt", "../copy.txt") != RAMFS_OK ||
        ramfs_resolve("../copy.txt", &file) != RAMFS_OK ||
        file->size != 6) {
        return fail("copy");
    }
    if (ramfs_move("../copy.txt", "moved.txt") != RAMFS_OK ||
        ramfs_resolve("moved.txt", &file) != RAMFS_OK ||
        ramfs_resolve("../copy.txt", &file) == RAMFS_OK) {
        return fail("move");
    }
    if (ramfs_remove("moved.txt") != RAMFS_OK ||
        ramfs_remove("/") != RAMFS_ROOT) {
        return fail("remove/root");
    }
    puts("RAMFS tests passed");
    return 0;
}
