#ifndef REA_RAMFS_H
#define REA_RAMFS_H

#include <stddef.h>
#include <stdint.h>

#define RAMFS_NAME_MAX 31u
#define RAMFS_MAX_DEPTH 16u
#define RAMFS_MAX_FILE_SIZE (1024u * 1024u)
#define RAMFS_MAX_TOTAL_SIZE (8u * 1024u * 1024u)

struct ramfs_node {
    char name[RAMFS_NAME_MAX + 1u];
    uint8_t directory;
    uint32_t size;
    char *data;
    struct ramfs_node *parent;
    struct ramfs_node *child;
    struct ramfs_node *next;
};

enum ramfs_result {
    RAMFS_OK = 0,
    RAMFS_INVALID_NAME,
    RAMFS_NOT_FOUND,
    RAMFS_NOT_DIRECTORY,
    RAMFS_EXISTS,
    RAMFS_LIMIT,
    RAMFS_NOT_EMPTY,
    RAMFS_ROOT,
    RAMFS_INVALID_PATH,
    RAMFS_NO_MEMORY
};

void ramfs_init(void);
struct ramfs_node *ramfs_root(void);
struct ramfs_node *ramfs_cwd(void);
int ramfs_chdir(struct ramfs_node *directory);
int ramfs_set_cwd_path(const char *path);
enum ramfs_result ramfs_resolve(const char *path,
                                struct ramfs_node **node);
enum ramfs_result ramfs_resolve_parent(const char *path,
                                       struct ramfs_node **parent,
                                       char leaf[RAMFS_NAME_MAX + 1u]);
enum ramfs_result ramfs_mkdir(const char *path);
enum ramfs_result ramfs_remove(const char *path);
enum ramfs_result ramfs_write(const char *path, const char *data,
                              size_t size);
enum ramfs_result ramfs_append(const char *path, const char *data,
                               size_t size);
enum ramfs_result ramfs_copy_file(const char *source, const char *target);
enum ramfs_result ramfs_move(const char *source, const char *target);
size_t ramfs_format_cwd(char *buffer, size_t capacity);
size_t ramfs_format_path(const struct ramfs_node *node,
                         char *buffer, size_t capacity);
uint32_t ramfs_total_size(void);

#endif
