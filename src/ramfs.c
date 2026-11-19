#include "ramfs.h"

#include "heap.h"
#include "util.h"

#include <stddef.h>
#include <stdint.h>

static struct ramfs_node ramfs_root_node;
static struct ramfs_node *cwd;
static uint32_t ramfs_total;

static int is_name_char(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '.' || character == '_' || character == '-';
}

static int valid_name(const char *name)
{
    size_t length = 0;
    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    while (name[length] != '\0') {
        if (!is_name_char(name[length]) || length >= RAMFS_NAME_MAX) {
            return 0;
        }
        ++length;
    }
    return length != 0;
}

static int copy_name(char destination[RAMFS_NAME_MAX + 1u],
                     const char *source)
{
    size_t length = 0;
    if (!valid_name(source)) {
        return 0;
    }
    while (source[length] != '\0') {
        destination[length] = source[length];
        ++length;
    }
    destination[length] = '\0';
    return 1;
}

static struct ramfs_node *find_child(struct ramfs_node *directory,
                                     const char *name)
{
    if (directory == 0 || !directory->directory) {
        return 0;
    }
    for (struct ramfs_node *node = directory->child;
         node != 0; node = node->next) {
        if (node->name[0] != '\0' &&
            str_compare(node->name, name) == 0) {
            return node;
        }
    }
    return 0;
}

static uint32_t node_depth(struct ramfs_node *node)
{
    uint32_t depth = 0;
    while (node != 0 && node->parent != 0) {
        ++depth;
        node = node->parent;
    }
    return depth;
}

static int resolve_component(struct ramfs_node **directory,
                             const char *component)
{
    if (component[0] == '\0' ||
        (component[0] == '.' && component[1] == '\0')) {
        return 1;
    }
    if (component[0] == '.' && component[1] == '.' &&
        component[2] == '\0') {
        if ((*directory)->parent != 0) {
            *directory = (*directory)->parent;
        }
        return 1;
    }
    if (!valid_name(component)) {
        return 0;
    }
    struct ramfs_node *child = find_child(*directory, component);
    if (child == 0) {
        return 0;
    }
    *directory = child;
    return 1;
}

enum ramfs_result ramfs_resolve(const char *path,
                                struct ramfs_node **node)
{
    if (node == 0) {
        return RAMFS_INVALID_PATH;
    }
    *node = 0;
    if (path == 0) {
        return RAMFS_INVALID_PATH;
    }
    if (path[0] == '\0') {
        *node = cwd;
        return RAMFS_OK;
    }

    struct ramfs_node *current =
        path[0] == '/' ? &ramfs_root_node : cwd;
    const char *cursor = path;
    if (*cursor == '/') {
        ++cursor;
    }

    char component[RAMFS_NAME_MAX + 1u];
    uint32_t depth = 0;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        size_t index = 0;
        while (*cursor != '\0' && *cursor != '/') {
            if (index >= RAMFS_NAME_MAX) {
                return RAMFS_INVALID_NAME;
            }
            component[index++] = *cursor++;
        }
        component[index] = '\0';
        if (!resolve_component(&current, component)) {
            return RAMFS_NOT_FOUND;
        }
        if (current != 0) {
            ++depth;
            if (depth > RAMFS_MAX_DEPTH) {
                return RAMFS_LIMIT;
            }
        }
    }
    *node = current;
    return RAMFS_OK;
}

enum ramfs_result ramfs_resolve_parent(const char *path,
                                       struct ramfs_node **parent,
                                       char leaf[RAMFS_NAME_MAX + 1u])
{
    if (parent == 0 || leaf == 0 || path == 0 || path[0] == '\0') {
        return RAMFS_INVALID_PATH;
    }
    leaf[0] = '\0';

    const char *last_slash = 0;
    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            last_slash = cursor;
        }
    }

    char parent_path[256];
    if (last_slash == 0) {
        if (!copy_name(leaf, path)) {
            return RAMFS_INVALID_NAME;
        }
        *parent = cwd;
        return RAMFS_OK;
    }

    size_t parent_length = (size_t)(last_slash - path);
    if (parent_length >= sizeof(parent_path)) {
        return RAMFS_INVALID_PATH;
    }
    if (parent_length == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        memory_copy(parent_path, path, parent_length);
        parent_path[parent_length] = '\0';
    }

    const char *leaf_start = last_slash + 1;
    if (!copy_name(leaf, leaf_start)) {
        return RAMFS_INVALID_NAME;
    }
    return ramfs_resolve(parent_path, parent);
}

