#ifndef CHIBI_TEST_CENSUS_TREE_H
#define CHIBI_TEST_CENSUS_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The syntax tree the native census reads: the rows of compiler_syntax.Parse,
 * the authored parser's tree, in pre-order with links by index. Absent links
 * are -1. A census_node is a row of one tree, or null when the index is
 * negative. The census owns a tree for the lifetime of its sources. */
typedef struct
{
    int count;
    char **kinds, **fields;
    int *starts, *ends, *parents, *first_children, *next_siblings;
    bool *named, *has_errors;
} census_tree;

typedef struct
{
    const census_tree *tree;
    int index;
} census_node;

typedef struct
{
    const census_tree *tree;
    int current;
} census_cursor;

static inline census_node census_node_at(const census_tree *tree, int index)
{
    return (census_node){ index < 0 ? NULL : tree, index };
}
static inline bool census_node_is_null(census_node node)
{
    return !node.tree || node.index < 0;
}
static inline const char *census_node_type(census_node node)
{
    if (census_node_is_null(node)) {
        return "";
    }
    return node.tree->kinds[node.index];
}
static inline uint32_t census_node_start(census_node node)
{
    return (uint32_t)node.tree->starts[node.index];
}
static inline uint32_t census_node_end(census_node node)
{
    return (uint32_t)node.tree->ends[node.index];
}
static inline bool census_node_has_error(census_node node)
{
    return node.tree->has_errors[node.index];
}
static inline census_node census_node_parent(census_node node)
{
    if (census_node_is_null(node)) {
        return node;
    }
    return census_node_at(node.tree, node.tree->parents[node.index]);
}
static inline census_node census_node_field(census_node node, const char *name)
{
    if (census_node_is_null(node)) {
        return node;
    }
    for (int child = node.tree->first_children[node.index]; child >= 0;
         child = node.tree->next_siblings[child]) {
        if (!strcmp(node.tree->fields[child], name)) {
            return census_node_at(node.tree, child);
        }
    }
    return census_node_at(node.tree, -1);
}
static inline census_node census_node_named_child(census_node node, uint32_t at)
{
    if (census_node_is_null(node)) {
        return node;
    }
    for (int child = node.tree->first_children[node.index]; child >= 0;
         child = node.tree->next_siblings[child]) {
        if (node.tree->named[child] && at-- == 0) {
            return census_node_at(node.tree, child);
        }
    }
    return census_node_at(node.tree, -1);
}
static inline uint32_t census_node_named_children(census_node node)
{
    uint32_t count = 0;
    if (census_node_is_null(node)) {
        return 0;
    }
    for (int child = node.tree->first_children[node.index]; child >= 0;
         child = node.tree->next_siblings[child]) {
        count += node.tree->named[child];
    }
    return count;
}

/* A cursor over the children of a node, with their field names. */
static inline census_cursor census_cursor_new(census_node node)
{
    return (census_cursor){ node.tree, node.index };
}
static inline bool census_cursor_first_child(census_cursor *cursor)
{
    int child = cursor->tree->first_children[cursor->current];
    if (child < 0) {
        return false;
    }
    cursor->current = child;
    return true;
}
static inline bool census_cursor_next_sibling(census_cursor *cursor)
{
    int next = cursor->tree->next_siblings[cursor->current];
    if (next < 0) {
        return false;
    }
    cursor->current = next;
    return true;
}
static inline census_node census_cursor_node(const census_cursor *cursor)
{
    return census_node_at(cursor->tree, cursor->current);
}
/* The current node's field name, or NULL when it has none. */
static inline const char *census_cursor_field(const census_cursor *cursor)
{
    const char *field = cursor->tree->fields[cursor->current];
    return *field ? field : NULL;
}

#endif