static void unlink_child(struct ramfs_node *node)
{
    struct ramfs_node *parent = node->parent;
    if (parent == 0) {
        return;
    }
    struct ramfs_node **link = &parent->child;
    while (*link != 0 && *link != node) {
        link = &(*link)->next;
    }
    if (*link == node) {
        *link = node->next;
    }
    node->next = 0;
    node->parent = 0;
}

static void insert_child(struct ramfs_node *parent,
                         struct ramfs_node *node)
{
    node->next = parent->child;
    parent->child = node;
    node->parent = parent;
}

void ramfs_init(void)
{
    ramfs_root_node.name[0] = '/';
    ramfs_root_node.name[1] = '\0';
    ramfs_root_node.directory = 1;
    ramfs_root_node.size = 0;
    ramfs_root_node.data = 0;
    ramfs_root_node.parent = 0;
    ramfs_root_node.child = 0;
    ramfs_root_node.next = 0;
    cwd = &ramfs_root_node;
    ramfs_total = 0;
}

struct ramfs_node *ramfs_root(void)
{
    return &ramfs_root_node;
}

struct ramfs_node *ramfs_cwd(void)
{
    return cwd;
}

int ramfs_chdir(struct ramfs_node *directory)
{
    if (directory == 0 || !directory->directory) {
        return 0;
    }
    cwd = directory;
    return 1;
}

int ramfs_set_cwd_path(const char *path)
{
    struct ramfs_node *directory;
    if (ramfs_resolve(path, &directory) != RAMFS_OK ||
        directory == 0 || !directory->directory) {
        return 0;
    }
    cwd = directory;
    return 1;
}

enum ramfs_result ramfs_mkdir(const char *path)
{
    struct ramfs_node *parent;
    char leaf[RAMFS_NAME_MAX + 1u];
    enum ramfs_result result =
        ramfs_resolve_parent(path, &parent, leaf);
    if (result != RAMFS_OK) {
        return result;
    }
    if (parent == 0 || !parent->directory) {
        return RAMFS_NOT_DIRECTORY;
    }
    if (node_depth(parent) + 1u > RAMFS_MAX_DEPTH) {
        return RAMFS_LIMIT;
    }
    if (leaf[0] == '.' &&
        (leaf[1] == '\0' ||
         (leaf[1] == '.' && leaf[2] == '\0'))) {
        return RAMFS_INVALID_NAME;
    }
    if (find_child(parent, leaf) != 0) {
        return RAMFS_EXISTS;
    }

    struct ramfs_node *node = kmalloc(sizeof(*node));
    if (node == 0) {
        return RAMFS_NO_MEMORY;
    }
    memory_set(node, 0, sizeof(*node));
    copy_name(node->name, leaf);
    node->directory = 1;
    insert_child(parent, node);
    return RAMFS_OK;
}

enum ramfs_result ramfs_remove(const char *path)
{
    if (path == 0 || path[0] == '\0') {
        return RAMFS_INVALID_PATH;
    }
    struct ramfs_node *node;
    enum ramfs_result result = ramfs_resolve(path, &node);
    if (result != RAMFS_OK) {
        return result;
    }
    if (node == &ramfs_root_node) {
        return RAMFS_ROOT;
    }
    if (node->directory && node->child != 0) {
        return RAMFS_NOT_EMPTY;
    }
    if (!node->directory) {
        ramfs_total -= node->size;
        if (node->data != 0) {
            kfree(node->data);
        }
    }
    unlink_child(node);
    kfree(node);
    return RAMFS_OK;
}

enum ramfs_result ramfs_write(const char *path, const char *data,
                              size_t size)
{
    if (size > RAMFS_MAX_FILE_SIZE) {
        return RAMFS_LIMIT;
    }
    struct ramfs_node *parent;
    char leaf[RAMFS_NAME_MAX + 1u];
    enum ramfs_result result =
        ramfs_resolve_parent(path, &parent, leaf);
    if (result != RAMFS_OK) {
        return result;
    }
    if (parent == 0 || !parent->directory) {
        return RAMFS_NOT_DIRECTORY;
    }
    if (node_depth(parent) + 1u > RAMFS_MAX_DEPTH) {
        return RAMFS_LIMIT;
    }

    struct ramfs_node *node = find_child(parent, leaf);
    if (node != 0 && node->directory) {
        return RAMFS_NOT_DIRECTORY;
    }
    if (node == 0) {
        if (ramfs_total + (uint32_t)size > RAMFS_MAX_TOTAL_SIZE) {
            return RAMFS_LIMIT;
        }
        node = kmalloc(sizeof(*node));
        if (node == 0) {
            return RAMFS_NO_MEMORY;
        }
        memory_set(node, 0, sizeof(*node));
        copy_name(node->name, leaf);
        insert_child(parent, node);
    } else {
        uint32_t old_size = node->size;
        if (ramfs_total - old_size + (uint32_t)size >
            RAMFS_MAX_TOTAL_SIZE) {
            return RAMFS_LIMIT;
        }
        ramfs_total -= old_size;
        if (node->data != 0) {
            kfree(node->data);
            node->data = 0;
        }
    }

    if (size != 0) {
        node->data = kmalloc(size);
        if (node->data == 0) {
            node->size = 0;
            return RAMFS_NO_MEMORY;
        }
        memory_copy(node->data, data, size);
    } else {
        node->data = 0;
    }
    node->size = (uint32_t)size;
    ramfs_total += (uint32_t)size;
    return RAMFS_OK;
}

enum ramfs_result ramfs_append(const char *path, const char *data,
                               size_t size)
{
    struct ramfs_node *node;
    enum ramfs_result result = ramfs_resolve(path, &node);
    if (result != RAMFS_OK) {
        if (size > RAMFS_MAX_FILE_SIZE) {
            return RAMFS_LIMIT;
        }
        return ramfs_write(path, data, size);
    }
    if (node->directory) {
        return RAMFS_NOT_DIRECTORY;
    }
    if (size > RAMFS_MAX_FILE_SIZE - node->size) {
        return RAMFS_LIMIT;
    }
    if (ramfs_total + (uint32_t)size > RAMFS_MAX_TOTAL_SIZE) {
        return RAMFS_LIMIT;
    }

    size_t new_size = (size_t)node->size + size;
    char *new_data = size == 0 ? node->data : kmalloc(new_size);
    if (size != 0 && new_data == 0) {
        return RAMFS_NO_MEMORY;
    }
    if (size != 0) {
        if (node->size != 0) {
            memory_copy(new_data, node->data, node->size);
        }
        memory_copy(new_data + node->size, data, size);
        if (node->data != 0) {
            kfree(node->data);
        }
        node->data = new_data;
    }
    node->size = (uint32_t)new_size;
    ramfs_total += (uint32_t)size;
    return RAMFS_OK;
}

enum ramfs_result ramfs_copy_file(const char *source, const char *target)
{
    struct ramfs_node *source_node;
    enum ramfs_result result = ramfs_resolve(source, &source_node);
    if (result != RAMFS_OK) {
        return result;
    }
    if (source_node->directory) {
        return RAMFS_NOT_DIRECTORY;
    }

    char target_path[256];
    struct ramfs_node *target_node;
    if (ramfs_resolve(target, &target_node) == RAMFS_OK &&
        target_node != 0 && target_node->directory) {
        char leaf[RAMFS_NAME_MAX + 1u];
        copy_name(leaf, source_node->name);
        size_t prefix = ramfs_format_path(target_node, target_path,
                                          sizeof(target_path));
        if (prefix == 0 || prefix >= sizeof(target_path) - 2u) {
            return RAMFS_INVALID_PATH;
        }
        if (prefix != 1u) {
            target_path[prefix++] = '/';
        } else {
            prefix = 1u;
        }
        target_path[prefix] = '\0';
        size_t leaf_length = str_length(leaf);
        if (prefix + leaf_length >= sizeof(target_path)) {
            return RAMFS_INVALID_PATH;
        }
        memory_copy(target_path + prefix, leaf, leaf_length + 1u);
    } else {
        size_t target_length = str_length(target);
        if (target_length >= sizeof(target_path)) {
            return RAMFS_INVALID_PATH;
        }
        memory_copy(target_path, target, target_length + 1u);
    }
    return ramfs_write(target_path, source_node->data,
                       source_node->size);
}

static int is_ancestor(struct ramfs_node *node,
                       struct ramfs_node *possible_child)
{
    for (struct ramfs_node *cursor = possible_child;
         cursor != 0; cursor = cursor->parent) {
        if (cursor == node) {
            return 1;
        }
    }
    return 0;
}

enum ramfs_result ramfs_move(const char *source, const char *target)
{
    struct ramfs_node *source_node;
    enum ramfs_result result = ramfs_resolve(source, &source_node);
    if (result != RAMFS_OK) {
        return result;
    }
    if (source_node == &ramfs_root_node) {
        return RAMFS_ROOT;
    }

    char target_path[256];
    struct ramfs_node *target_node;
    if (ramfs_resolve(target, &target_node) == RAMFS_OK &&
        target_node != 0 && target_node->directory) {
        if (source_node->directory &&
            is_ancestor(source_node, target_node)) {
            return RAMFS_LIMIT;
        }
        size_t prefix = ramfs_format_path(target_node, target_path,
                                          sizeof(target_path));
        if (prefix == 0 || prefix >= sizeof(target_path) - 2u) {
            return RAMFS_INVALID_PATH;
        }
        if (prefix != 1u) {
            target_path[prefix++] = '/';
        } else {
            prefix = 1u;
        }
        size_t leaf_length = str_length(source_node->name);
        if (prefix + leaf_length >= sizeof(target_path)) {
            return RAMFS_INVALID_PATH;
        }
        memory_copy(target_path + prefix, source_node->name,
                    leaf_length + 1u);
    } else {
        size_t target_length = str_length(target);
        if (target_length >= sizeof(target_path)) {
            return RAMFS_INVALID_PATH;
        }
        memory_copy(target_path, target, target_length + 1u);
    }

    struct ramfs_node *new_parent;
    char leaf[RAMFS_NAME_MAX + 1u];
    result = ramfs_resolve_parent(target_path, &new_parent, leaf);
    if (result != RAMFS_OK) {
        return result;
    }
    if (new_parent == 0 || !new_parent->directory) {
        return RAMFS_NOT_DIRECTORY;
    }
    if (source_node->directory &&
        is_ancestor(source_node, new_parent)) {
        return RAMFS_LIMIT;
    }
    if (node_depth(new_parent) + 1u > RAMFS_MAX_DEPTH &&
        source_node->directory) {
        return RAMFS_LIMIT;
    }

    struct ramfs_node *existing = find_child(new_parent, leaf);
    if (existing != 0) {
        if (existing == source_node) {
            return RAMFS_OK;
        }
        if (existing->directory) {
            return RAMFS_EXISTS;
        }
        ramfs_total -= existing->size;
        if (existing->data != 0) {
            kfree(existing->data);
        }
        unlink_child(existing);
        kfree(existing);
    }

    unlink_child(source_node);
    if (!copy_name(source_node->name, leaf)) {
        insert_child(source_node->parent, source_node);
        return RAMFS_INVALID_NAME;
    }
    insert_child(new_parent, source_node);
    return RAMFS_OK;
}

size_t ramfs_format_cwd(char *buffer, size_t capacity)
{
    if (buffer == 0 || capacity == 0) {
        return 0;
    }
    return ramfs_format_path(cwd, buffer, capacity);
}

size_t ramfs_format_path(const struct ramfs_node *node,
                         char *buffer, size_t capacity)
{
    if (node == 0 || buffer == 0 || capacity == 0) {
        return 0;
    }
    if (node == &ramfs_root_node) {
        buffer[0] = '/';
        buffer[1] = '\0';
        return 1;
    }

    char parts[RAMFS_MAX_DEPTH][RAMFS_NAME_MAX + 1u];
    uint32_t depth = 0;
    const struct ramfs_node *cursor = node;
    while (cursor != 0 && cursor != &ramfs_root_node) {
        if (depth >= RAMFS_MAX_DEPTH) {
            buffer[0] = '\0';
            return 0;
        }
        memory_copy(parts[depth], cursor->name,
                    str_length(cursor->name) + 1u);
        ++depth;
        cursor = cursor->parent;
    }

    size_t length = 0;
    if (capacity < 2u) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[length++] = '/';
    for (uint32_t index = 0; index < depth; ++index) {
        const char *part = parts[depth - index - 1u];
        size_t part_length = str_length(part);
        if (length + part_length + (index + 1u < depth ? 1u : 0u) >=
            capacity) {
            buffer[length] = '\0';
            return length;
        }
        memory_copy(buffer + length, part, part_length);
        length += part_length;
        if (index + 1u < depth) {
            buffer[length++] = '/';
        }
    }
    buffer[length] = '\0';
    return length;
}

uint32_t ramfs_total_size(void)
{
    return ramfs_total;
}
